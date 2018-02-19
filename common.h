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

#define MB 0x100000

#define MAX_GRID_X          52
#define MAX_GRID_Y          52
#define MAX_COMPONENT       10000
#define MAX_NODE            10000
#define MAX_GRID_TERM       5

#define MODEL_STATE_RESET    0
#define MODEL_STATE_RUNNING  1
#define MODEL_STATE_PAUSED   2

#define MODEL_STATE_STR(x) \
    ((x) == MODEL_STATE_RESET    ? "RESET"   : \
     (x) == MODEL_STATE_RUNNING  ? "RUNNING" : \
     (x) == MODEL_STATE_PAUSED   ? "PAUSED"  : \
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

#define NO_VALUE  NAN  // xxx how is this being used

// for str_to_val and val_to_str
#define UNITS_VOLTS  1
#define UNITS_AMPS   2
#define UNITS_OHMS   3
#define UNITS_FARADS 4
#define UNITS_HENRYS 5
#define UNITS_HZ     6

#define strcmp strcasecmp

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
    int32_t termid;
    gridloc_t gridloc;
    struct node_s * node;
    double current;  // xxx prior ?
} terminal_t;

typedef struct component_s {
    // these fields describe the component
    int32_t type;
    char * type_str;
    char comp_str[8];
    terminal_t term[2];
    union {
        struct {
            double volts;
            double hz;  // 0 = DC
        } power;
        struct {
            double ohms;
        } resistor;
        struct {
            double farads;
        } capacitor;
        struct {
            double henrys;
        } inductor;
    };
    // component state, used by model.c, follow:
    // when clearing component state, zero from here
    int32_t zero_init_component_state;
    int32_t xxx_tbd;
} component_t;

typedef struct grid_s {
    terminal_t * term[MAX_GRID_TERM];
    char glstr[4];
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
    double voltage[3];   // circular buffer of: prior, curr, new 
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

double      model_time_s;
int32_t     model_state;

//
// parameters
// 

#define PARAM_DELTA_T_US (params_tbl[0].value)
#define PARAM_GRID       (params_tbl[1].value)
#define PARAM_CURRENT    (params_tbl[2].value)
#define PARAM_VOLTAGE    (params_tbl[3].value)
#define PARAM_COMPONENT  (params_tbl[4].value)
#define PARAM_CENTER     (params_tbl[5].value)
#define PARAM_SCALE      (params_tbl[6].value)

typedef struct {
    char *name;
    char  value[100];
} params_tbl_entry_t;

#ifdef MAIN
params_tbl_entry_t params_tbl[] = { 
        { "delta_t_us",     "1"      },   // model time increment
        { "grid",          "off"     },   // on, off
        { "current",       "on"      },   // on, off
        { "voltage",       "on"      },   // on, off
        { "component",     "value"   },   // id, value, off
        { "center",        "c3"      },   // gridloc of display center
        { "scale",         "200"     },   // display scale, pixels between components
        { NULL,            ""        }
                                          };
#else
params_tbl_entry_t params_tbl[0];
#endif

//
// prototypes
//

// main.c

char * gridloc_to_str(gridloc_t * gl, char * s);
int32_t str_to_gridloc(char *glstr, gridloc_t * gl);
char * component_to_value_str(component_t * c, char * s);
char * component_to_full_str(component_t * c, char * s);
int32_t str_to_val(char * s, int32_t units, double * val_result);
char * val_to_str(double val, int32_t units, char * s);

// display.c
void display_init(void);
void display_lock(void);
void display_unlock(void);
int32_t display_center(void);
void display_handler(void);

// circsum.c
void model_init(void);
int32_t model_cmd(char *cmd);

#endif
