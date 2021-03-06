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

// general
#define MB 0x100000

// max constants
#define MAX_GRID_X          52
#define MAX_GRID_Y          52
#define MAX_COMPONENT       10000
#define MAX_NODE            10000
#define MAX_GRID_TERM       5
#define MAX_HISTORY         500
#define MAX_SCOPE           16

// model state
#define MODEL_STATE_RESET    0
#define MODEL_STATE_RUNNING  1
#define MODEL_STATE_STOPPED  2
#define MODEL_STATE_STR(x) \
    ((x) == MODEL_STATE_RESET    ? "RESET"   : \
     (x) == MODEL_STATE_RUNNING  ? "RUNNING" : \
     (x) == MODEL_STATE_STOPPED  ? "STOPPED" : \
                                   "????")

// component types
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
#define UNITS_WATTS     8

// parameters
#define PARAM_RUN_T           0
#define PARAM_DELTA_T         1
#define PARAM_STEP_COUNT      2
#define PARAM_DCPWR_RAMP      3
#define PARAM_GRID            4
#define PARAM_CURRENT         5
#define PARAM_VOLTAGE         6
#define PARAM_COMPONENT       7
#define PARAM_INTERMEDIATE    8
#define PARAM_CENTER          9
#define PARAM_SCALE           10
#define PARAM_SCOPE_MODE      11
#define PARAM_SCOPE_TRIGGER   12
#define PARAM_SCOPE_SPAN_T    13  
#define PARAM_SCOPE_A         14  // for len MAX_SCOPE

#define param_has_changed(id) \
    ({ static int32_t last_update_count=-1; \
       int32_t updcnt = param_update_count(id); \
       bool result = (updcnt != last_update_count); \
       last_update_count = updcnt; \
       result; })

// range allowed for scaling (zoom) the display
#define MIN_GRID_SCALE     100
#define MAX_GRID_SCALE     400

// power supply wave form
#define WAVE_FORM_SINE      0
#define WAVE_FORM_SQUARE    1
#define WAVE_FORM_TRIANGLE  2

//
// typedefs
//

struct component_s;
struct node_s;

typedef struct {
    float min;
    float max;
} hist_t;

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
            int32_t wave_form;
        } power;
        struct {
            long double ohms;
        } resistor;
        struct {
            long double farads;
        } capacitor;
        struct {
            long double henrys;
            long double i_init;
        } inductor;
        struct {
            int32_t nothing_yet;
        } diode;
    };
    // component state, used by model.c, follow:
    // when clearing component state, init from here
    int32_t start_init_component_state;
    long double i_next;
    long double i_now;
    long double diode_ohms;
    hist_t i_history[MAX_HISTORY];
    tma_t * watts;
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
    // when clearing a node, init from here
    int32_t start_init_node_state;
    int32_t max_term;
    int32_t max_gridloc;
    bool ground;
    terminal_t * power;
    long double v_next;
    long double v_now;
    long double dv_dt;
    hist_t v_history[MAX_HISTORY];
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

int32_t     model_state;
long double model_t;
long double delta_t;
long double stop_t;
int32_t     failed_to_stabilize_count;

long double history_t;
int32_t     max_history;

char        current_filename[200];
int32_t     scope_select_idx;

//
// prototypes
//

// main.c
char * gridloc_to_str(gridloc_t * gl, char * s);
int32_t str_to_gridloc(char *glstr, gridloc_t * gl);
char * component_to_value_str(component_t * c, char * s);
char * component_to_full_str(component_t * c, char * s);
int32_t str_to_val(char * s, int32_t units, long double * val_result);
char * val_to_str(long double val, int32_t units, char * s, bool shorten);
int32_t param_set(int32_t id, char *str);
int32_t param_set_by_name(char *name, char *str);
const char * param_name(int32_t id);
char * param_str_val(int32_t id);
char * param_default_str_val(int32_t id);
long double param_num_val(int32_t id);
int32_t param_update_count(int32_t id);

// display.c
void display_init(void);
void display_lock(void);
void display_unlock(void);
void display_handler(void);

// model.c
void model_init(void);
int32_t model_reset(void);
int32_t model_run(void);
int32_t model_stop(void);
int32_t model_cont(void);
int32_t model_step(void);

#endif
