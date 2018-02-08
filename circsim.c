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

//
// prototypes
//

static void identify_grid_ground(gridloc_t *gl);
static node_t * allocate_node(void);
static void add_terms_to_node(node_t *node, gridloc_t *gl);
static void debug_print_nodes(void);

// -----------------  CIRCSIM PREP  ---------------------------------------

int32_t cs_prep(void)
{
    int32_t i, j, x, y, component_count=0, ground_node_count=0;
    grid_t * g;
    gridloc_t ground_lclvar;
    
    // initialize
    memset(grid,0,sizeof(grid));    // xxx better way to clear grid ?
    max_node = 0;
    for (i = 0; i < max_component; i++) {
        for (j = 0; j < 2; j++) {
            component[i].term[j].node = NULL;
        }
    }

    // loop over components: 
    // - add them to the grid
    for (i = 0; i < max_component; i++) {
        component_t * c = &component[i];
        if (c->type == COMP_NONE) {
            continue;
        }
        if (c->type != COMP_CONNECTION) {
            component_count++;
        }
        for (j = 0; j < 2; j++) {
            x = c->term[j].gridloc.x;
            y = c->term[j].gridloc.y;
            g = &grid[x][y];
            if (g->max_term == MAX_GRID_TERM) {
                ERROR("all terminals used on gridloc %s\n", make_gridloc_str(&c->term[j].gridloc));
                return -1;
            }
            g->term[g->max_term++] = &c->term[j];
        }
    }
    INFO("component_count = %d\n", component_count);

    // if no components then return
    if (component_count == 0) {
        ERROR("no components\n");
        return -1;
    }

    // if ground is not specified then pick a ground
    // - the second terminal on a power supply, if one exists OR
    // - first terminal of the first component
    if (!ground_is_set) {
        component_t *first_component = NULL;
        component_t *first_power_component = NULL;
        for (i = 0; i < max_component; i++) {
            component_t * c = &component[i];
            if (c->type == COMP_NONE || c->type == COMP_CONNECTION) {
                continue;
            }
            if (first_component == NULL) {
                first_component = c;
            }
            if (first_power_component == NULL && c->type == COMP_DCPOWER) {
                first_power_component = c;
                break;
            }
        }
        if (first_power_component) {
            ground_lclvar = first_power_component->term[1].gridloc;
        } else if (first_component) {
            ground_lclvar = first_component->term[0].gridloc;
        } else {
            FATAL("failed to pick a ground\n");
        }
    } else {
        ground_lclvar = ground;
    }

    // identify all grid locations that are ground
    identify_grid_ground(&ground_lclvar);

    // create a list of nodes, eliminating the connection component;
    // so that each node provides a list of connected real components
    // such as resistors, capacitors, diodes etc.
    //
    // loop over all components 
    //   if the component is a CONNECTION then continue
    //   loop over the component's terminals
    //     if the terminal is already part of a node then continue
    //     allocate a new node
    //     call add_terms_to_node to add all terms on this gridloc, and
    //      connected to this gridloc to the node
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

    // validity check that just exactly one node is a ground node
    for (i = 0; i < max_node; i++) {
        if (node[i].ground) {
            ground_node_count++;
        }
    }
    if (ground_node_count != 1) {
        ERROR("ground_node_count=%d, should be 1\n", ground_node_count);
        return -1;
    }

    // return success
    return 0;
}

static void identify_grid_ground(gridloc_t *gl)
{
    grid_t *g = &grid[gl->x][gl->y];
    int32_t i;

    g->ground = true;
    for (i = 0; i < g->max_term; i++) {
        component_t * c = g->term[i]->component;
        if (c->type == COMP_CONNECTION) {
            int32_t      other_term_id = (g->term[i]->id ^ 1);
            terminal_t * other_term = &c->term[other_term_id];
            gridloc_t  * other_gl = &other_term->gridloc;
            grid_t     * other_g = &grid[other_gl->x][other_gl->y];
            if (other_g->ground == false)  {
                identify_grid_ground(other_gl);
            }
        }
    }
}

static node_t * allocate_node(void)
{
    node_t * n;

    n = &node[max_node++];
    n->ground = false;
    n->max_term = 0;
    if (n->max_alloced_term == 0) {
        n->term = malloc(100*sizeof(terminal_t));
        n->max_alloced_term = 100;
    }
    n->max_gridloc = 0;
    if (n->max_alloced_gridloc == 0) {
        n->gridloc = malloc(100*sizeof(gridloc_t));
        n->max_alloced_gridloc = 100;
    }
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
    n->gridloc[n->max_gridloc++] = *gl;

    // add all terminals that are either direcctly connected
    // to this gridloc or are on other gridlocs that are connected
    // to this gridloc
    g = &grid[gl->x][gl->y];
    for (i = 0; i < g->max_term; i++) {
        terminal_t * term = g->term[i];
        component_t * c = term->component;
        if (c->type == COMP_CONNECTION) {
            int32_t other_term_id = (term->id ^ 1);
            terminal_t * other_term = &c->term[other_term_id];
            add_terms_to_node(n, &other_term->gridloc);
        } else {
            assert(term->node == NULL);
            n->term[n->max_term++] = term;
            term->node = n;
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
 
    INFO("max_node = %d\n", max_node);
    for (i = 0; i < max_node; i++) {
        node_t * n = &node[i];

        INFO("node %d - max_gridloc=%d  max_term=%d  ground=%d\n", 
             i, n->max_gridloc, n->max_term, n->ground);
        
        p = s;
        for (j = 0; j < n->max_gridloc; j++) {
            p += sprintf(p, "%s ", make_gridloc_str(&n->gridloc[j]));
        }
        INFO("   gridlocs %s\n", s);

        p = s;
        for (j = 0; j < n->max_term; j++) {
            p += sprintf(p, "copmid,term=%ld,%d ", 
                    n->term[j]->component - component,
                    n->term[j]->id);
        }
        INFO("   terms %s\n", s);
    }
}

// ------------------------------------------------------------------------



#if 0
components    loc0,loc1
- resistor
- dcpower
- connection

wire junctions   loc

wires   locstart, locend

-------------

cs_run()
{
    resets
    run = true;
}

cs_pause()
{
    run = false;
}

cs_cont()
{
    run = true;
}

// xxx start with a single threaded approach

sim_thread()
{
    time = 0;

    while (true) {
        // release the work threads, these threads will
        // each grab a node and determine a new voltage for that node

        // wait for the work threads to complete

        // commit the new voltages, and save voltage history

        // update time
        time += delta_time;
    }
}

work_thread()
{
    while (true) {
        // wait to start

        while (true) {
            // get a node to process, if none left then done

            // analyze the node

        }

        // indicate done
    }
}

#endif
