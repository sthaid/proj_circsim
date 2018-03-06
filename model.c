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

#define USAGE "\
usage: model <cmd> [<args>]\n\
\n\
cmd: reset             - resets to time 0 initial condition\n\
     run [<stop_t>]    - reset and run the model\n\
     stop              - stop the model\n\
     cont [<delta_t>] - continue from stop\n\
     step             - executes model for one time increment\n\
\n\
"

#define STOP_T   (param_num_val(PARAM_STOP_T))
#define DELTA_T  (param_num_val(PARAM_DELTA_T))
#define DCPWR_T  (param_num_val(PARAM_DCPWR_T))
#define SCOPE_T  (param_num_val(PARAM_SCOPE_T))

//
// typedefs
//

//
// variables
//

static int32_t  model_state_req;
static bool     model_step_req;

static node_t * ground_node;

//
// prototypes
//
  
static int32_t init_nodes(void);
static node_t * allocate_node(void);
static void add_terms_to_node(node_t *node, gridloc_t *gl);
static void debug_print_nodes(void);
static void reset(void);
static void * model_thread(void * cx);
static long double get_comp_power_voltage(component_t * c);

// -----------------  PUBLIC ---------------------------------------------------------

void model_init(void)
{
    pthread_t thread_id;

    // create the model_thread
    pthread_create(&thread_id, NULL, model_thread, NULL);
}

int32_t model_cmd(char *cmdline)
{
    int32_t rc;
    char *cmd, *arg, s[100];
    long double val;

    // extract cmd from cmdline
    cmd = strtok(cmdline, " ");
    if (cmd == NULL) {
        printf("%s\n", USAGE);
        return 0;
    }

    // parse and process the cmd
    if (strcmp(cmd, "reset") == 0) {
        rc = model_reset();
    } else if (strcmp(cmd, "run") == 0) {
        if ((arg = strtok(NULL, " ")) != NULL) {
            if (param_set(PARAM_STOP_T, arg) < 0) {
                ERROR("invalid time '%s'\n", arg);
                return -1;
            }
        }
        rc = model_run();
    } else if (strcmp(cmd, "stop") == 0) {
        rc = model_stop();
    } else if (strcmp(cmd, "cont") == 0) {
        if ((arg = strtok(NULL, " ")) != NULL) {
            if (str_to_val(arg, UNITS_SECONDS, &val) == -1) {
                ERROR("invalid delta time '%s'\n", arg);
                return -1;
            }
            param_set(PARAM_STOP_T, val_to_str(model_t+val,UNITS_SECONDS,s));
        }
        rc = model_cont();
    } else if (strcmp(cmd, "step") == 0) {
        rc = model_step();
    } else {
        ERROR("unsupported cmd '%s'\n", cmd);
        rc = -1;
    }

    return rc;
}

int32_t model_reset(void)
{
    // if already reset then just return
    if (model_state == MODEL_STATE_RESET) { 
        return 0;
    }

    // request the model_thread to idle
    SET_MODEL_REQ(MODEL_STATE_RESET);

    // reset model variables
    reset();

    // success
    return 0;
}

int32_t model_run(void)
{
    int32_t rc;

    // reset the model
    reset();

    // analyze the grid and components to create list of nodes;
    // if this fails then reset the model variables
    rc = init_nodes();
    if (rc < 0) {
        reset();
        return -1;
    }

    // set the model state to RUNNING
    SET_MODEL_REQ(MODEL_STATE_RUNNING);

    // success
    return 0;
}

int32_t model_stop(void)
{
    if (model_state != MODEL_STATE_RUNNING) {
        ERROR("not running\n");
        return -1;
    }

    SET_MODEL_REQ(MODEL_STATE_STOPPED);

    return 0;
}

int32_t model_cont(void)
{
    if (model_state != MODEL_STATE_STOPPED) {
        ERROR("not stopped\n");
        return -1;
    }

    SET_MODEL_REQ(MODEL_STATE_RUNNING);

    return 0;
}

int32_t model_step(void)
{
    if (model_state != MODEL_STATE_STOPPED && model_state != MODEL_STATE_RESET) {
        ERROR("model state is not stopped or reset\n");
        return -1;
    }

    model_step_req = true;

    if (model_state == MODEL_STATE_RESET) {
        model_run();
    } else {
        model_cont();
    }

    return 0;
}

// -----------------  MODEL SUPPORT  -------------------------------------------------

