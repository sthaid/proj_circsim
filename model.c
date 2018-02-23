#if 0
XXX -O0
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

#define SET_MODEL_REQ(x) \
    do { \
        model_state_req = (x); \
        __sync_synchronize(); \
        while (model_state != model_state_req) { \
            usleep(1000); \
        } \
    } while (0)

#define MAX_DELTA_T_S       (1e-3)  // 1 ms
#define MIN_DCPWR_RAMP_T_S  (1e-3)  // 1 ms

//
// typedefs
//

//
// variables
//

static int32_t  model_state_req;
static double   model_stop_time_s;

static node_t * ground_node;

static double   delta_t_s;
static double   dcpwr_ramp_t_s;

//
// prototypes
//
  
static int32_t model_run(double run_intvl_s);
static int32_t model_stop(void);
static int32_t model_cont(double run_intvl_s);
static int32_t model_step(void);
static int32_t model_init_nodes(void);
static node_t * allocate_node(void);
static void add_terms_to_node(node_t *node, gridloc_t *gl);
static void debug_print_nodes(void);
static void * model_thread(void * cx);
static double get_comp_power_voltage(component_t * c);

// -----------------  PUBLIC ---------------------------------------------------------

void model_init(void)
{
    pthread_t thread_id;

    // init variables
    node_v_prior_idx = 0;
    node_v_curr_idx  = 1;
    node_v_next_idx  = 2;

    // create the model_thread
    pthread_create(&thread_id, NULL, model_thread, NULL);
}

int32_t model_cmd(char *cmdline)
{
    int32_t rc;
    char *cmd;
    double run_intvl_s;

    // extract cmd from cmdline
    cmd = strtok(cmdline, " ");
    if (cmd == NULL) {
        ERROR("usage: XXX\n");
        return 0;
    }

    // scan PARAM_RUN_INTVL_T
    if (str_to_val(PARAM_RUN_INTVL_T, UNITS_SECONDS, &run_intvl_s) == -1) {
        ERROR("invalid interval '%s'\n", PARAM_RUN_INTVL_T);
        return -1;
    }

    // parse and process the cmd
    if (strcmp(cmd, "reset") == 0) {
        rc = model_reset();
    } else if (strcmp(cmd, "run") == 0) {
        rc = model_run(run_intvl_s);
    } else if (strcmp(cmd, "stop") == 0) {
        rc = model_stop();
    } else if (strcmp(cmd, "cont") == 0) {
        rc = model_cont(run_intvl_s);
    } else if (strcmp(cmd, "step") == 0) {
        rc = model_step();
    } else {
        ERROR("unsupported cmd '%s'\n", cmd);
        rc = -1;
    }

    return rc;
}

// -----------------  MODEL SUPPORT  -------------------------------------------------

int32_t model_reset(void)
{
    int32_t i;

    SET_MODEL_REQ(MODEL_STATE_RESET);

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

    model_time_s = 0;
    model_stop_time_s = 0;
    max_node = 0;

    return 0;
}

static int32_t model_run(double run_intvl_s)          
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

    model_stop_time_s = run_intvl_s;

    SET_MODEL_REQ(MODEL_STATE_RUNNING);

    return 0;
}

static int32_t model_stop(void)
{
    if (model_state != MODEL_STATE_RUNNING) {
        ERROR("not running\n");
        return -1;
    }

    SET_MODEL_REQ(MODEL_STATE_STOPPED);

    return 0;
}

static int32_t model_cont(double run_intvl_s)
{
    if (model_state != MODEL_STATE_STOPPED) {
        ERROR("not stopped\n");
        return -1;
    }

    model_stop_time_s = (run_intvl_s ? model_time_s + run_intvl_s : 0);

    SET_MODEL_REQ(MODEL_STATE_RUNNING);

    return 0;
}

static int32_t model_step(void)
{
    if (model_state != MODEL_STATE_STOPPED && model_state != MODEL_STATE_RESET) {
        ERROR("not stopped or reset\n");
        return -1;
    }

    if (model_state == MODEL_STATE_RESET) {
        model_run(1e-30);
    } else {
        model_cont(1e-30);
    }

    return 0;
}


