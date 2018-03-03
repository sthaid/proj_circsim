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
#include <sys/stat.h>
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
#define MAX_COMPONENT       3000
#define MAX_NODE            3000
#define MAX_GRID_TERM       5
#define MAX_HISTORY         500

#define MODEL_STATE_RESET    0
#define MODEL_STATE_RUNNING  1
#define MODEL_STATE_STOPPED  2

#define MODEL_STATE_STR(x) \
    ((x) == MODEL_STATE_RESET    ? "RESET"   : \
     (x) == MODEL_STATE_RUNNING  ? "RUNNING" : \
     (x) == MODEL_STATE_STOPPED  ? "STOPPED" : \
                                   "????")

#define COMP_NONE           0
#define COMP_WIRE           1
#define COMP_POWER          2
#define COMP_RESISTOR       3
#define COMP_CAPACITOR      4
#define COMP_INDUCTOR       5
#define COMP_DIODE          6
#define COMP_LAST           6

// for str_to_val and val_to_str
#define UNITS_VOLTS     1
#define UNITS_AMPS      2
#define UNITS_OHMS      3
#define UNITS_FARADS    4
#define UNITS_HENRYS    5
#define UNITS_HZ        6
#define UNITS_SECONDS   7

#define strcmp strcasecmp
// XXX also for strncmp

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
} terminal_t;

typedef struct component_s {
    // these fields describe the component
    int32_t type;
    char * type_str;
    char comp_str[8];
    terminal_t term[2];
    union {
        struct {
            bool remote;
            int32_t remote_color;
        } wire;
        struct {
            long double volts;
            long double hz;  // 0 = DC
        } power;
        struct {
            long double ohms;
        } resistor;
        struct {
            long double farads;
        } capacitor;
        struct {
            long double henrys;
        } inductor;
    };
    // component state, used by model.c, follow:
    // when clearing component state, zero from here
    int32_t zero_init_component_state;
    long double i_next;
    long double i_current;
    float i_history[MAX_HISTORY];
} component_t;

typedef struct grid_s {
    terminal_t * term[MAX_GRID_TERM];
    int32_t max_term;
    char glstr[4];
    bool ground;
    bool has_remote_wire;
    int32_t remote_wire_color;
    struct node_s * node;
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
    long double v_next;
    long double v_current;
    long double v_prior;
    float v_history[MAX_HISTORY];
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

long double model_t;
int32_t     model_state;

long double history_t;
int32_t     max_history;

//
// parameters
// 

#define PARAM_STOP_T     0
#define PARAM_DELTA_T    1
#define PARAM_DCPWR_T    2
#define PARAM_GRID       3
#define PARAM_CURRENT    4
#define PARAM_VOLTAGE    5
#define PARAM_COMPONENT  6
#define PARAM_CENTER     7
#define PARAM_SCALE      8
#define PARAM_SCOPE_A    9
#define PARAM_SCOPE_B    10
#define PARAM_SCOPE_C    11
#define PARAM_SCOPE_D    12
#define PARAM_SCOPE_T    13  

#define PARAM_NAME(idx) (param[idx].name)
#define PARAM_VALUE(idx) (param[idx].value)
#define PARAM_SET_VALUE(idx,val) \
    do { \
        if (param[idx].set_only_if_model_state_is_reset && \
            (model_state != MODEL_STATE_RESET)) \
        { \
            ERROR("param %s can only be set when model_state is RESET\n", \
                  param[idx].name); \
        } else { \
            strcpy(param[idx].value, val); \
            param[idx].update_count++; \
        } \
    } while (0)
#define PARAM_HAS_CHANGED(idx) \
    ({ static int32_t last_update_count=-1; \
       bool result = (param[idx].update_count != last_update_count); \
       last_update_count = param[idx].update_count; \
       result; })

#define PARAM_NUMERIC_VALUE(xxx) 1.0 // XXX

#define DEFAULT_SCALE   "200"
#define DEFAULT_CENTER  "c3"

typedef struct {
    char *name;
    char  value[100];
    bool set_only_if_model_state_is_reset;
    int32_t update_count;
} param_tbl_entry_t;

#ifdef MAIN
param_tbl_entry_t param[] = { 
        { "stop_t",    "100ms",        false },   // model stop time
        { "delta_t",   "1ns",          true  },   // model time increment, units=seconds
        { "dcpwr_t",   "1ms",          true  },   // dc power supply time to ramp to voltage, seconds
        { "grid",      "off",          false },   // on, off
        { "current",   "on",           false },   // on, off
        { "voltage",   "on",           false },   // on, off
        { "component", "value",        false },   // id, value, off
        { "center",    DEFAULT_CENTER, false },   // gridloc of display center
        { "scale",     DEFAULT_SCALE,  false },   // display scale, pixels between components
        { "scope_a",   "off",          false },   // xxx comment
        { "scope_b",   "off",          false },   // 
        { "scope_c",   "off",          false },   // 
        { "scope_d",   "off",          false },   // 
        { "scope_t",   "1s",           false },   // 
        { NULL                               }
                                                };
#else
param_tbl_entry_t param[20];
#endif

//
// prototypes
//

// main.c
char * gridloc_to_str(gridloc_t * gl, char * s);
int32_t str_to_gridloc(char *glstr, gridloc_t * gl);
char * component_to_value_str(component_t * c, char * s);
char * component_to_full_str(component_t * c, char * s);
int32_t str_to_val(char * s, int32_t units, long double * val_result);
char * val_to_str(long double val, int32_t units, char * s);

// display.c
void display_init(void);
void display_lock(void);
void display_unlock(void);
void display_handler(void);

// model.c
void model_init(void);
int32_t model_cmd(char *cmdline);
int32_t model_reset(void);
int32_t model_run(void);
int32_t model_stop(void);
int32_t model_cont(void);
int32_t model_step(void);

#endif
