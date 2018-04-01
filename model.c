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

#define DCPWR_RAMP_T 0.25e-3    // 0.25ms

//
// typedefs
//

//
// variables
//

static int32_t     model_state_req;
static int32_t     model_step_count;

static int32_t largest_stable_count;  // xxx temp

//
// prototypes
//
  
static int32_t init_nodes(void);
static node_t * allocate_node(void);
static void add_terms_to_node(node_t *node, gridloc_t *gl);
static void debug_print_nodes(void);
static void reset(void);
static void * model_thread(void * cx);
static void eval_circuit_for_delta_t(void);
static bool circuit_is_stable(int32_t count);
static long double get_comp_power_voltage(component_t * c);

// -----------------  PUBLIC ---------------------------------------------------------

void model_init(void)
{
    pthread_t thread_id;

    // create the model_thread
    pthread_create(&thread_id, NULL, model_thread, NULL);
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

    // set model stop time
    stop_t = param_num_val(PARAM_RUN_T);

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
    if (model_state == MODEL_STATE_RESET) {
        model_run();
    } else if (model_state == MODEL_STATE_STOPPED) {
        if (param_has_changed(PARAM_RUN_T) || model_t >= stop_t) {
            stop_t = model_t + param_num_val(PARAM_RUN_T);
        }
        SET_MODEL_REQ(MODEL_STATE_RUNNING);
    } else {
        ERROR("not stopped or reset\n");
        return -1;
    }

    return 0;
}

int32_t model_step(void)
{
    if (model_state != MODEL_STATE_STOPPED && model_state != MODEL_STATE_RESET) {
        ERROR("model state is not stopped or reset\n");
        return -1;
    }

    model_step_count = param_num_val(PARAM_STEP_COUNT);
    if (model_state == MODEL_STATE_RESET) {
        model_run();
    } else {
        model_cont();
    }

    return 0;
}

// -----------------  NODE INITIALIZATION  -------------------------------------------

