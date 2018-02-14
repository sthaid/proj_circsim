#if 0
XXX TESTS
- series/parallel resistors
- infinite resistor array
- resistor / capacitor time constant
- capacitor / inductor resonant circuit
#endif

#include "common.h"

//
// defines
//

//
// typedefs
//

//
// variables
//

static int32_t  circsim_state_req;
static node_t * ground_node;

//
// prototypes
//
  
static int32_t model_reset(void);
static int32_t model_run(void);
static int32_t model_pause(void);
static int32_t model_continue(void);
static int32_t model_init_nodes(void);
static node_t * allocate_node(void);
static void add_terms_to_node(node_t *node, gridloc_t *gl);
static void debug_print_nodes(void);
static void * model_thread(void * cx);
static float get_comp_power_voltage(component_t * c);

// -----------------  PUBLIC ---------------------------------------------------------

void circsim_init(void)
{
    pthread_t thread_id;

    // init variables
    node_v_prior_idx = 0;
    node_v_curr_idx  = 1;
    node_v_next_idx  = 2;

    // create the model_thread
    pthread_create(&thread_id, NULL, model_thread, NULL);
}

int32_t circsim_cmd(char *cmd)
{
    int32_t rc;

    // parse and process the cmd
    if (strcasecmp(cmd, "reset") == 0) {
        rc = model_reset();
    } else if (strcasecmp(cmd, "run") == 0) {
        rc = model_run();
    } else if (strcasecmp(cmd, "pause") == 0) {
        rc = model_pause();
    } else if (strcasecmp(cmd, "cont") == 0) {
        rc = model_continue();
    } else {
        ERROR("unsupported cmd '%s'\n", cmd);
        rc = -1;
    }

    return rc;
}

// -----------------  MODEL SUPPORT  -------------------------------------------------

#define SET_CIRSIM_REQ(x) \
    do { \
        circsim_state_req = (x); \
        __sync_synchronize(); \
        while (circsim_state != circsim_state_req) { \
            usleep(1000); \
        } \
    } while (0)

static int32_t model_reset(void)
{
    int32_t i;

    SET_CIRSIM_REQ(CIRCSIM_STATE_RESET);

    for (i = 0; i < max_node; i++) {
        node_t *n = &node[i];
        memset(&n->zero_init_node_state, 
               0,
               sizeof(node_t) - offsetof(node_t,zero_init_node_state));
    }

    for (i = 0; i < max_component; i++) {
        component_t *c = &component[i];
        c->term[0].node = NULL;
        c->term[1].node = NULL;
        memset(&c->zero_init_component_state, 
               0,
               sizeof(component_t) - offsetof(component_t,zero_init_component_state));
    }

    circsim_time = 0;
    max_node = 0;

    INFO("success\n");
    return 0;
}

static int32_t model_run(void)
{
    int32_t rc;

    rc = model_reset();
    if (rc < 0) {
        return -1;
    }

    rc = model_init_nodes();
    if (rc < 0) {
        return -1;
    }

    SET_CIRSIM_REQ(CIRCSIM_STATE_RUNNING);

    return 0;
}

static int32_t model_pause(void)
{
    if (circsim_state != CIRCSIM_STATE_RUNNING) {
        ERROR("not running\n");
        return -1;
    }

    SET_CIRSIM_REQ(CIRCSIM_STATE_PAUSED);

    return 0;
}

static int32_t model_continue(void)
{
    if (circsim_state != CIRCSIM_STATE_PAUSED) {
        ERROR("not paused\n");
        return -1;
    }

    SET_CIRSIM_REQ(CIRCSIM_STATE_RUNNING);

    return 0;
}