static int32_t model_init_nodes(void)
{
    int32_t i, j, ground_node_count, power_count;

    // create a list of nodes, eliminating the wire component;
    // so that each node provides a list of connected real components
    // such as resistors, capacitors, diodes etc.
    //
    // loop over all components 
    //   if the component is a WIRE then continue
    //   loop over the component's terminals
    //     if the terminal is already part of a node then continue;
    //     allocate a new node;
    //     call add_terms_to_node to add all terms on this gridloc, and
    //      connected to this gridloc to the node;
    //   endloop
    // endloop
    for (i = 0; i < max_component; i++) {
        component_t * c = &component[i];
        if (c->type == COMP_NONE || c->type == COMP_WIRE) {
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
    ground_node_count = 0;
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
            ERROR("power %s has terminal 0 connected to ground\n", c->comp_str);
            return -1;
        }
        if (c->term[1].node != ground_node) {
            ERROR("power %s has terminal 1 connected to non-ground\n", c->comp_str);
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
    return 0;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - 

static node_t * allocate_node(void)
{
    node_t * n;

    assert(max_node < MAX_NODE);
    n = &node[max_node++];

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
        DEBUG("%ld: MAX_ALLOCED_GRIDLOC IS NOW %d\n", n-node, n->max_alloced_gridloc);
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
        if (c->type == COMP_WIRE) {
            int32_t other_term_id = (term->termid ^ 1);
            terminal_t * other_term = &c->term[other_term_id];
            add_terms_to_node(n, &other_term->gridloc);
        } else {
            assert(term->node == NULL);

            // XXX assert(n->max_term < n->max_alloced_term);
            if (n->max_term >= n->max_alloced_term) {
                n->max_alloced_term = (n->max_term == 0 ? 8 : n->max_term * 2);
                DEBUG("%ld: MAX_ALLOCED_TERM IS NOW %d\n", n-node, n->max_alloced_term);
                n->term = realloc(n->term, n->max_alloced_term * sizeof(terminal_t));
            }
            n->term[n->max_term] = term;
            term->current = NO_VALUE;
            n->max_term++;

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
    char s[200], s1[100], *p;
    int32_t i, j;

    // XXX needs larger string, or do something about gridloc
    return;
 
    INFO("max_node = %d\n", max_node);
    for (i = 0; i < max_node; i++) {
        node_t * n = &node[i];

        INFO("node %d - max_gridloc=%d  max_term=%d  ground=%d  power=%p %s\n", 
             i, n->max_gridloc, n->max_term, n->ground, n->power,
             n->power ? n->power->component->comp_str : "");
        
        p = s;
        for (j = 0; j < n->max_gridloc; j++) {
            p += sprintf(p, "%s ", gridloc_to_str(&n->gridloc[j],s1));
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
    int32_t i,j,rc;

    while (true) {
        // handle request to transition model_state
        while (true) {
            if (model_state_req != model_state) {
                INFO("model_state is %s\n", MODEL_STATE_STR(model_state_req));
            }
            model_state = model_state_req;
            __sync_synchronize();
            if (model_state == MODEL_STATE_RUNNING) {
                break;
            }
            usleep(1000);
        }

        // XXX only when starting model ?
        //     or when param has changed

        // get delta_t_s from param
        rc = str_to_val(PARAM_DELTA_T, UNITS_SECONDS, &delta_t_s);
        if (rc < 0 || delta_t_s <= 0 || delta_t_s > MAX_DELTA_T_S) {
            ERROR("delta_t_us must be in range 0 to %.3le seconds\n", MAX_DELTA_T_S);
            model_state_req = MODEL_STATE_STOPPED;
            continue;
        }
        // get dcpwr_ramp_t_s from param
        rc = str_to_val(PARAM_DCPWR_RAMP_T, UNITS_SECONDS, &dcpwr_ramp_t_s);
        if (rc < 0 || dcpwr_ramp_t_s < MIN_DCPWR_RAMP_T_S) {
            ERROR("dcpwr_ramp_t must be >= %.3lf seconds\n", MIN_DCPWR_RAMP_T_S);
            model_state_req = MODEL_STATE_STOPPED;
            continue;
        }

        // loop over all nodes, computing the next voltage for that node
        for (i = 0; i < max_node; i++) {
            node_t * n = & node[i];

            if (n->ground) {
                NODE_V_NEXT(n) = 0;
            } else if (n->power) {
                component_t * c = n->power->component;
                NODE_V_NEXT(n) = get_comp_power_voltage(c);
            } else {
                double r_sum_num=0, r_sum_denom=0;
                double c_sum_num=0, c_sum_denom=0;
                for (j = 0; j < n->max_term; j++) {
                    component_t *c = n->term[j]->component;
                    int32_t other_termid = (n->term[j]->termid ^ 1);
                    node_t *other_n = c->term[other_termid].node;

                    switch (c->type) {
                    case COMP_RESISTOR:
                        r_sum_num += (NODE_V_CURR(other_n) / c->resistor.ohms);
                        r_sum_denom += (1 / c->resistor.ohms);
                        break;
                    case COMP_CAPACITOR:
                        c_sum_num += (NODE_V_CURR(n) + NODE_V_CURR(other_n) - NODE_V_PRIOR(other_n)) *
                                     (c->capacitor.farads / delta_t_s);
                        c_sum_denom += c->capacitor.farads / delta_t_s;
                        break;
                    default:
                        FATAL("comp type %s not supported\n", c->type_str);
                    }
                }
                NODE_V_NEXT(n) = (r_sum_num + c_sum_num) / 
                                 (r_sum_denom + c_sum_denom);
            }
        }

        // compute the current from each node terminal, using the voltage just calculated   xxx
        for (i = 0; i < max_node; i++) {
            node_t * n = & node[i];
            double total_current = 0;

            for (j = 0; j < n->max_term; j++) {
                terminal_t *term = n->term[j];
                component_t *c = term->component;
                int32_t other_termid = (term->termid ^ 1);
                node_t *other_n = c->term[other_termid].node;

                switch (c->type) {
                case COMP_RESISTOR:
                    term->current = (NODE_V_NEXT(n) - NODE_V_NEXT(other_n)) / c->resistor.ohms;
                    total_current += term->current;
                    break;
                case COMP_CAPACITOR:
                    term->current = ((NODE_V_NEXT(n) - NODE_V_CURR(n)) - (NODE_V_NEXT(other_n) - NODE_V_CURR(other_n)))
                                     * c->capacitor.farads / delta_t_s;
                    //printf("NODE %d CAP TERMID %d  CURRENT %lf\n", i, term->termid, term->current);
                    total_current += term->current;
                    break;
                }
            }

            if (n->power) {
                n->power->current = -total_current;
            }
        }
        // XXX what about ground node current

        // increment the idx values which affect the operation of the 
        // macros which return the prior, current, and next values
        // - update the current and prior node voltage
        node_v_prior_idx = (node_v_prior_idx + 1) % 3;
        node_v_curr_idx  = (node_v_curr_idx + 1) % 3;
        node_v_next_idx  = (node_v_next_idx + 1) % 3;

        // increment time
        model_time_s += delta_t_s;

        // if model stop time is specified and model time exceeds stop time then stop
        if (model_stop_time_s > 0 && model_time_s >= model_stop_time_s) {                
            model_state_req = MODEL_STATE_STOPPED;   
        }
    }
    return NULL;
}

static double get_comp_power_voltage(component_t * c)
{
    double v;

    if (c->power.hz != 0) {
        FATAL("AC not supported yet\n");
    }
    
    if (model_time_s > dcpwr_ramp_t_s) {
        v = c->power.volts;
    } else {
        v = c->power.volts * sin((model_time_s / dcpwr_ramp_t_s) * M_PI_2);
    }

    return v;
}

