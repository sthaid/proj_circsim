// XXX -O0
                //       SUM (Vn/Rn)
                //   V = -----------
                //       SUM (1/Rn)
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

#define SET_MODEL_REQ(x) \
    do { \
        model_state_req = (x); \
        __sync_synchronize(); \
        while (model_state != model_state_req) { \
            usleep(1000); \
        } \
    } while (0)

//
// typedefs
//

//
// variables
//

static int32_t  model_state_req;
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
//static double get_comp_power_voltage(component_t * c);

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

int32_t model_cmd(char *cmd)
{
    int32_t rc;

    // parse and process the cmd
    if (strcmp(cmd, "reset") == 0) {
        rc = model_reset();
    } else if (strcmp(cmd, "run") == 0) {
        rc = model_run();
    } else if (strcmp(cmd, "pause") == 0) {
        rc = model_pause();
    } else if (strcmp(cmd, "cont") == 0) {
        rc = model_continue();
    } else {
        ERROR("unsupported cmd '%s'\n", cmd);
        rc = -1;
    }

    return rc;
}

// XXX run and cont cmd should give a duration

// -----------------  MODEL SUPPORT  -------------------------------------------------

static int32_t model_reset(void)
{
    int32_t i;

    if (model_state == MODEL_STATE_RESET) {
        return 0;
    }

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

    SET_MODEL_REQ(MODEL_STATE_RUNNING);

    return 0;
}

static int32_t model_pause(void)
{
    if (model_state != MODEL_STATE_RUNNING) {
        ERROR("not running\n");
        return -1;
    }

    SET_MODEL_REQ(MODEL_STATE_PAUSED);

    return 0;
}