static int32_t init_nodes(void)
{
    int32_t i, j, ground_node_count, power_count;
    node_t * ground_node = NULL;

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

    memset(&n->start_init_node_state, 
           0,
           sizeof(node_t) - offsetof(node_t,start_init_node_state));

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

// -----------------  RESET  ---------------------------------------------------------

static void reset(void)
{
    int32_t i, glx, gly;

    SET_MODEL_REQ(MODEL_STATE_RESET);

    for (i = 0; i < max_node; i++) {
        node_t *n = &node[i];
        memset(&n->start_init_node_state, 
               0,
               sizeof(node_t) - offsetof(node_t,start_init_node_state));
    }

    for (i = 0; i < max_component; i++) {
        component_t *c = &component[i];
        c->term[0].node = NULL;
        c->term[1].node = NULL;

        c->i_next = 0;
        c->i_now = 0;
        c->diode_ohms = 1e8;  // xxx #define
        memset(c->i_history,0,sizeof(c->i_history));
        timed_moving_average_reset(c->watts);
    }

    for (glx = 0; glx < MAX_GRID_X; glx++) {
        for (gly = 0; gly < MAX_GRID_Y; gly++) {
            grid[glx][gly].node = NULL;
        }
    }

    model_t = 0;
    history_t = 0;
    stop_t = 0;
    max_history = 0;
    max_node = 0;
    largest_stable_count = 0; // xxx temp

    for (i = 0; i < max_component; i++) {
        component_t *c = &component[i];
        if (c->type == COMP_INDUCTOR && c->inductor.i_init != 0) {
            c->i_next = c->inductor.i_init;
            c->i_now  = c->inductor.i_init;
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

    uint64_t i,idx;
    long double last_scope_span_t = -1;

    while (true) {
        // handle request to transition model_state
        if (model_state_req != model_state) {
            INFO("model_state is %s\n", MODEL_STATE_STR(model_state_req));
        }
        model_state = model_state_req;
        __sync_synchronize();

        // if scope time span param has changed or scope trigger is requested 
        // then reset scope history
        if (param_num_val(PARAM_SCOPE_SPAN_T) != last_scope_span_t) {
            last_scope_span_t = param_num_val(PARAM_SCOPE_SPAN_T);
            history_t = model_t;
            max_history = 0;
            __sync_synchronize();
        }
        if (param_num_val(PARAM_SCOPE_TRIGGER) == 1) {
            param_set(PARAM_SCOPE_TRIGGER, "0");
            history_t = model_t;
            max_history = 0;
            __sync_synchronize();
        }

        // if model is not running then continue
        if (model_state != MODEL_STATE_RUNNING) {
            usleep(1000);
            continue;
        }

        // determine delta_t value, using small delta_t when the model is starting
        delta_t = param_num_val(PARAM_DELTA_T);

        // evaluate the circuit to determine the circuit values after
        // the circuit evolves for delta_t interval
        eval_circuit_for_delta_t();

        // keep track of voltage and current history, 
        // these are used for the scope display
        // xxx if values have been skipped then fill them in somehow, maybe with INVALID
        // xxx maybe don't need the min and max any more
        idx = (model_t - history_t) / param_num_val(PARAM_SCOPE_SPAN_T) * MAX_HISTORY;
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
        } else if (strcasecmp(param_str_val(PARAM_SCOPE_MODE), "continuous") == 0) {
            history_t = model_t;
            max_history = 0;
            __sync_synchronize();
        }

        // increment time
        model_t += delta_t;

        // if model has reached the stop time, or 
        // has reached single step count then stop the model
        if (model_t >= stop_t && model_step_count == 0) {
            model_state_req = MODEL_STATE_STOPPED;   
        }
        if (model_step_count > 0) {
            if (--model_step_count == 0) {
                model_state_req = MODEL_STATE_STOPPED;   
            }
        }
    }
    return NULL;
}

static void eval_circuit_for_delta_t(void)
{
    uint64_t i, j, count=0;

    // xxx comments throughout
    while (true) {
        // loop over all nodes, computing the next voltage for that node
        for (i = 0; i < max_node; i++) {
            node_t * n = & node[i];

            if (n->ground) {
                n->v_next = 0;
            } else if (n->power) {
                n->v_next = get_comp_power_voltage(n->power->component);
                // xxx n->v_now = n->v_next;
            } else {
                long double sum_num=0, sum_denom=0;

                for (j = 0; j < n->max_term; j++) {
                    component_t *c = n->term[j]->component;
                    int32_t termid = n->term[j]->termid;
                    int32_t other_termid = (termid ^ 1);
                    node_t *other_n = c->term[other_termid].node;

                    switch (c->type) {
                    case COMP_RESISTOR:
                        sum_num += (other_n->v_next / c->resistor.ohms);
                        sum_denom += (1 / c->resistor.ohms);
                        break;
                    case COMP_CAPACITOR:
                        sum_num += (c->capacitor.farads / delta_t) * n->v_now +
                                   c->capacitor.farads * other_n->dv_dt;
                        sum_denom += c->capacitor.farads / delta_t;
                        break;
                    case COMP_INDUCTOR:
                        sum_num += (delta_t / c->inductor.henrys) * other_n->v_next;
                        if (termid == 0) {
                            sum_num -= c->i_now;
                        } else {
                            sum_num += c->i_now;
                        }
                        sum_denom += delta_t / c->inductor.henrys;
                        break;
                    case COMP_DIODE: {
                        sum_num += (other_n->v_next / c->diode_ohms);
                        sum_denom += (1 / c->diode_ohms);
                        break; }
                    default:
                        FATAL("comp type %s not supported\n", c->type_str);
                    }
                }
                n->v_next = sum_num / sum_denom;
                //INFO("count %ld  node %ld sum_num %Le sum_denom %Le  voltage %Le\n",
                     //count, i, sum_num, sum_denom, n->v_next);
            }
        }

        // compute the current through each component;
        // also, for COMP_DIODE, compute the equivalent resistance, c->diode_ohms
        for (i = 0; i < max_component; i++) {
            component_t *c = &component[i];
            node_t *n0 = c->term[0].node;
            node_t *n1 = c->term[1].node;
            switch (c->type) {
            case COMP_RESISTOR:
                c->i_next = (n0->v_next - n1->v_next) / c->resistor.ohms;
                break;
            case COMP_CAPACITOR:
                c->i_next = ((n0->v_next - n1->v_next) - (n0->v_now - n1->v_now)) *
                            (c->capacitor.farads / delta_t);
                break;
            case COMP_INDUCTOR: {
                long double dv = n0->v_next - n1->v_next;
                c->i_next = c->i_now + (delta_t / c->inductor.henrys) * dv;
                break; }
            case COMP_DIODE: {
#if 0
                // xxx cleanup, remove this ifdefed out code
                long double v0 = (n0->v_next + n0->v_now) / 2.;
                long double v1 = (n1->v_next + n1->v_now) / 2.;
                long double dv = (v0 - v1);
                long double ohms;
#else
                long double dv = n0->v_next - n1->v_next;
                long double ohms;
#endif
                ohms = expl(50.L * (.7L - dv));
                if (ohms > 1e8) ohms = 1e8;
                if (ohms < .10) ohms = .10;
                basic_exponential_smoothing(ohms, &c->diode_ohms, 0.01);
                c->i_next = dv / ohms;
                break; }
            }
        }

        // compute the power supply component current
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

        // compute dv_dt for all nodes
        for (i = 0; i < max_node; i++) {
            node_t * n = &node[i];
            n->dv_dt = (n->v_next - n->v_now) / delta_t;
        }

#if 0
        // XXX check if stable
        // rgrid        50000
        // ps1          10000
        // RC7          >100000000   100M-1ohm-100M 
        // most diodes  1000
        // lc1          100
        // rc1          10
        // rc5          100  unstable at begining
        count++;
        if (count == 500000) {
            break;
        }
#else
        // if the circuit is stable, meaning that for each node the sum of currents 
        // is close to zero; and if stable break out of the loop 
        count++;
        if (circuit_is_stable(count)) {
            break;
        }
#endif
    }

    // completed evaluating the circuit's progression for the delta_t interval;
    // move next values to now values
    for (i = 0; i < max_node; i++) {
        node_t * n = &node[i];
        n->v_now = n->v_next;
    }
    for (i = 0; i < max_component; i++) {
        component_t *c = &component[i];
        c->i_now = c->i_next;
    }

    // compute component power dissipation (watts)
    // - reverse sign for power supply power, so it is positive too
    // - used timed_moving_average routine which averages the 'watts' arg value
    //   over a 1 second interval
    for (i = 0; i < max_component; i++) {
        component_t *c = &component[i];
        long double watts;

        if (c->type != COMP_RESISTOR &&
            c->type != COMP_CAPACITOR &&
            c->type != COMP_INDUCTOR &&
            c->type != COMP_DIODE &&
            c->type != COMP_POWER)
        {
            continue;
        }

        watts = (c->term[0].node->v_now - c->term[1].node->v_now) * c->i_now;
        if (c->type == COMP_POWER) {
            watts = -watts;
        }
        timed_moving_average(watts, model_t, c->watts);
    }
}

// xxx might not want to pass count in
static bool circuit_is_stable(int32_t count)
{
    #define MAX_COUNT 1000000

    int32_t i, j;
    long double sum_i, sum_abs_i, i_term, fraction, max_fraction;
    char s1[100];

#if 1
    // xxx is this needed
    if (count < 10) {
        //INFO("returning because count too small, %d\n", count);
        return false;
    }
#endif

    for (i = 0; i < max_node; i++) {
        node_t * n = &node[i];

        if (n->power || n->ground) {
            continue;
        }

        sum_i = sum_abs_i = 0;
        for (j = 0; j < n->max_term; j++) {
            if (n->term[j]->termid == 0) {
                i_term = n->term[j]->component->i_next;
            } else {
                i_term = -n->term[j]->component->i_next;
            }
            sum_i += i_term;
            sum_abs_i += fabsl(i_term);
        }
        sum_i = fabsl(sum_i);

        fraction = sum_i / sum_abs_i;

        // xxx make an equation
        // xxx not working well:
        //   - rc5
#if 0
        max_fraction = (sum_abs_i < .0001 ? .30  :
                        sum_abs_i < .001  ? .15  :
                        sum_abs_i < .010  ? .01  :
                        sum_abs_i < .050  ? .005 :
                                            .001);
#else
        max_fraction = .001;
#endif

        if (fraction > max_fraction) {
            if (count == MAX_COUNT) {
                ERROR("failed to stabilize: count=%d gl=%s voltage=%Lf sum_i=%Lf sum_abs_i=%Lf frac=%Lf max_frac=%Lf\n",
                      count, gridloc_to_str(&n->gridloc[0],s1), n->v_next, sum_i, sum_abs_i, fraction, max_fraction);
                return true;
            }
            return false;
        }
    }

    if (count > largest_stable_count) {
        INFO("largest_stable_count %d\n", count);
        // xxx also print avg stable count, and maybe work that back into the max_fraction
        largest_stable_count = count;
    }

    return true;
}

static long double get_comp_power_voltage(component_t * c)
{
    long double v;

    if (c->power.hz == 0) {
        // dc 
        if (strcasecmp(param_str_val(PARAM_DCPWR_RAMP), "on") == 0) {
            if (model_t >= DCPWR_RAMP_T) {
                v = c->power.volts;
            } else {
                v = c->power.volts * model_t / DCPWR_RAMP_T;
            }
        } else {
            v = c->power.volts;
        }
    } else {
        // ac
        v = c->power.volts * sinl(model_t * c->power.hz * (2. * M_PI));
    }

    return v;
}