static int32_t init_nodes(void)
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
    if (n->max_gridloc >= n->max_alloced_gridloc) {
        n->max_alloced_gridloc = (n->max_gridloc == 0 ? 8 : n->max_gridloc * 2);
        DEBUG("%ld: MAX_ALLOCED_GRIDLOC IS NOW %d\n", n-node, n->max_alloced_gridloc);
        n->gridloc = realloc(n->gridloc, n->max_alloced_gridloc * sizeof(gridloc_t));
    }
    n->gridloc[n->max_gridloc++] = *gl;

    // add all terminals that are either direcctly connected to this gridloc or 
    // are on other gridlocs that are connected to this gridloc
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

            if (n->max_term >= n->max_alloced_term) {
                n->max_alloced_term = (n->max_term == 0 ? 8 : n->max_term * 2);
                DEBUG("%ld: MAX_ALLOCED_TERM IS NOW %d\n", n-node, n->max_alloced_term);
                n->term = realloc(n->term, n->max_alloced_term * sizeof(terminal_t));
            }
            n->term[n->max_term] = term;
            n->max_term++;

            term->node = n;

            if (term->component->type == COMP_POWER && term->termid == 0) {
                n->power = term;
            }
        }
    }

    // if this grid location is ground then set this node's ground flag
    if (g->ground) {
        n->ground = true;
    }

    // link the grid location to this node
    g->node = n;
}

