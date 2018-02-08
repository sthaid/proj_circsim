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

// -----------------  UTILS  -------------------------------------------------------------------

int32_t make_gridloc(char *glstr, gridloc_t * gl)
{
    int32_t x=-1, y=-1;

    if (glstr[0] >= 'A' && glstr[0] <= 'Z') {
        y = glstr[0] - 'A';
    } else if (glstr[0] >= 'a' && glstr[0] <= 'z') {
        y = glstr[0] - 'a' + 26;
    }

    sscanf(glstr+1, "%d", &x);

    if (y < 0 || y >= MAX_GRID_Y || x < 0 || x >= MAX_GRID_X) {
        return -1;
    }

    gl->x = x;
    gl->y = y;
    return 0;
}

char * make_gridloc_str(gridloc_t * gl)
{
    #define MAX_S 32
    static char static_str[MAX_S][8];
    static int32_t static_idx;
    int32_t idx;
    char *s;

    assert(gl->x >= 0 && gl->x < MAX_GRID_X);
    assert(gl->y >= 0 && gl->y < MAX_GRID_Y);

    idx = __sync_fetch_and_add(&static_idx,1) % MAX_S;
    s = static_str[idx];

    sprintf(s, "%c%d", 
            gl->y + (gl->y < 26 ? 'A' : 'a' - 26),
            gl->x);

    return s;
}


char * component_type_str(int32_t type)
{
    static char * type_str[] = { "NONE",
                                 "CONNECTION",
                                 "DCPOWER",
                                 "RESISTOR",
                                 "CAPACITOR",
                                 "DIODE",
                                 "SWITCH",
                                            };

    assert(type >= 0 && type <= sizeof(type_str)/sizeof(char*));
    return type_str[type];
}

int32_t component_num_values(int32_t type)
{
    static int32_t num_values[] = { 0,  // "NONE"
                                    0,  // "CONNECTION"
                                    1,  // "DCPOWER"
                                    1,  // "RESISTOR"
                                    1,  // "CAPACITOR"
                                    0,  // "DIODE"
                                    1,  // "SWITCH"
                                             };

    assert(type >= 0 && type <= sizeof(num_values)/sizeof(num_values[0]));
    return num_values[type];
}

char * make_component_definition_str(component_t * c)
{
    #define MAX_S 32
    static char static_str[MAX_S][8];
    static int32_t static_idx;
    int32_t idx, i;
    char *s, *p;

    idx = __sync_fetch_and_add(&static_idx,1) % MAX_S;
    s = static_str[idx];

    p = s;
    p += sprintf(p, "%-10s %-3s %-3s",
                 component_type_str(c->type),
                 make_gridloc_str(&c->term[0].gridloc),
                 make_gridloc_str(&c->term[1].gridloc));
    for (i = 0; i < component_num_values(c->type); i++) {
        p += sprintf(p, " %f", c->values[i]);
    }

    return s;
}