static int32_t model_init_nodes(void)
{
    int32_t i, j, ground_node_count, power_count;

    // create a list of nodes, eliminating the connection component;
    // so that each node provides a list of connected real components
    // such as resistors, capacitors, diodes etc.
    //
    // loop over all components 
    //   if the component is a CONNECTION then continue
    //   loop over the component's terminals
    //     if the terminal is already part of a node then continue;
    //     allocate a new node;
    //     call add_terms_to_node to add all terms on this gridloc, and
    //      connected to this gridloc to the node;
    //   endloop
    // endloop
    for (i = 0; i < max_component; i++) {
        component_t * c = &component[i];
        if (c->type == COMP_NONE || c->type == COMP_CONNECTION) {
            continue;
        }
        for (j = 0; j < 2; j++) {
            terminal_t * term = &c->term[j];
            if (term->node) {
                continue;
            }
            node_t * n = allocate_node();
            add_terms_to_node(n, &c->term[j].gridloc);
        }
    }

    // debug print the list of nodes generated above
    debug_print_nodes();

    // if no nodes then return error
    if (max_node == 0) {
        ERROR("no components\n");
        return -1;
    }

    // verify that exactly one node is a ground node
    for (i = 0; i < max_node; i++) {
        if (node[i].ground) {
            ground_node_count++;
            ground_node = &node[i];
        }
    }
    if (ground_node_count != 1) {
        ERROR("ground_node_count=%d, must be 1\n", ground_node_count);
        return -1;
    }

    // verify power:
    // - error if term[0] is connected to the ground node;
    // - error if term[1] is not connected to the ground node;
    // - error if non ground nodes have more than 1 power connected
    for (i = 0; i < max_component; i++) {
        component_t *c = &component[i];
        if (c->type != COMP_POWER) {
            continue;
        }
        if (c->term[0].node == ground_node) {
            ERROR("power compid %d has terminal 0 connected to ground\n", c->compid);
            return -1;
        }
        if (c->term[1].node != ground_node) {
            ERROR("power compid %d has terminal 1 connected to non-ground\n", c->compid);
            return -1;
        }
    }
    for (i = 0; i < max_node; i++) {
        node_t *n = &node[i];
        if (&node[i] == ground_node) {
            continue;
        }
        power_count = 0;
        for (j = 0; j < n->max_term; j++) {
            if (n->term[j]->component->type == COMP_POWER) {
                power_count++;
            }
        }
        if (power_count > 1) {
            ERROR("multiple power components interconnected\n");
            return -1;
        }
    }

    // return success
    INFO("success\n");
    return 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - 

static node_t * allocate_node(void)
{
    node_t * n;

    assert(max_node < MAX_NODE);
    n = &node[max_node++];

#if 0
    // xxx reallocate if not big enough
    //     this will be done in other routines
    if (n->max_alloced_term == 0) {
        n->term = malloc(100*sizeof(terminal_t));
        n->max_alloced_term = 100;
    }
    if (n->max_alloced_gridloc == 0) {
        n->gridloc = malloc(100*sizeof(gridloc_t));
        n->max_alloced_gridloc = 100;
    }
#endif

    memset(&n->zero_init_node_state, 
           0,
           sizeof(node_t) - offsetof(node_t,zero_init_node_state));

    return n;
}

static void add_terms_to_node(node_t *n, gridloc_t *gl)
{
    int32_t i;
    grid_t *g;

    // if this gridloc is already part of node then return
    for (i = 0; i < n->max_gridloc; i++) {
        if (gl->x == n->gridloc[i].x && gl->y == n->gridloc[i].y) {
            return;
        }
    }

    // add this gridloc to the node
    // XXX assert(n->max_gridloc < n->max_alloced_gridloc);
    if (n->max_gridloc >= n->max_alloced_gridloc) {
        n->max_alloced_gridloc = (n->max_gridloc == 0 ? 8 : n->max_gridloc * 2);
        INFO("%ld: MAX_ALLOCED_GRIDLOC IS NOW %d\n", n-node, n->max_alloced_gridloc);
        n->gridloc = realloc(n->gridloc, n->max_alloced_gridloc * sizeof(gridloc_t));
    }
    n->gridloc[n->max_gridloc++] = *gl;

    // add all terminals that are either direcctly connected
    // to this gridloc or are on other gridlocs that are connected
    // to this gridloc
    g = &grid[gl->x][gl->y];
    for (i = 0; i < g->max_term; i++) {
        terminal_t * term = g->term[i];
        component_t * c = term->component;
        if (c->type == COMP_CONNECTION) {
            int32_t other_term_id = (term->termid ^ 1);
            terminal_t * other_term = &c->term[other_term_id];
            add_terms_to_node(n, &other_term->gridloc);
        } else {
            assert(term->node == NULL);

            // XXX assert(n->max_term < n->max_alloced_term);
            if (n->max_term >= n->max_alloced_term) {
                n->max_alloced_term = (n->max_term == 0 ? 8 : n->max_term * 2);
                INFO("%ld: MAX_ALLOCED_TERM IS NOW %d\n", n-node, n->max_alloced_term);
                n->term = realloc(n->term, n->max_alloced_term * sizeof(terminal_t));
            }
            n->term[n->max_term++] = term;

            term->node = n;

            // xxx comment
            if (term->component->type == COMP_POWER && term->termid == 0) {
                n->power = term;
            }
        }
    }

    // if this grid location is ground then set this node's ground flag
    if (g->ground) {
        n->ground = true;
    }
}

static void debug_print_nodes(void)
{
    char s[200], *p;
    int32_t i, j;

    // XXX needs larger string
    return;
 
    INFO("max_node = %d\n", max_node);
    for (i = 0; i < max_node; i++) {
        node_t * n = &node[i];

        INFO("node %d - max_gridloc=%d  max_term=%d  ground=%d  power=%p power_compid=%d\n", 
             i, n->max_gridloc, n->max_term, n->ground, n->power,
             n->power ? n->power->component->compid : -1);
        
        p = s;
        for (j = 0; j < n->max_gridloc; j++) {
            p += sprintf(p, "%s ", make_gridloc_str(&n->gridloc[j]));
        }
        INFO("   gridlocs %s\n", s);

        p = s;
        for (j = 0; j < n->max_term; j++) {
            p += sprintf(p, "copmid,term=%ld,%d ", 
                    n->term[j]->component - component,
                    n->term[j]->termid);
        }
        INFO("   terms %s\n", s);
    }
}

// -----------------  MODEL THREAD  --------------------------------------------------

// xxx description
//
// https://ocw.mit.edu/courses/electrical-engineering-and-computer-science/6-071j-introduction-to-electronics-signals-and-measurement-spring-2006/lecture-notes/capactr_inductr.pdf
//
// the sum of the curren flowing into a node through resistors is
//   SUM ((Vn - V) / Rn) = SUM (Vn/Rn) - V * SUM (1/Rn)
//   where Vn is the voltage on the other side of the resistor
//         Rn is the resistance
//         V is the voltage of the node
// setting this to 0 and solving for V
//   SUM (Vn/Rn) - V * SUM (1/Rn) = 0
//   V * SUM (1/Rn) = SUM (Vn/Rn)
//
//       SUM (Vn/Rn)
//   V = -----------
//       SUM (1/Rn)

static void * model_thread(void * cx) 
{
    int32_t i;

    while (true) {
        // wait here until requested to run the model
        while (circsim_state_req != CIRCSIM_STATE_RUNNING) {
            circsim_state = circsim_state_req;
            usleep(1000);
        }
        circsim_state = CIRCSIM_STATE_RUNNING;
        __sync_synchronize();

        // xxx comment
        for (i = 0; i < max_node; i++) {
            node_t * n = & node[i];

            if (n->ground) {
                NODE_V_NEXT(n) = 0;
            } else if (n->power) {
                component_t * c = n->power->component;
                NODE_V_NEXT(n) = get_comp_power_voltage(c);
            } else {
                //       SUM (Vn/Rn)
                //   V = -----------
                //       SUM (1/Rn)
                float sum1=0, sum2=0;
                int32_t j;
                for (j = 0; j < n->max_term; j++) {
                    component_t *c = n->term[j]->component;
                    int32_t other_termid = (n->term[j]->termid ^ 1);
                    node_t *other_n = c->term[other_termid].node;

                    assert(c->type == COMP_RESISTOR);
                    sum1 += (NODE_V_CURR(other_n) / c->resistor.ohms);
                    sum2 += (1 / c->resistor.ohms);
                }
                // xxx INFO("sum1 = %f sum2 = %f\n", sum1, sum2);
                NODE_V_NEXT(n) = sum1 / sum2;
            }
        }

        // postprocessing:
        // - update the current and prior node voltage
        node_v_prior_idx = (node_v_prior_idx + 1) % 3;
        node_v_curr_idx  = (node_v_curr_idx + 1) % 3;
        node_v_next_idx  = (node_v_next_idx + 1) % 3;

#if 0
        // xxx debug print the node voltages
        for (i = 0; i < max_node; i++) {
            node_t * n = & node[i];
            INFO("node %d v_curr %f\n", i,  NODE_V_CURR(n));
        }
#endif

        // increment time
        double delta_t_us;  // XXX how to validity check
        sscanf(PARAM_DELTA_T_US, "%lf", &delta_t_us);
        circsim_time += (delta_t_us / 1000000.);
        // xxx INFO("TIME %f\n", circsim_time);

        //usleep(100000);  // xxx temp
    }
    return NULL;
}

static float get_comp_power_voltage(component_t * c)
{
    float v;

    if (c->power.hz != 0) {
        FATAL("AC not supported yet\n");
    }

    // ramp up to dc voltage in 100 ms
    if (circsim_time > .1) {
        v = c->power.volts;
    } else {
        v = circsim_time / .1 * c->power.volts;
    }
    // xxx INFO("volts %f\n", v);

    return v;
}

