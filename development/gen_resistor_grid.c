#include <common.h>

static char * make_component_str(component_t * c);

int32_t main(int32_t argc, char **argv) 
{
    int32_t x, y;
    component_t c;

    memset(&c,0,sizeof(c));

    for (x = 0; x < MAX_GRID_X-1; x++) {
        for (y = 0; y < MAX_GRID_Y; y++) {
            if (y == 26) {
                continue;
            }
            c.type = COMP_RESISTOR;
            c.type_str = "resistor";
            c.term[0].termid = 0;
            c.term[0].gridloc.x = x;
            c.term[0].gridloc.y = y;
            c.term[1].termid = 1;
            c.term[1].gridloc.x = x+1;
            c.term[1].gridloc.y = y;
            c.resistor.ohms = 1;
            printf("add %s\n", make_component_str(&c));
        }
    }
    printf("\n");

    for (x = 0; x < MAX_GRID_X; x++) {
        for (y = 0; y < MAX_GRID_Y-1; y++) {
            if (y == 26) {
                c.type = COMP_WIRE;
                c.type_str = "wire";
            } else {
                c.type = COMP_RESISTOR;
                c.type_str = "resistor";
            } 
            c.term[0].termid = 0;
            c.term[0].gridloc.x = x;
            c.term[0].gridloc.y = y;
            c.term[1].termid = 1;
            c.term[1].gridloc.x = x;
            c.term[1].gridloc.y = y+1;
            c.resistor.ohms = 1;
            printf("add %s\n", make_component_str(&c));
        }
    }
    printf("\n");

    return 0;
}

// -----------------  UTILS FROM MAIN.C  ---------------------------------

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
            gl->y + (gl->y < 26 ? 'a' : 'A' - 26),
            gl->x + 1);

    return s;
}

static char * make_component_str(component_t * c)
{
    #define MAX_S 32

    static char static_str[MAX_S][100];
    static int32_t static_idx;
    int32_t idx;
    char *s, *p;

    idx = __sync_fetch_and_add(&static_idx,1) % MAX_S;
    s = static_str[idx];

    p = s;
    p += sprintf(p, "%-10s %-4s %-4s",
                 c->type_str,
                 make_gridloc_str(&c->term[0].gridloc),
                 make_gridloc_str(&c->term[1].gridloc));

    switch (c->type) {
    case COMP_WIRE:
        break;
    case COMP_RESISTOR:
        p += sprintf(p, "%.2Lf", c->resistor.ohms);
        break;
    default:
        assert(0);
    }

    return s;
}