static void debug_print_nodes(void)
{
    #define MAX_DEBUG_STR 1000
    char s[MAX_DEBUG_STR], *p;
    int32_t i, j;

    //return;
 
    INFO("max_node = %d\n", max_node);
    for (i = 0; i < max_node; i++) {
        node_t * n = &node[i];

        INFO("node %d - max_gridloc=%d  max_term=%d  ground=%d  power=%p %s\n", 
             i, n->max_gridloc, n->max_term, n->ground, n->power,
             n->power ? n->power->component->comp_str : "");
        
        p = s;
        for (j = 0; j < n->max_gridloc; j++) {
            char s1[100];
            p += sprintf(p, "%s ", gridloc_to_str(&n->gridloc[j],s1));
            if (p - s > MAX_DEBUG_STR - 100) {
                strcpy(p, " ...");
                break;
            }
        }
        INFO("   gridlocs %s\n", s);

        p = s;
        for (j = 0; j < n->max_term; j++) {
            p += sprintf(p, "copmid,term=%ld,%d ", 
                         n->term[j]->component - component,
                         n->term[j]->termid);
            if (p - s > MAX_DEBUG_STR - 100) {
                strcpy(p, " ...");
                break;
            }
        }
        INFO("   terms %s\n", s);
    }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - 

static void reset(void)
{
    int32_t i, glx, gly;

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

    for (glx = 0; glx < MAX_GRID_X; glx++) {
        for (gly = 0; gly < MAX_GRID_Y; gly++) {
            grid[glx][gly].node = NULL;
        }
    }

    model_t = 0;
    history_t = 0;
    max_history = 0;
    max_node = 0;
    model_step_req = false;

    for (i = 0; i < max_component; i++) {
        component_t *c = &component[i];
        if (c->type == COMP_INDUCTOR && c->inductor.i_init != 0) {
            c->i_next    = c->inductor.i_init;
            c->i_current = c->inductor.i_init;
        }
    }
}

// -----------------  MODEL THREAD  --------------------------------------------------

static void * model_thread(void * cx) 
{
    #define SET_HISTORY(h,v) \
        do { \
            if (idx == max_history) { \
                h[idx].min = h[idx].max = v; \
            } else { \
                if (v > h[idx].max) h[idx].max = v; \
                if (v < h[idx].min) h[idx].min = v; \
            } \
        } while (0)

    int32_t i,j,idx;
    long double delta_t;   // xxx  todo

    while (true) {
        // handle request to transition model_state
        if (model_state_req != model_state) {
            INFO("model_state is %s\n", MODEL_STATE_STR(model_state_req));
        }
        model_state = model_state_req;
        __sync_synchronize();

        // if scope time param has changed then reset scope history
        if (param_has_changed(PARAM_SCOPE_T)) {
            history_t = model_t;
            max_history = 0;
            __sync_synchronize();
        }

        // if model is not running then continue
        if (model_state != MODEL_STATE_RUNNING) {
            usleep(1000);
            continue;
        }

        // XXX try init of power prior too

        // XXX THIS NEEDS WORK
        // xxx can we start at time 0?
        if (model_t == 0) {
            model_t = 1e-10;
        }
        delta_t = .001 * (1 - expl(-model_t / 400));
        //delta_t = .0001 * (1 - expl(-model_t / 400));

        // increment time
        model_t += delta_t;

        // XXX how many steps to achieve 1ms model_t ?
        //static int64_t count;
        //if (count++ < 100) {
            //INFO("model_t = %Le  delta_t = %Le\n", model_t, delta_t);
        //}

        // loop over all nodes, computing the next voltage for that node
        for (i = 0; i < max_node; i++) {
            node_t * n = & node[i];

            if (n->ground) {
                n->v_next = 0;
            } else if (n->power) {
                n->v_next = get_comp_power_voltage(n->power->component);
            } else {
                long double r_sum_num=0, r_sum_denom=0;
                long double c_sum_num=0, c_sum_denom=0;
                long double l_sum_num=0, l_sum_denom=0;
                for (j = 0; j < n->max_term; j++) {
                    component_t *c = n->term[j]->component;
                    int32_t termid = n->term[j]->termid;
                    int32_t other_termid = (termid ^ 1);
                    node_t *other_n = c->term[other_termid].node;

                    switch (c->type) {
                    case COMP_RESISTOR:
                        // xxx use better estimate for other node
                        r_sum_num += (other_n->v_current / c->resistor.ohms);
                        r_sum_denom += (1 / c->resistor.ohms);
                        break;
                    case COMP_CAPACITOR:
                        c_sum_num += (n->v_current + other_n->v_current - other_n->v_prior) *
                                     (c->capacitor.farads / delta_t);
                        c_sum_denom += c->capacitor.farads / delta_t;
                        break;
                    case COMP_INDUCTOR:
                        l_sum_num += 
                            (delta_t / c->inductor.henrys) * (other_n->v_current + other_n->v_current - other_n->v_prior);
                        if (termid == 0) {
                            l_sum_num -= c->i_current;
                        } else {
                            l_sum_num += c->i_current;
                        }
                        l_sum_denom += delta_t / c->inductor.henrys;
                        break;
                    default:
                        FATAL("comp type %s not supported\n", c->type_str);
                    }
                }
                n->v_next = (r_sum_num + c_sum_num + l_sum_num) / 
                             (r_sum_denom + c_sum_denom + l_sum_denom);
            }
        }

        // comute the current through each component
        for (i = 0; i < max_component; i++) {
            component_t *c = &component[i];
            node_t *n0 = c->term[0].node;
            node_t *n1 = c->term[1].node;
            switch (c->type) {
            case COMP_RESISTOR:
                c->i_next = (n0->v_next - n1->v_next) / c->resistor.ohms;
                break;
            case COMP_CAPACITOR:
                c->i_next = ((n0->v_next - n1->v_next) - (n0->v_current - n1->v_current)) *
                            (c->capacitor.farads / delta_t);
                break;
            case COMP_INDUCTOR: {
                long double v0 = (n0->v_next + n0->v_current) / 2.;
                long double v1 = (n1->v_next + n1->v_current) / 2.;
                c->i_next = c->i_current + (delta_t / c->inductor.henrys) * (v0 - v1);
                break; }
            }
        }

        // compute the power supply current
        for (i = 0; i < max_node; i++) {
            node_t * n = & node[i];
            if (n->power == NULL) {
                continue;
            }
            long double total_current = 0;
            for (j = 0; j < n->max_term; j++) {
                if (n->term[j] == n->power) {
                    continue;
                }
                if (n->term[j]->termid == 0) {
                    total_current += n->term[j]->component->i_next;
                } else {
                    total_current -= n->term[j]->component->i_next;
                }
            }
            n->power->component->i_next = -total_current;
        }

        // rotate next -> current -> prior
        for (i = 0; i < max_node; i++) {
            node_t * n = &node[i];
            n->v_prior = n->v_current;
            n->v_current = n->v_next;
        }
        for (i = 0; i < max_component; i++) {
            component_t *c = &component[i];
            c->i_current = c->i_next;
        }

        // keep track of voltage and current history, 
        // these are used for the scope display
        idx = (model_t - history_t) / SCOPE_T * MAX_HISTORY;
        if (idx < MAX_HISTORY) {
            for (i = 0; i < max_component; i++) {
                component_t *c = &component[i];
                if (c->type != COMP_NONE && c->type != COMP_WIRE) {
                    SET_HISTORY(c->i_history, c->i_next);
                }
            }
            for (i = 0; i < max_node; i++) {
                node_t *n = &node[i];
                SET_HISTORY(n->v_history, n->v_next);
            }
            __sync_synchronize();
            max_history = idx + 1;
            __sync_synchronize();
        }

        // if model has reached the stop time, or is being single stepped
        // then stop the model
        if (model_t >= STOP_T || model_step_req) {                
            model_state_req = MODEL_STATE_STOPPED;   
            model_step_req = false;
        }
    }
    return NULL;
}

static long double get_comp_power_voltage(component_t * c)
{
    long double v;

    if (c->power.hz == 0) {
        // dc
        if (model_t >= DCPWR_T) {
            v = c->power.volts;
        } else {
            v = c->power.volts * sinl((model_t / DCPWR_T) * M_PI_2);
        }
    } else {
        // ac, c->power.volts is RMS
        v = c->power.volts * M_SQRT2 * sinl(model_t * c->power.hz * (2. * M_PI));
    }

    return v;
}
