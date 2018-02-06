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

#include "util_sdl.h"
#include "util_sdl_predefined_panes.h"
#include "util_misc.h"

//
// defines
//

#define MAX_GRID_X      50
#define MAX_GRID_Y      26
#define MAX_COMPONENT  100

#define COMP_NONE        99
#define COMP_RESISTOR    0
#define COMP_CAPACITOR   1
#define COMP_DIODE       2
#define COMP_OPEN_SWITCH 3     // xxx dont want 2 components
#define COMP_CLOSED_SWITCH 4
#define COMP_DC_POWER      5
#define COMP_WIRE          10

//#define COMP_POWER       1
//#define COMP_WIRE        3
//#define COMP_SWITCH      4

//#define GRID_DIR_UP      0
//#define GRID_DIR_RIGHT   1
//#define GRID_DIR_DOWN    2
//#define GRID_DIR_LEFT    3

//
// typedefs
//

struct component_s;

typedef struct {
    struct component_s * component;
    int32_t id;
    int32_t grid_x;
    int32_t grid_y;   
} terminal_t;

typedef struct component_s {
    int32_t type;
    terminal_t term[2];
    union {
        struct {
            float ohms;
        } resistor;
    };
} component_t;

typedef struct {
    terminal_t * terminal[4];  // up, right, down, left
} grid_t;

//
// variables
// 

grid_t grid[MAX_GRID_X][MAX_GRID_Y];
component_t component[MAX_COMPONENT];

int32_t max_component;


#endif
