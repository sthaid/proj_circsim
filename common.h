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

#define COMP_NONE           0
#define COMP_CONNECTION     1
#define COMP_DCPOWER        2
#define COMP_RESISTOR       3
#define COMP_CAPACITOR      4  // xxx later
#define COMP_DIODE          5  // xxx later
#define COMP_SWITCH         6  // xxx later
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
    int32_t id;
    gridloc_t gridloc;
} terminal_t;

typedef struct component_s {
    int32_t type;
    terminal_t term[2];
    union {
        struct {
            float value1;   
            float value2;
        } connection;
        struct {
            float volts;
            float value2;
        } dcpower;
        struct {
            float ohms;
            float value2;
        } resistor;
        struct {
            float farads;
            float value2;
        } capacitor;
        struct {
            float value1;   
            float value2;
        } diode;
        struct {
            float is_closed;
            float value2;
        } switch_;
        float values[2];
    };
} component_t;

typedef struct grid_s {
    terminal_t * term[MAX_GRID_TERM];
    int32_t max_term;
    bool ground;
} grid_t;

typedef struct node_s {
    bool ground;
    terminal_t ** term;
    int32_t max_term;
    int32_t max_alloced_term;
    gridloc_t * gridloc;
    int32_t max_gridloc;
    int32_t max_alloced_gridloc;
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

//
// prototypes
//

void display_handler(void);

int32_t cs_prep(void);

#endif
