/*
Copyright (c) 2018 Steven Haid

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

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

#define MAX_DIODE_OHMS 1e8L
#define MIN_DIODE_OHMS .1L


//
// typedefs
//

//
// variables
//

static int32_t     model_state_req;
static int32_t     model_step_count;
static long double auto_delta_t;

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
    int32_t rc, i;
    long double largest_hz;

    // reset the model
    reset();

    // analyze the grid and components to create list of nodes;
    // if this fails then reset the model variables
    rc = init_nodes();
    if (rc < 0) {
        reset();
        return -1;
    }

    // auto_delta_t will be used by the circuit sim model if the delta_t param is 
    // set to 0;  the following code calculates auto_delta_t by taking
    // into account the frequency of AC power supplies, and the scope_span_t
    //
    // - if there are no power supplies then auto_delta_t is set to 0;
    //   this means that the user must set the delta_t param
    // - if all power supplies are DC then auto_delta_t is set to 1ms
    // - otherwise the highest frequency power supply is used to determine
    //   auto_delta_t by multiplying the period by .001; for example if the
    //   highest frequency power supply is 60HZ then the period is .017ms and
    //   the auto_delta_t will be set to .000017ms
    //
    // - a final check is made to compare the calculated auto_delta_t with
    //   the scope_interval (the scope interval equalling scope_span_t/MAX_HISTORY);
    //   if auto_delta_t is greater than scope_interval then auto_delta_t is
    //   set equal to scope_interval
    largest_hz = -1;
    for (i = 0; i < max_component; i++) {
        component_t * c = &component[i];
        if (c->type != COMP_POWER) {
            continue;
        }
        if (c->power.hz > largest_hz) {
            largest_hz = c->power.hz;
        }
    }
    auto_delta_t = (largest_hz == -1 ? 0    :
                    largest_hz == 0  ? 1e-3 :
                                       1 / largest_hz * .001);
    if (auto_delta_t && auto_delta_t > param_num_val(PARAM_SCOPE_SPAN_T) / MAX_HISTORY) {
        auto_delta_t = param_num_val(PARAM_SCOPE_SPAN_T) / MAX_HISTORY;
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

    return;
 
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
        if (c->type == COMP_NONE) {
            continue;
        }

        c->term[0].node = NULL;
        c->term[1].node = NULL;

        c->i_next = 0;
        c->i_now = 0;
        c->diode_ohms = MAX_DIODE_OHMS;
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
    delta_t = 0;
    max_history = 0;
    max_node = 0;
    failed_to_stabilize_count = 0;

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
            int32_t i; \
            if (idx >= max_history) { \
                for (i = max_history; i < idx; i++) { \
                    h[i].min = h[i].max = NAN; \
                } \
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

        // determine delta_t value
        delta_t = param_num_val(PARAM_DELTA_T);
        if (delta_t == 0) {
            delta_t = auto_delta_t;
            if (delta_t == 0) {
                ERROR("param delta_t must be specified\n");
                model_state_req = MODEL_STATE_STOPPED;   
                continue;
            }
        }

        // evaluate the circuit to determine the circuit values after
        // the circuit evolves for delta_t interval
        eval_circuit_for_delta_t();

        // keep track of voltage and current history, 
        // these are used for the scope display
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

    // This routine will evaluate the circuit for a single time increment (delta_t).
    // 
    // The 'next' node voltage values, and 'next' node current values are calculated
    // for each node and component. And then circuit_is_stable is called to evaluate
    // if each node's current (based on the 'next' current values) is near zero; which
    // means the circuit is stable. If the circuit is not stable then repeat until 
    // stability is achieved.
    //
    // The reason for this iterative process is that to evaluate a node the state
    // of the adjacent nodes are used as input. The adjacent nodes are being
    // evaluated in a similar manner, thus there is a circular dependency. This iterative
    // process usually converges for the circuits that I've tested. A problem, however, is that
    // in some circuits many iterations are needed which cause long execution times.

    // iterate evaluating the circuit until circuit_is_stable returns true
    while (true) {
        // loop over all nodes, computing the next voltage for that node,
        // based on the components attached to the node and the states of 
        // the nodes on the other side of the components
        //
        // Kirchhoff's current law is used, along with ohms law and the 
        // current-voltage relationships for capacitors and inductors.
        //
        // Diodes are treated as resistors, except that the resistance varies
        // as a function of the voltage across the diode. The model uses this
        // equation for diode resistance: 
        //      diode_ohms = exp(50 * (.7 - V))
        // See development/diode_ohms.png for a graph of this equation.
        //
        // A simple example is a 2 resistor circuit:
        //
        //     5v---R1ohm---Vx---R2ohm---14v
        //
        // where we need to solve for Vx based on the current state of the
        // other 2 nodes (5v and 14v). Using ohms law and Kichoff's current law:
        //
        //     Vx - 5     Vx - 14
        //     ------  +  ------- = 0
        //       1           2
        //
        // Re-arranging to solve for Vx:
        //             1     1       5     14
        //      Vx * (--- + ---) - (--- + ----) = 0
        //             1     2       1     2
        //
        //      Vx = 12 / 1.5 = 8
        // 
        // BTW this is a nice description of capacitors and inductors.
        // https://ocw.mit.edu/courses/electrical-engineering-and-computer-science/6-071j-introduction-to-electronics-signals-and-measurement-spring-2006/lecture-notes/capactr_inductr.pdf
        //
        for (i = 0; i < max_node; i++) {
            node_t * n = & node[i];

            if (n->ground) {
                n->v_next = 0;
            } else if (n->power) {
                n->v_next = get_comp_power_voltage(n->power->component);
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
                long double dv = n0->v_next - n1->v_next;
                long double ohms;
                ohms = expl(50.L * (.7L - dv));
                if (ohms > MAX_DIODE_OHMS) ohms = MAX_DIODE_OHMS;
                if (ohms < MIN_DIODE_OHMS) ohms = MIN_DIODE_OHMS;
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

        // check if the circuit is stable, meaning that for each node the sum of currents 
        // is close to zero; and if stable break out of the loop 
        count++;
        if (circuit_is_stable(count)) {
            break;
        }
    }

    // completed evaluating the circuit's progression for thise delta_t interval;
    // move the 'next' values to 'now' values
    for (i = 0; i < max_node; i++) {
        node_t * n = &node[i];
        n->v_now = n->v_next;
    }
    for (i = 0; i < max_component; i++) {
        component_t *c = &component[i];
        c->i_now = c->i_next;
    }

    // compute component power dissipation (watts)
    // - reverse the sign for power supply power, so it is positive too
    // - use timed_moving_average routine which averages the 'watts' arg value
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

static bool circuit_is_stable(int32_t count)
{
    #define MAX_COUNT 100000

    int32_t i, j;
    long double sum_i, sum_abs_i, i_term, fraction, max_fraction;

    // loop over all nodes and for each node check that the sum of 
    // the currents is close to zero; if all nodes have currents sums
    // close to zero then the circuit is stable
    for (i = 0; i < max_node; i++) {
        node_t * n = &node[i];

        // don't check the power and ground nodes
        if (n->power || n->ground) {
            continue;
        }

        // create sum of current and abs(current); summing the current from
        // each of the node's terminals
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

        // the fraction of the sum of currents divided by the sum of abs(current) 
        // is used to check if the node's current is close to 0; for example suppose
        // there are 3 terminals with currents of 1A, 2A, and -2.999 then
        //   sum_i = .001
        //   sum_abs_i = 5.999
        //   fraction = .001/5.999 = .000166
        fraction = sum_i / sum_abs_i;

        // determine max_fraction; if the node's fraction value is above 
        // max_fraction the circuit is considered not-stable
        if (sum_abs_i < 0.00001) {
            max_fraction = .10;
        } else if (sum_abs_i < 0.0001) {
            max_fraction = .01;
        } else {
            max_fraction = .001;
        }

        // check if this node has near to zero current, if not then return false;
        //
        // If too many attempts have been made then increment the failed_to_stabilize_count
        // and return true. The failed_to_stabilize_count is displayed in red on the 
        // display status pane if it is non zero. When failed to stabilize occurs this means
        // the model's results are in doubt; the results may be okay, partially okay, or
        // totally wrong. Some things that can be done to correct;
        // - verify the circuit is properly entered and is a reasonable circuit
        // - increase MAX_COUNT
        // - set the delta_t param to a smaller value than the auto_delta_t 
        //   that was used
        if (fraction > max_fraction) {
            if (count == MAX_COUNT) {
#ifdef ENABLE_LOGGING_AT_DEBUG_LEVEL
                char s1[100];
                DEBUG("failed to stabilize: count=%d gl=%s voltage=%Lf sum_i=%Lf "
                      "sum_abs_i=%.12Lf frac=%Lf max_frac=%Lf\n",
                      count, gridloc_to_str(&n->gridloc[0],s1), n->v_next, sum_i, 
                      sum_abs_i, fraction, max_fraction);
#endif
                failed_to_stabilize_count++;
                return true;
            }
            return false;
        }
    }

    // the circuit is stable
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
    } else if (c->power.wave_form == WAVE_FORM_SINE) {
        v = c->power.volts * sinl(model_t * c->power.hz * (2. * M_PI));
    } else if (c->power.wave_form == WAVE_FORM_SQUARE) {
        long double cycles = model_t * c->power.hz;
        cycles -= floor(cycles);
#if 0
        v = (cycles < 0.5 ? c->power.volts : -c->power.volts);
#else
        #define RISE_TIME .01
        long double slope = 2 * c->power.volts / RISE_TIME;

        if (cycles < RISE_TIME) {
            v = -c->power.volts + slope * cycles;
        } else if (cycles < 0.5) {
            v = c->power.volts;
        } else if (cycles < 0.5+RISE_TIME) {
            v = c->power.volts - slope * (cycles - 0.5);
        } else {
            v = -c->power.volts;
        }
#endif
    } else if (c->power.wave_form == WAVE_FORM_TRIANGLE) {
        FATAL("triangle wave form not supported yet\n");
        v = 0;
    } else {
        assert(0);
    }

    return v;
}

