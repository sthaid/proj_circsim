#ifndef __COMMON_H__
#define __COMMON_H__

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stddef.h>
#include <unistd.h>
#include <string.h>
#include <errno.h>
#include <time.h>
#include <limits.h>
#include <assert.h>
#include <pthread.h>
#include <math.h>
#include <readline/readline.h>
#include <readline/history.h>

#include "util_sdl.h"
#include "util_sdl_predefined_panes.h"
#include "util_misc.h"

//
// defines
//

#define MAX_GRID_X          52
#define MAX_GRID_Y          52
#define MAX_COMPONENT       10000
#define MAX_NODE            10000
#define MAX_GRID_TERM       5

#define CIRCSIM_STATE_RESET    0
#define CIRCSIM_STATE_RUNNING  1
#define CIRCSIM_STATE_PAUSED   2

#define CIRCSIM_STATE_STR(x) \
    ((x) == CIRCSIM_STATE_RESET    ? "RESET"   : \
     (x) == CIRCSIM_STATE_RUNNING  ? "RUNNING" : \
     (x) == CIRCSIM_STATE_PAUSED   ? "PAUSED"  : \
                                     "????")

#define NODE_V_PRIOR(n) ((n)->voltage[node_v_prior_idx])
#define NODE_V_CURR(n)  ((n)->voltage[node_v_curr_idx])
#define NODE_V_NEXT(n)  ((n)->voltage[node_v_next_idx])

#define COMP_NONE           0
#define COMP_CONNECTION     1
#define COMP_POWER          2
#define COMP_RESISTOR       3
#define COMP_CAPACITOR      4
#define COMP_INDUCTOR       5
#define COMP_DIODE          6
#define COMP_LAST           6

//
// typedefs
//

struct component_s;
struct node_s;

typedef struct gridloc_s {
    int32_t x;
    int32_t y;
} gridloc_t;

typedef struct terminal_s {
    struct component_s * component;
    struct node_s * node;
    int32_t termid;
    gridloc_t gridloc;
} terminal_t;

typedef struct component_s {
    // these fields describe the component
    int32_t type;
    char * type_str;
    int32_t compid;
    terminal_t term[2];
    union {
        struct {
            float volts;
            float hz;  // 0 = DC
        } power;
        struct {
            float ohms;
        } resistor;
        struct {
            float farads;
        } capacitor;
        struct {
            float henrys;
        } inductor;
    };
    // component state, used by circsim.c, follow:
    // when clearing component state, zero from here
    int32_t zero_init_component_state;
    int32_t xxx_tbd;
} component_t;

typedef struct grid_s {
    terminal_t * term[MAX_GRID_TERM];
    int32_t max_term;
    bool ground;
} grid_t;

typedef struct node_s {
    terminal_t ** term;
    gridloc_t * gridloc;
    int32_t max_alloced_term;
    int32_t max_alloced_gridloc;
    // when clearing a node, zero from here
    int32_t zero_init_node_state;
    int32_t max_term;
    int32_t max_gridloc;
    bool ground;
    terminal_t * power;
    float voltage[3];   // circular buffer of: prior, curr, new 
} node_t;

//
// variables
//

component_t component[MAX_COMPONENT];
int32_t     max_component;

gridloc_t   ground;
bool        ground_is_set;

grid_t      grid[MAX_GRID_X][MAX_GRID_Y];

node_t      node[MAX_NODE];
int32_t     max_node;
int32_t     node_v_prior_idx; //xxx should these be here?
int32_t     node_v_curr_idx;
int32_t     node_v_next_idx;

float       circsim_time;
int32_t     circsim_state;

//
// parameters
// 

#define PARAM_GRID       (params_tbl[0].value)
#define PARAM_DELTA_T_US (params_tbl[1].value)

typedef struct {
    char    *name;
    char    *units;
    int32_t  value;
} params_tbl_entry_t;

#ifdef MAIN
params_tbl_entry_t params_tbl[] = { 
        { "grid",    "bool",  true },
        { "delta_t", "us",    1000 },
        { NULL,      NULL,    0    }
                                    };
#else
params_tbl_entry_t params_tbl[0];
#endif

//
// prototypes
//

// main.c
char * make_gridloc_str(gridloc_t * gl);

// display.c
void display_init(void);
void display_lock(void);
void display_unlock(void);
void display_handler(void);

// circsum.c
void circsim_init(void);
int32_t circsim_cmd(char *cmd);

#endif