static int32_t model_continue(void)
{
    if (model_state != MODEL_STATE_PAUSED) {
        ERROR("not paused\n");
        return -1;
    }

    SET_MODEL_REQ(MODEL_STATE_RUNNING);

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
    INFO("success\n");
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
            n->term[n->max_term] = term;
            term->current = NO_VALUE;   // XXX be sure to reset this too
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

    // XXX needs larger string
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
    int32_t i,j;
    double delta_t_us, delta_t_s;  // XXX how to validity check

    while (true) {
        // wait here until requested to run the model
        while (model_state_req != MODEL_STATE_RUNNING) {
            model_state = model_state_req;
            usleep(1000);
        }
        model_state = MODEL_STATE_RUNNING;
        __sync_synchronize();

        // get delta time (secs) from param
        sscanf(PARAM_DELTA_T_US, "%lf", &delta_t_us);
        delta_t_s = delta_t_us * 1e-6;

        // adjust power compoents
        // xxx need predefined list
        for (i = 0; i < max_component; i++) {
            component_t *c = &component[i];
            if (c->type != COMP_POWER) {
                continue;
            }

            //if (model_time_s > .001) {
                //break;
            //}

            // xxx this is for dc
            // xxx deltat_t should be <= 1 us
            // adjust rate is from 0 to voltage in 10 ms
            // apply adjustment equally to both sides;  (excpet when powers are connected togethor)

            node_t *n0, *n1;
            static double v;

            n0 = c->term[0].node;
            n1 = c->term[1].node;
            //v0 = NODE_V_CURR(n0);
            //v1 = NODE_V_CURR(n1);

            if (model_time_s <= .001) {
                v = 5. * sin(model_time_s * (M_PI_2 / .001));
            } 
#if 0
            else {
                if (model_time_s < .0011) {
                    printf("BREAK\n");
                }
                break;
            }
#endif
#if 0
            if (model_time_s < .0011 || model_time_s > .99999) {
                printf("n0 c-p = %.15lf\n", NODE_V_CURR(n0) - NODE_V_PRIOR(n0));
                printf("n1 c-p = %.15lf\n", NODE_V_CURR(n1) - NODE_V_PRIOR(n1));
                printf("------------\n");
            }
#endif

#if 0
            static double offset;
            double a = -(NODE_V_CURR(n0) - NODE_V_PRIOR(n0));
            double b = NODE_V_CURR(n1) - NODE_V_PRIOR(n1);

            if (a > b) {
                offset += .0001;
            } else {
                offset -= .0001;
            }
            if (model_time_s < .0011 || model_time_s > .99999) {
                printf("a = %.15lf b = %.15lf offset = %lf\n", a, b, offset);
            }
#else
            static double offset = 0;
#endif


            double a = -c->term[0].current;
            double b = c->term[1].current;
            if (a > b + .0001) {
                offset -= .0001;
            } else if (a < b - .001) {
                offset += .00001;
            }

            

            NODE_V_CURR(n0) =  1 * v + offset;
            NODE_V_CURR(n1) = -0 * v + offset;
            NODE_V_PRIOR(n0) = NODE_V_CURR(n0);
            NODE_V_PRIOR(n1) = NODE_V_CURR(n1);

#if 0
            // if (model_time_s < .0011 || model_time_s > .99999) 
            {
                printf("%lf  POWER TERM CURRENT  %lf  %lf  offset=%lf   v %lf %lf\n",
                        model_time_s, a, b, offset,
                        NODE_V_CURR(n0), NODE_V_CURR(n1));
            }
#endif
        }

        // xxx comment
        for (i = 0; i < max_node; i++) {
            node_t * n = &node[i];
            double r_sum_num=0, r_sum_denom=0;
            double c_sum_num=0, c_sum_denom=0;

            for (j = 0; j < n->max_term; j++) {
                component_t *c = n->term[j]->component;
                int32_t other_termid = (n->term[j]->termid ^ 1);
                node_t *other_n = c->term[other_termid].node;

                switch (c->type) {
                case COMP_POWER: {
                    double farads = 1000;  // xxx or more?
                    c_sum_num += (NODE_V_CURR(n) + NODE_V_CURR(other_n) - NODE_V_PRIOR(other_n)) *
                                 (farads / delta_t_s);
                    c_sum_denom += farads / delta_t_s;
                    break; }
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
#if 0
            if (model_time_s < 0.0011 || model_time_s > .99999) {
                printf("%lf  P,C,N = %.12lf %.12lf %.12lf    node %d\n",  
                    model_time_s, NODE_V_PRIOR(n), NODE_V_CURR(n), NODE_V_NEXT(n), i);
            }
#endif
        }

        // compute the current from each node terminal
        for (i = 0; i < max_node; i++) {
            node_t * n = & node[i];
            double total_current = 0;
            terminal_t *power_term = NULL;
            for (j = 0; j < n->max_term; j++) {
                terminal_t *term = n->term[j];
                component_t *c = term->component;
                int32_t other_termid = (term->termid ^ 1);
                node_t *other_n = c->term[other_termid].node;

                switch (c->type) {
                case COMP_POWER:
                    power_term = term;
#if 0
                    term->current = ((NODE_V_NEXT(n) - NODE_V_CURR(n)) - (NODE_V_NEXT(other_n) - NODE_V_CURR(other_n)))
                                     * 1000. / delta_t_s;
                    //if (model_time_s < 0.001100 || model_time_s > 0.99999) {
                        //printf("TIME %lf  POWER CURRENT %lf termid %d  node %d\n", model_time_s, term->current, term->termid, i);
                    //}
                    //XXX
#endif
                    break;
                case COMP_RESISTOR:
                    term->current = (NODE_V_NEXT(n) - NODE_V_NEXT(other_n)) / c->resistor.ohms;
                    total_current += term->current;
                    break;
                case COMP_CAPACITOR:
                    term->current = ((NODE_V_NEXT(n) - NODE_V_CURR(n)) - (NODE_V_NEXT(other_n) - NODE_V_CURR(other_n)))
                                     * c->capacitor.farads / delta_t_s;
                    total_current += term->current;
#if 0
                    if (model_time_s > 0.99999) {
                        printf("time=%lf     %le  %le    farads=%lf  delta_t=%lf  current=%lf   termid %d  node %d\n",
                            model_time_s,
                            (NODE_V_NEXT(n) - NODE_V_CURR(n)),
                            (NODE_V_NEXT(other_n) - NODE_V_CURR(other_n)),
                            c->capacitor.farads, delta_t_s,
                            term->current, term->termid, i);
                    }
#endif
                    break;
                }
            }

            if (power_term) {
                power_term->current = -total_current;
            }
        }

        // xxx better comment
        // - update the current and prior node voltage
        node_v_prior_idx = (node_v_prior_idx + 1) % 3;
        node_v_curr_idx  = (node_v_curr_idx + 1) % 3;
        node_v_next_idx  = (node_v_next_idx + 1) % 3;

        // increment time
        model_time_s += delta_t_s;

        //usleep(100000);  // xxx temp
        if (model_time_s > 1) {  // xxx make this an option
            model_state_req = MODEL_STATE_PAUSED;
        }
    }
    return NULL;
}

#if 0
static double get_comp_power_voltage(component_t * c)
{
    double v;

    // xxx 
    //return c->power.volts;

// XXX try without this

    if (c->power.hz != 0) {
        // XXX FATAL("AC not supported yet\n");
    }

    // ramp up to dc voltage in 100 ms
    if (model_time_s > .1) {
        v = c->power.volts;
    } else {
        v = model_time_s / .1 * c->power.volts;
    }
    // xxx INFO("volts %f\n", v);

    return v;
}
#endif
