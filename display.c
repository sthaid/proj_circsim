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

#define DEFAULT_WIN_WIDTH  1900
#define DEFAULT_WIN_HEIGHT 1000

#define PH_SCHEMATIC_X     0
#define PH_SCHEMATIC_Y     0
#define PH_SCHEMATIC_W     1375
#define PH_SCHEMATIC_H     1000

#define PH_STATUS_X        1375
#define PH_STATUS_Y        0
#define PH_STATUS_W        525  
#define PH_STATUS_H        130

#define PH_SCOPE_X         1375
#define PH_SCOPE_Y         130
#define PH_SCOPE_W         525  
#define PH_SCOPE_H         870

#define FPSZ_MEDIUM        30
#define FPSZ_SMALL         24
#define FPSZ_SMALLER       16

//
// typedefs
//

typedef struct {
    bool enabled;
    gridloc_t gl0;
    gridloc_t gl1;
} scope_select_t;

//
// variables
//

static pthread_mutex_t mutex;
static int32_t win_width, win_height;

static int32_t grid_xoff; 
static int32_t grid_yoff;
static long double  grid_scale;

static scope_select_t scope_select;

//
// prototypes
//

static void display_start(void * cx);
static void display_end(void * cx);
static int32_t pane_hndlr_schematic(pane_cx_t * pane_cx, int32_t request, void * init, sdl_event_t * event);
static int32_t pane_hndlr_status(pane_cx_t * pane_cx, int32_t request, void * init, sdl_event_t * event);
static int32_t pane_hndlr_scope(pane_cx_t * pane_cx, int32_t request, void * init, sdl_event_t * event);

// -----------------  PUBLIC  ---------------------------------------------

void display_init(void)
{
    pthread_mutexattr_t attr;

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mutex, &attr);
}

void display_lock(void)
{
    pthread_mutex_lock(&mutex);
}

void display_unlock(void)
{
    pthread_mutex_unlock(&mutex);
}

void display_handler(void)
{
    // use sdl to display the schematic
    win_width  = DEFAULT_WIN_WIDTH;
    win_height = DEFAULT_WIN_HEIGHT;
    if (sdl_init(&win_width, &win_height, true) < 0) {
        FATAL("sdl_init %dx%d failed\n", win_width, win_height);
    }
    sdl_pane_manager(
        NULL,           // context
        display_start,  // called prior to pane handlers
        display_end,    // called after pane handlers
        100000,         // 0=continuous, -1=never, else us 
        3,              // number of pane handler varargs that follow
        pane_hndlr_schematic, NULL, PH_SCHEMATIC_X, PH_SCHEMATIC_Y, PH_SCHEMATIC_W, PH_SCHEMATIC_H, PANE_BORDER_STYLE_MINIMAL,
        pane_hndlr_status,    NULL, PH_STATUS_X,    PH_STATUS_Y,    PH_STATUS_W,    PH_STATUS_H,    PANE_BORDER_STYLE_MINIMAL,
        pane_hndlr_scope,     NULL, PH_SCOPE_X,     PH_SCOPE_Y,     PH_SCOPE_W,     PH_SCOPE_H,     PANE_BORDER_STYLE_MINIMAL
                        );
}

// -----------------  DISPLAY HANDLER SUPPORT -----------------------------

static void display_start(void * cx)
{
    // acquire display lock at the start of redrawing the display
    display_lock();
}

static void display_end(void * cx)
{
    // release the display lock when done redrawing the display
    display_unlock();
}

// -----------------  PANE HNDLR SCHEMATIC  -------------------------------

static int32_t pane_hndlr_schematic(pane_cx_t * pane_cx, int32_t request, void * init, sdl_event_t * event) 
{
    #define SDL_EVENT_MOUSE_MOTION  (SDL_EVENT_USER_DEFINED + 0)
    #define SDL_EVENT_MOUSE_WHEEL   (SDL_EVENT_USER_DEFINED + 1)

    #define OUT_OF_PANE(x,y) ( (x) < x_min || (x) > x_max || \
                               (y) < y_min || (y) > y_max )

    typedef struct {
        struct {
            int32_t x;
            int32_t y;
        } points[20][50];
    } component_image_t;

    static component_image_t power_image =
        { { { {0,0}, {400,0}, {-1,-1} },
            { {400,0}, {429,71}, {500,100}, {571,71}, {600,0,}, {571,-71}, {500,-100}, {429,-71}, {400,0}, {-1,-1} },
            { {600,0}, {1000,0}, {-1,-1} },
            { {-1,-1} } } };

    static component_image_t resistor_image = 
        { { { {0,0}, {200,0}, {250,-50}, {350,50}, {450,-50}, {550,50}, {650,-50}, {750,50}, {800,0}, {1000,0}, {-1,-1} },
            { {-1,-1} } } };

    static component_image_t capacitor_image = 
        { { { {0,0}, {450,0}, {-1,-1} },
            { {550,0}, {1000,0}, {-1,-1} },
            { {450,-70}, {450,70}, {-1,-1} },
            { {550,-70}, {550,70}, {-1,-1} },
            { {-1,-1} } } };

    static component_image_t inductor_image =  
        { { { {0,0}, {275,0}, 
              {275,50}, {300,75}, {325,75}, {350,50}, {350,0}, 
              {350,50}, {375,75}, {400,75}, {425,50}, {425,0}, 
              {425,50}, {450,75}, {475,75}, {500,50}, {500,0}, 
              {500,50}, {525,75}, {550,75}, {575,50}, {575,0}, 
              {575,50}, {600,75}, {625,75}, {650,50}, {650,0}, 
              {650,50}, {675,75}, {700,75}, {725,50}, {725,0}, 
              {1000,0},
              {-1,-1} },
            { {-1,-1} } } };

    static component_image_t diode_image = 
        { { { {0,0}, {450,0}, {-1,-1} },
            { {450,-71}, {450,71}, {-1,-1} },
            { {450,-71}, {550,0}, {-1,-1} },
            { {450,71}, {550,0}, {-1,-1} },
            { {550,71}, {550,-71}, {-1,-1} },
            { {550,0}, {1000,0}, {-1,-1} },
            { {-1,-1} } } };

    struct {
        int32_t nothing;
    } * vars = pane_cx->vars;
    rect_t * pane = &pane_cx->pane;

    // ----------------------------
    // -------- INITIALIZE --------
    // ----------------------------

    if (request == PANE_HANDLER_REQ_INITIALIZE) {
        vars = pane_cx->vars = calloc(1,sizeof(*vars));
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // ------------------------
    // -------- RENDER --------
    // ------------------------

    if (request == PANE_HANDLER_REQ_RENDER) {
        int32_t x_min, x_max, y_min, y_max;
        int32_t fpsz;
        bool    display_intermediate_values;
        int32_t large_point_size, small_point_size;

        // determine grid_xoff, grid_yoff, grid_scale from
        // PARAM_CENTER, PARAM_SCALE
        {
        gridloc_t gl;
        int32_t xadj=0, yadj=0;
        char *p;

        str_to_gridloc(param_str_val(PARAM_CENTER), &gl);
        if ((p = strchr(param_str_val(PARAM_CENTER), ',')) != NULL) {
            sscanf(p+1, "%d,%d", &xadj, &yadj);
        }

        grid_scale = param_num_val(PARAM_SCALE);
        grid_xoff = -grid_scale * gl.x + PH_SCHEMATIC_W/2 - xadj * grid_scale / 1000;
        grid_yoff = -grid_scale * gl.y + PH_SCHEMATIC_H/2 - yadj * grid_scale / 1000;
        }

        // initialize range of x,y that will be rendered by the 
        // the subsequent code; these variables are used by the OUT_OF_PANE macro
        x_min = -grid_scale;                  
        x_max = PH_SCHEMATIC_W + grid_scale; 
        y_min = -grid_scale;                
        y_max = PH_SCHEMATIC_H + grid_scale;

        // select font point size based on grid_scale
        fpsz = grid_scale * 40 / MAX_GRID_SCALE;

        // determine the size of the large and small points,
        // the largest size currently supported by util_sdl is 9
        // - large points are to display the selected scope
        // - small points are used for component and wire junctions
        // - small points are used to display the grid locations  (set grid on)
        large_point_size = 9 * grid_scale / 350;
        if (large_point_size > 9) large_point_size = 9;
        if (large_point_size < 6) large_point_size = 6;
        small_point_size = large_point_size - 3;

        // determine whether intermediate voltage and current values will be displayed
        // - normally the intermediate values should not be displayed;
        // - intermediate values are the values that are computed by the model during the 
        //   circuit evaluation before the evaluation stabilizes at the values that allow
        //   the model to proceed to the next time;
        // - displaying intermediate values is useful when evaluating the resistor grid, 
        //   to watch the progression prior to the resistor grid evaluation stabilizing
        display_intermediate_values = (strcasecmp(param_str_val(PARAM_INTERMEDIATE), "on") == 0);

        // draw grid, if enabled
        if (strcasecmp(param_str_val(PARAM_GRID), "on") == 0) {
            int32_t glx, gly, x, y, count=0;
            point_t points[MAX_GRID_X*MAX_GRID_Y];

            // draw gridloc_str at each grid point
            for (glx = 0; glx < MAX_GRID_X; glx++) {
                for (gly = 0; gly < MAX_GRID_Y; gly++) {
                    x = glx * grid_scale + grid_xoff + 2;
                    y = gly * grid_scale + grid_yoff - 2 - sdl_font_char_height(fpsz);
                    if (OUT_OF_PANE(x,y)) {
                        continue;
                    }
                    sdl_render_printf(pane, x, y, fpsz, BLUE, WHITE, "%s", grid[glx][gly].glstr);
                }
            }

            // draw grid points
            for (glx = 0; glx < MAX_GRID_X; glx++) {
                for (gly = 0; gly < MAX_GRID_Y; gly++) {
                    x = glx * grid_scale + grid_xoff;
                    y = gly * grid_scale + grid_yoff;
                    if (OUT_OF_PANE(x,y)) {
                        continue;
                    }
                    points[count].x = x;
                    points[count].y = y;
                    count++;
                }
            }
            sdl_render_points(pane, points, count, BLUE, small_point_size);
        }

        // draw schematic components
        { int32_t i;
        for (i = 0; i < max_component; i++) {
            component_t * c  = &component[i];
            int32_t color;

            // if the component is the selected scope then display the component
            // in BLUE, else BLACK
            if ((scope_select.enabled) &&
                ((memcmp(&c->term[0].gridloc, &scope_select.gl0, sizeof(gridloc_t)) == 0 &&
                  memcmp(&c->term[1].gridloc, &scope_select.gl1, sizeof(gridloc_t)) == 0) ||
                 (memcmp(&c->term[0].gridloc, &scope_select.gl1, sizeof(gridloc_t)) == 0 &&
                  memcmp(&c->term[1].gridloc, &scope_select.gl0, sizeof(gridloc_t)) == 0)))
            {
                color = BLUE;
            } else {
                color = BLACK;
            }

            // draw the component 
            switch (c->type) {
            case COMP_WIRE: {
                if (c->wire.remote == false) {
                    int32_t x1 = c->term[0].gridloc.x * grid_scale + grid_xoff;
                    int32_t y1 = c->term[0].gridloc.y * grid_scale + grid_yoff;
                    int32_t x2 = c->term[1].gridloc.x * grid_scale + grid_xoff;
                    int32_t y2 = c->term[1].gridloc.y * grid_scale + grid_yoff;
                    if (OUT_OF_PANE(x1,y1) || OUT_OF_PANE(x2,y2)) {
                        continue;
                    }
                    sdl_render_line(pane, x1, y1, x2, y2, color);
                }
                break; }
            case COMP_POWER:
            case COMP_RESISTOR:
            case COMP_CAPACITOR:
            case COMP_INDUCTOR:
            case COMP_DIODE: {
                component_image_t * ci = NULL;
                int32_t x, y, j, k;
                point_t points[100];

                x = c->term[0].gridloc.x * grid_scale + grid_xoff;
                y = c->term[0].gridloc.y * grid_scale + grid_yoff;
                if (OUT_OF_PANE(x,y)) {
                    continue;
                }

                if (c->type == COMP_POWER) {
                    ci = &power_image;
                } else if (c->type == COMP_RESISTOR) {
                    ci = &resistor_image;
                } else if (c->type == COMP_CAPACITOR) {
                    ci = &capacitor_image;
                } else if (c->type == COMP_INDUCTOR) {
                    ci = &inductor_image;
                } else if (c->type == COMP_DIODE) {
                    ci = &diode_image;
                } else {
                    FATAL("invalid component type %d\n", c->type);
                }
                for (j = 0; ci->points[j][0].x != -1; j++) {
                    for (k = 0; ci->points[j][k].x != -1; k++) {
                        if (c->term[1].gridloc.x == c->term[0].gridloc.x + 1) {
                            points[k].x = x + ci->points[j][k].x * grid_scale / 1000;  // right
                            points[k].y = y + ci->points[j][k].y * grid_scale / 1000;
                        } else if (c->term[1].gridloc.x == c->term[0].gridloc.x - 1) {
                            points[k].x = x - ci->points[j][k].x * grid_scale / 1000;  // leftt
                            points[k].y = y + ci->points[j][k].y * grid_scale / 1000;
                        } else if (c->term[1].gridloc.y == c->term[0].gridloc.y + 1) {
                            points[k].x = x + ci->points[j][k].y * grid_scale / 1000;  // down
                            points[k].y = y + ci->points[j][k].x * grid_scale / 1000; 
                        } else if (c->term[1].gridloc.y == c->term[0].gridloc.y - 1) {
                            points[k].x = x + ci->points[j][k].y * grid_scale / 1000;  // up
                            points[k].y = y - ci->points[j][k].x * grid_scale / 1000; 
                        } else {
                            char s0[10], s1[10];
                            FATAL("component %s incorrect term locations %s %s\n",
                                  c->comp_str, 
                                  gridloc_to_str(&c->term[0].gridloc,s0),
                                  gridloc_to_str(&c->term[1].gridloc,s1));
                        }
                    }
                    sdl_render_lines(pane, points, k, color);
                }
                break; }
            case COMP_NONE:
                break;
            default:
                FATAL("invalid component type %d\n", c->type);
                break;
            }
        } }

        // display component id or value or power, if enabled
        if (strcasecmp(param_str_val(PARAM_COMPONENT), "id") == 0 ||
            strcasecmp(param_str_val(PARAM_COMPONENT), "value") == 0 ||
            strcasecmp(param_str_val(PARAM_COMPONENT), "power") == 0)
        {
            int32_t i, x, y;
            char *s, s1[100];
            component_t *c;

            for (i = 0; i < max_component; i++) {
                c = &component[i];
                if (c->type == COMP_NONE) {
                    continue;
                }

                x = c->term[0].gridloc.x * grid_scale + grid_xoff;
                y = c->term[0].gridloc.y * grid_scale + grid_yoff;
                if (OUT_OF_PANE(x,y)) {
                    continue;
                }

                if (strcasecmp(param_str_val(PARAM_COMPONENT), "id") == 0) {
                    s = c->comp_str;
                } else if (strcasecmp(param_str_val(PARAM_COMPONENT), "value") == 0) {
                    s = component_to_value_str(c,s1);
                } else {  // must be "power"
                    s = "";
                    if (c->watts && c->type != COMP_CAPACITOR && c->type != COMP_INDUCTOR) {
                        long double watts = timed_moving_average_query(c->watts);
                        s = isnan(watts) ? "" : val_to_str(watts,UNITS_WATTS,s1,false);
                    }
                }

                if (s[0] == '\0') {
                    continue;
                }

                if (c->term[1].gridloc.x == c->term[0].gridloc.x + 1) {         // right
                    x += grid_scale / 2 - strlen(s) * sdl_font_char_width(fpsz) / 2;
                    y -= 2 * sdl_font_char_height(fpsz);
                } else if (c->term[1].gridloc.x == c->term[0].gridloc.x - 1) {  // left
                    x -= grid_scale / 2 + strlen(s) * sdl_font_char_width(fpsz) / 2;
                    y -= 2 * sdl_font_char_height(fpsz);
                } else if (c->term[1].gridloc.y == c->term[0].gridloc.y + 1) {  // down 
                    x += 2 * sdl_font_char_width(fpsz);
                    y += grid_scale / 2 - sdl_font_char_height(fpsz) / 2;
                } else if (c->term[1].gridloc.y == c->term[0].gridloc.y - 1) {  // up
                    x += 2 * sdl_font_char_width(fpsz);
                    y -= grid_scale / 2 + sdl_font_char_height(fpsz) / 2;
                } else {
                    // this should only occure for a long wire or a remote wire; 
                    // in this case don't display the wire's component id
                    continue;
                }
                sdl_render_printf(pane, x, y, fpsz, BLACK, WHITE, "%s", s);
            }
        } 

        // display the voltage at all node gridlocs;
        if (strcasecmp(param_str_val(PARAM_GRID), "off") == 0 &&
            strcasecmp(param_str_val(PARAM_VOLTAGE), "on") == 0) 
        {
            int32_t i, j, x, y;
            component_t *c;
            terminal_t *term;
            node_t *n;
            char s[100];
            long double voltage;

            for (i = 0; i < max_component; i++) {
                c  = &component[i];
                if (c->type == COMP_NONE || c->type == COMP_WIRE) {
                    continue;
                }
                for (j = 0; j < 2; j++) {
                    term = &c->term[j];
                    n = term->node;
                    if (n == NULL) {
                        continue;
                    }

                    x = term->gridloc.x * grid_scale + grid_xoff + 2;
                    y = term->gridloc.y * grid_scale + grid_yoff - 2 - sdl_font_char_height(fpsz);
                    if (OUT_OF_PANE(x,y)) {
                        continue;
                    }

                    voltage = (display_intermediate_values ? n->v_next : n->v_now);
                    val_to_str(voltage, UNITS_VOLTS, s, false);
                    sdl_render_printf(pane, x, y, fpsz, BLACK, WHITE, "%s", s);
                }
            }
        }

        // display current for all components, if enabled
        if (strcasecmp(param_str_val(PARAM_CURRENT), "on") == 0) {
            int32_t i, x, y;
            component_t *c;
            long double current;
            char current_str[100], *pre_str, *post_str;

            for (i = 0; i < max_component; i++) {
                c  = &component[i];
                if (c->type == COMP_NONE || c->type == COMP_WIRE) {
                    continue;
                }

                if (c->term[0].node == NULL || c->term[1].node == NULL) {
                    continue;
                }

                x = c->term[0].gridloc.x * grid_scale + grid_xoff;
                y = c->term[0].gridloc.y * grid_scale + grid_yoff;
                if (OUT_OF_PANE(x,y)) {
                    continue;
                }

                current = (display_intermediate_values ? c->i_next : c->i_now);
                val_to_str(fabsl(current), UNITS_AMPS, current_str, false);
                pre_str = "";
                post_str = "";

                if (c->term[1].gridloc.x == c->term[0].gridloc.x + 1) {         // right
                    if (strcasecmp(current_str, "0A")) {
                        if (current > 0) post_str = " >";
                        else if (current < 0) pre_str = "< ";
                    }
                    x += grid_scale / 2 - (strlen(current_str)+2) * sdl_font_char_width(fpsz) / 2;
                    y += 1 * sdl_font_char_height(fpsz);
                } else if (c->term[1].gridloc.x == c->term[0].gridloc.x - 1) {  // left
                    if (strcasecmp(current_str, "0A")) {
                        if (current > 0) pre_str = "< ";
                        else if (current < 0) post_str = " >";
                    }
                    x -= grid_scale / 2 + (strlen(current_str)+2) * sdl_font_char_width(fpsz) / 2;
                    y += 1 * sdl_font_char_height(fpsz);
                } else if (c->term[1].gridloc.y == c->term[0].gridloc.y + 1) {  // down 
                    if (strcasecmp(current_str, "0A")) {
                        if (current > 0) pre_str = "v ";
                        else if (current < 0) pre_str = "^ ";
                    }
                    x += 2 * sdl_font_char_width(fpsz);
                    y += grid_scale / 2 - sdl_font_char_height(fpsz) / 2 - 2 * sdl_font_char_height(fpsz);
                } else if (c->term[1].gridloc.y == c->term[0].gridloc.y - 1) {  // up
                    if (strcasecmp(current_str, "0A")) {
                        if (current > 0) pre_str = "^ ";
                        else if (current < 0) pre_str = "v ";
                    }
                    x += 2 * sdl_font_char_width(fpsz);
                    y -= grid_scale / 2 + sdl_font_char_height(fpsz) / 2 + 2 * sdl_font_char_height(fpsz);
                } else {
                    // COMP_POWER may or may not be adjacent
                    continue;
                }

                sdl_render_printf(pane, x, y, fpsz, BLACK, WHITE, "%s%s%s", pre_str, current_str, post_str);
            }
        }

        // if scope select is enabled then draw a large blue dot at the 
        // two gridloc's associated with the selected scope
        { int32_t x,y;
        if (scope_select.enabled) {
            x = scope_select.gl0.x * grid_scale + grid_xoff;
            y = scope_select.gl0.y * grid_scale + grid_yoff;
            sdl_render_point(pane, x, y, BLUE, large_point_size);
            x = scope_select.gl1.x * grid_scale + grid_xoff;
            y = scope_select.gl1.y * grid_scale + grid_yoff;
            sdl_render_point(pane, x, y, BLUE, large_point_size);
        } }

        // draw a point at all grid locations that have at least one terminal as follows:
        // - ground      : GREEN
        // - remote_wire : the color assigned to the remote wire
        // - otherwise   : BLACK
        // if a grid location is both ground and remote-wire then alternate the color
        { int32_t glx, gly, color, x, y;
          grid_t *g;
          static int32_t count;
        count++;
        for (glx = 0; glx < MAX_GRID_X; glx++) {
            for (gly = 0; gly < MAX_GRID_Y; gly++) {
                g = &grid[glx][gly];

                if (g->max_term == 0) {
                    continue;
                }

                x = glx * grid_scale + grid_xoff;
                y = gly * grid_scale + grid_yoff;
                if (OUT_OF_PANE(x,y)) {
                    continue;
                }

                // alternate color if ground and remote
                color = (g->has_remote_wire && g->ground && (count % 20 < 10) ? GREEN :
                         g->has_remote_wire                                   ? g->remote_wire_color :
                         g->ground                                            ? GREEN 
                                                                              : BLACK);
                sdl_render_point(pane, x, y, color, small_point_size);
            }
        } }

        // if there is a current_filename then display it, top center
        { int32_t len = strlen(current_filename);
        if (len > 0) {
            int32_t x = pane->w/2 - len*sdl_font_char_width(FPSZ_MEDIUM)/2;
            if (x < 0) x = 0;
            sdl_render_printf(pane, x, 0, FPSZ_MEDIUM, BLACK, WHITE, "%s", current_filename);
        } }

        // register for mouse motion and mouse wheel events
        // - mouse motion used to pan 
        // - mouse wheel used to zoom
        rect_t locf = {0,0,pane->w,pane->h};
        sdl_register_event(pane, &locf, SDL_EVENT_MOUSE_MOTION, SDL_EVENT_TYPE_MOUSE_MOTION, pane_cx);
        sdl_register_event(pane, &locf, SDL_EVENT_MOUSE_WHEEL, SDL_EVENT_TYPE_MOUSE_WHEEL, pane_cx);

        return PANE_HANDLER_RET_NO_ACTION;
    }

    // -----------------------
    // -------- EVENT --------
    // -----------------------

    if (request == PANE_HANDLER_REQ_EVENT) {
        switch(event->event_id) {
        case SDL_EVENT_MOUSE_MOTION: {
            gridloc_t gl;
            int32_t xoff, yoff, xadj, yadj, new_grid_xoff, new_grid_yoff;
            char param_center_str[100], str[100];

            // determine new grid_xoff/yoff by incorporating the mouse motion
            new_grid_xoff = grid_xoff + event->mouse_motion.delta_x;
            new_grid_yoff = grid_yoff + event->mouse_motion.delta_y;

            // determine the nearest gl to the new_grid_xoff/yoff
            gl.x = (new_grid_xoff - (PH_SCHEMATIC_W/2) - grid_scale/2) / (-grid_scale);
            gl.y = (new_grid_yoff - (PH_SCHEMATIC_H/2) - grid_scale/2) / (-grid_scale);
            if (gl.x < 0) gl.x = 0;
            if (gl.x >= MAX_GRID_X) gl.x = MAX_GRID_X-1;
            if (gl.y < 0) gl.y = 0;
            if (gl.y >= MAX_GRID_X) gl.y = MAX_GRID_Y-1;

            // determine xadj/yadj which is the difference between the new_grid_xoff/yoff and
            // the nearest gridloc; an xadj/yadj value of 1000 will move the center to the
            // adjacent gridloc
            xoff = -grid_scale * gl.x + PH_SCHEMATIC_W/2;
            yoff = -grid_scale * gl.y + PH_SCHEMATIC_H/2;
            xadj = -(new_grid_xoff - xoff) * 1000 / grid_scale;
            yadj = -(new_grid_yoff - yoff) * 1000 / grid_scale;

            // update PARAM_CENTER, using gl,xadj,yadj;  which exactly specifies the center
            sprintf(param_center_str, "%s,%d,%d",
                    gridloc_to_str(&gl,str), xadj, yadj);
            param_set(PARAM_CENTER, param_center_str);
            return PANE_HANDLER_RET_DISPLAY_REDRAW; }
        case SDL_EVENT_MOUSE_WHEEL: {
            long double new_grid_scale = 0;
            char param_scale_str[100];

            // determine new_grid_scale by multiplying or dividing the current
            // grid_scale by 1.1
            if (event->mouse_wheel.delta_y > 0) {
                new_grid_scale = grid_scale * 1.1;
            } if (event->mouse_wheel.delta_y < 0) {
                new_grid_scale = grid_scale * (1./1.1);
            }

            // limit range of new grid scale
            if (new_grid_scale < MIN_GRID_SCALE) {
                new_grid_scale = MIN_GRID_SCALE;  
            } else if (new_grid_scale > MAX_GRID_SCALE) {
                new_grid_scale = MAX_GRID_SCALE;  
            }

            // update the PARAM_SCALE 
            sprintf(param_scale_str, "%.0Lf", new_grid_scale);
            param_set(PARAM_SCALE, param_scale_str);
            return PANE_HANDLER_RET_DISPLAY_REDRAW; }
        }
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // ---------------------------
    // -------- TERMINATE --------
    // ---------------------------

    if (request == PANE_HANDLER_REQ_TERMINATE) {
        free(vars);
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // not reached
    assert(0);
    return PANE_HANDLER_RET_NO_ACTION;
}

// -----------------  PANE HNDLR STATUS  ----------------------------------

static int32_t pane_hndlr_status(pane_cx_t * pane_cx, int32_t request, void * init, sdl_event_t * event) 
{
    #define SDL_EVENT_MODEL_RESET  (SDL_EVENT_USER_DEFINED + 10)
    #define SDL_EVENT_MODEL_RUN    (SDL_EVENT_USER_DEFINED + 11)
    #define SDL_EVENT_MODEL_STOP   (SDL_EVENT_USER_DEFINED + 12)
    #define SDL_EVENT_MODEL_CONT   (SDL_EVENT_USER_DEFINED + 13)
    #define SDL_EVENT_MODEL_STEP   (SDL_EVENT_USER_DEFINED + 14)

    struct {
        int32_t none;
    } * vars = pane_cx->vars;
    rect_t * pane = &pane_cx->pane;

    // ----------------------------
    // -------- INITIALIZE --------
    // ----------------------------

    if (request == PANE_HANDLER_REQ_INITIALIZE) {
        vars = pane_cx->vars = calloc(1,sizeof(*vars));
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // ------------------------
    // -------- RENDER --------
    // ------------------------

    if (request == PANE_HANDLER_REQ_RENDER) {
        char s[100];

        // state and time
        sdl_render_printf(pane, 0, ROW2Y(0,FPSZ_MEDIUM), FPSZ_MEDIUM, BLACK, WHITE, 
                          "%-8s %s", 
                          MODEL_STATE_STR(model_state),
                          val_to_str(model_t, UNITS_SECONDS, s, false));

        // stop time
        sdl_render_printf(pane, 0, ROW2Y(1,FPSZ_MEDIUM), FPSZ_MEDIUM, BLACK, WHITE, 
                          "%-8s %s", 
                          "STOP_T",
                          val_to_str(stop_t, UNITS_SECONDS, s, false));

        // delta_t time
        sdl_render_printf(pane, 0, ROW2Y(2,FPSZ_MEDIUM), FPSZ_MEDIUM, BLACK, WHITE, 
                          "%-8s %s", 
                          "DELTA_T", 
                          val_to_str(delta_t, UNITS_SECONDS, s, false));

        // failed_to_stabilize_count, only display if greater than 0
        if (failed_to_stabilize_count > 0) {
            // XXX debug why the '-2' is needed below
            sdl_render_printf(pane, pane->w-COL2X(9,FPSZ_MEDIUM)-2, ROW2Y(3,FPSZ_MEDIUM), FPSZ_MEDIUM, RED, WHITE, 
                              "%9d", failed_to_stabilize_count);
        }

        // register for mouse click events to control the model from the display
        sdl_render_text_and_register_event(
            pane, COL2X(0,FPSZ_MEDIUM), ROW2Y(3,FPSZ_MEDIUM), FPSZ_MEDIUM, "RESET", LIGHT_BLUE, WHITE,
            SDL_EVENT_MODEL_RESET, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
        switch (model_state) {
        case MODEL_STATE_RESET:
            sdl_render_text_and_register_event(
                pane, COL2X(9,FPSZ_MEDIUM), ROW2Y(3,FPSZ_MEDIUM), FPSZ_MEDIUM, "RUN", LIGHT_BLUE, WHITE,
                SDL_EVENT_MODEL_RUN, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
            break;
        case MODEL_STATE_RUNNING:
            sdl_render_text_and_register_event(
                pane, COL2X(9,FPSZ_MEDIUM), ROW2Y(3,FPSZ_MEDIUM), FPSZ_MEDIUM, "STOP", LIGHT_BLUE, WHITE,
                SDL_EVENT_MODEL_STOP, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
            break;
        case MODEL_STATE_STOPPED:
            sdl_render_text_and_register_event(
                pane, COL2X(9,FPSZ_MEDIUM), ROW2Y(3,FPSZ_MEDIUM), FPSZ_MEDIUM, "CONT", LIGHT_BLUE, WHITE,
                SDL_EVENT_MODEL_CONT, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
            break;
        }
        sdl_render_text_and_register_event(
            pane, COL2X(15,FPSZ_MEDIUM), ROW2Y(3,FPSZ_MEDIUM), FPSZ_MEDIUM, "STEP", LIGHT_BLUE, WHITE,
            SDL_EVENT_MODEL_STEP, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);

        return PANE_HANDLER_RET_NO_ACTION;
    }

    // -----------------------
    // -------- EVENT --------
    // -----------------------

    if (request == PANE_HANDLER_REQ_EVENT) {
        switch(event->event_id) {
        case SDL_EVENT_MODEL_RESET:
            model_reset();
            break;
        case SDL_EVENT_MODEL_RUN:
            model_run();
            break;
        case SDL_EVENT_MODEL_STOP:
            model_stop();
            break;
        case SDL_EVENT_MODEL_CONT:
            model_cont();
            break;
        case SDL_EVENT_MODEL_STEP:
            model_step();
            break;
        }
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // ---------------------------
    // -------- TERMINATE --------
    // ---------------------------

    if (request == PANE_HANDLER_REQ_TERMINATE) {
        free(vars);
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // not reached
    assert(0);
    return PANE_HANDLER_RET_NO_ACTION;
}

// -----------------  PANE HNDLR SCOPE  -----------------------------------

static int32_t pane_hndlr_scope(pane_cx_t * pane_cx, int32_t request, void * init, sdl_event_t * event) 
{
    #define SDL_EVENT_SCOPE_MODE      (SDL_EVENT_USER_DEFINED + 20)
    #define SDL_EVENT_SCOPE_TRIG      (SDL_EVENT_USER_DEFINED + 21)
    #define SDL_EVENT_MOUSE_WHEEL2    (SDL_EVENT_USER_DEFINED + 22)
    #define SDL_EVENT_MOUSE_MOTION2   (SDL_EVENT_USER_DEFINED + 23)
    #define SDL_EVENT_SCOPE_SELECT_A  (SDL_EVENT_USER_DEFINED + 24)  // for length MAX_SCOPE

    struct {
        int32_t y_scroll;
    } * vars = pane_cx->vars;
    rect_t * pane = &pane_cx->pane;

    // ----------------------------
    // -------- INITIALIZE --------
    // ----------------------------

    if (request == PANE_HANDLER_REQ_INITIALIZE) {
        vars = pane_cx->vars = calloc(1,sizeof(*vars));
        vars->y_scroll = 0;
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // ------------------------
    // -------- RENDER --------
    // ------------------------

    if (request == PANE_HANDLER_REQ_RENDER) {
        int32_t       i, j, count, x_left, y_top, units, x_title_str, color;
        int32_t       y_scroll_min, y_scroll_max, last_defined_scope;
        int32_t       y_axis_bottom;
        char          p[100], s1[100], s2[100], *select_str, *ymin_str, *ymax_str, *gl0_str, *gl1_str;
        char          *title_str, ymax_str2[100], ymin_str2[100], title_str_ext[100];
        long double   ymin, ymax;
        hist_t       *history0, *history1;
        gridloc_t     gl0, gl1;
        point_t       points[2*MAX_HISTORY];
        float         sign;

        #define YHEADER       50
        #define GRAPH_YSPAN   170
        #define GRAPH_YSPACE  30
        #define GRAPH_XSPAN   500

        // the graph xspan must equal MAX_HISTORY, because there
        // is currently no x-scaling done for the graphs
        if (GRAPH_XSPAN != MAX_HISTORY) {
            FATAL("GRAPH_XSPAN %d must equal MAX_HISTORY %d\n",
                  GRAPH_XSPAN, MAX_HISTORY);
        }

        // clear scope_select_enabled; it will be set below if there is a selected scope
        scope_select.enabled = false;

        // deterine the valid range for y_scroll, which depends on the 
        // last scope defined
        last_defined_scope = -1;
        for (i = 0; i < MAX_SCOPE; i++) {
            if (strncasecmp(param_str_val(PARAM_SCOPE_A+i), "off", 3) != 0) {
                last_defined_scope = i;
            }
        }
        y_scroll_min = 0;
        y_scroll_max = (YHEADER + (last_defined_scope + 1) * (GRAPH_YSPAN + GRAPH_YSPACE)) - pane->h;
        if (y_scroll_max < 0) y_scroll_max = 0;

        // register for mouse wheel event, and mouse motion event;
        // note that the registration for the mouse motion event needs to be done prior
        //      to the scope select event registration that is done at the end of the
        //      following loop; this is because the locations overlap and events that
        //      are defined later are given priority
        rect_t locf = {0,0,pane->w,pane->h};
        sdl_register_event(pane, &locf, SDL_EVENT_MOUSE_WHEEL2, SDL_EVENT_TYPE_MOUSE_WHEEL, pane_cx);
        sdl_register_event(pane, &locf, SDL_EVENT_MOUSE_MOTION2, SDL_EVENT_TYPE_MOUSE_MOTION, pane_cx);

        // loop over the scopes, displaying each
        for (i = 0; i < MAX_SCOPE; i++) {
            // if the model is reset then continue
            if (model_state == MODEL_STATE_RESET) {
                goto no_scope;
            }

            // init
            history0 = NULL;
            history1 = NULL;
            sign = 1;

            // parse and verify this scope's params; if invalid then continue, examples:
            // - off
            // - voltage,0v,5v,c3,c4,this-is-the-title
            // - current,-100ma,100ma,c3,c4,this-is-the-title
            strcpy(p, param_str_val(PARAM_SCOPE_A+i));
            select_str = strtok(p, ",");
            ymin_str = strtok(NULL, ",");
            ymax_str = strtok(NULL, ",");
            gl0_str = strtok(NULL, ",");
            gl1_str = strtok(NULL, ",");
            title_str = strtok(NULL, "");
            if (title_str == NULL) {
                goto no_scope;
            }
            if (strcasecmp(select_str,"voltage") != 0 && 
                strcasecmp(select_str,"current") != 0) 
            {
                goto no_scope;
            }
            units = (strcasecmp(select_str,"voltage") == 0) ? UNITS_VOLTS : UNITS_AMPS;
            if ((str_to_val(ymin_str, units, &ymin) < 0) ||
                (str_to_val(ymax_str, units, &ymax) < 0) ||
                (str_to_gridloc(gl0_str, &gl0) < 0) ||
                (grid[gl0.x][gl0.y].node == NULL) ||
                (str_to_gridloc(gl1_str, &gl1) < 0) ||
                (grid[gl1.x][gl1.y].node == NULL))
            {
                goto no_scope;
            }
            if (strcasecmp(select_str,"voltage") == 0) {
                history0 = grid[gl0.x][gl0.y].node->v_history;
                history1 = grid[gl1.x][gl1.y].node->v_history;
                sign = 1;
            } else {  // must be current
                // search for the component between gl0 and gl1
                grid_t *g;
                component_t *c;
                g = &grid[gl0.x][gl0.y];
                for (j = 0; j < g->max_term; j++) {
                    c = g->term[j]->component;
                    if (c->type == COMP_NONE || c->type == COMP_WIRE) {
                        continue;
                    }
                    if (memcmp(&c->term[0].gridloc, &gl0, sizeof(gridloc_t)) == 0 && 
                        memcmp(&c->term[1].gridloc, &gl1, sizeof(gridloc_t)) == 0) 
                    {
                        history0 = c->i_history;
                        sign = 1;
                        break;
                    } else if (memcmp(&c->term[0].gridloc, &gl1, sizeof(gridloc_t)) == 0 && 
                               memcmp(&c->term[1].gridloc, &gl0, sizeof(gridloc_t)) == 0) 
                    {
                        history0 = c->i_history;
                        sign = -1;
                        break;
                    }
                }
                if (j == g->max_term) {
                    // component not found between gl0 and gl1
                    goto no_scope;
                }
            }

            // if this is the selected scope then 
            //   display it in BLUE
            //   set scope_select global so that pane_hndlr_schematic() knows 
            //    which components to also display in BLUE
            // else
            //   display the scope in BLACK
            // endif
            // note that scope_select_idx==0 means not defined, 1==scope_a, etc
            if (i == scope_select_idx-1) {
                color = BLUE;
                scope_select.gl0 = gl0;
                scope_select.gl1 = gl1;
                scope_select.enabled = true;
            } else {
                color = BLACK;
            }
                
            // limit the y_scroll value so the vertical scroll range
            // encompasses just the defined scopes
            if (vars->y_scroll < y_scroll_min) {
                vars->y_scroll = y_scroll_min;
            } else if (vars->y_scroll > y_scroll_max) {
                vars->y_scroll = y_scroll_max;
            }

            // determine the coordinates of the top left corner of the scope graph
            x_left = 12;
            y_top  = YHEADER + i * (GRAPH_YSPAN + GRAPH_YSPACE) - vars->y_scroll;

            // if this scope is above the top of the pane or 
            // is below the bottom of the pane then skip to the next scope
            if (y_top + GRAPH_YSPAN < 0 || y_top > pane->h) {
                continue;
            }

            // display y axis
            // note - the reason for limitting y_axis_bottom to pane boundary is due to 
            //        a limitiation in util_sdl.c, where lines that are attempted to be 
            //        drawn from inside to outside pane boundary are not drawn
            y_axis_bottom = y_top + GRAPH_YSPAN - 1;
            if (y_axis_bottom > pane->h-1) y_axis_bottom = pane->h-1;
            sdl_render_line(pane, x_left, y_top, x_left, y_axis_bottom, color);
            sdl_render_line(pane, x_left-1, y_top, x_left-1, y_axis_bottom, color);

            // display x axis, at the 0 volt y intercept; 
            // if there is no 0 volt y intercept then don't display 
            // the x axis
            if (ymin <= 0 && ymax >= 0) {
                int32_t y = y_top + ymax / (ymax - ymin) * GRAPH_YSPAN;
                sdl_render_line(pane, x_left, y, x_left + GRAPH_XSPAN - 1, y, color);
                sdl_render_line(pane, x_left, y+1, x_left + GRAPH_XSPAN - 1, y+1, color);
            }

            // create array of points 
            count = 0;
            for (j = 0; j < max_history; j++) {
                float va=0, vb=0;
                int32_t ya,yb;

                // if history1 is NULL then
                //   we are displaying current data, set va and vb to the range of current values
                // else
                //   we are displaying voltage data between two nodes; set va and vb to 
                //    represent the range of difference between the two nodes; as seen below
                //    there are 2 approaches, I had hoped to use the first approach but the
                //    2nd produces better graphs in some cases
                // endif
                if (history1 == NULL) {
                    va = sign * history0[j].max;
                    vb = sign * history0[j].min;
                } else {
#if 0
                    va = sign * (history0[j].max - history1[j].min);
                    vb = sign * (history0[j].min - history1[j].max);
#else
                    va = sign * (history0[j].max - history1[j].max);
                    vb = sign * (history0[j].min - history1[j].min);
#endif
                }

                // if either va or vb is not-a-number that means there is no value to be
                // displayed for this scope 'x' coordinate; so continue
                if (isnan(va) || isnan(vb)) {
                    continue;
                }

                // limit the value range (va..vb) to be displayed at this x coord to
                // the scope value range, ymin..ymax
                if (va > ymax) va = ymax; else if (va < ymin) va = ymin;
                if (vb > ymax) vb = ymax; else if (vb < ymin) vb = ymin;

                // convert the value range (va..vb) to y coord range (ya..yb)
                ya = y_top + (ymax - va) / (ymax - ymin) * GRAPH_YSPAN;
                yb = y_top + (ymax - vb) / (ymax - ymin) * GRAPH_YSPAN;

                // add the j,ya and j,yb points to the array of points that
                // will be rendered as a line by code following this loop
                points[count].x = x_left + j;
                points[count].y = ya;
                count++;
                if (ya != yb) {
                    points[count].x = x_left + j;
                    points[count].y = yb;
                    count++;
                }
            }

            // display the graph
            sdl_render_lines(pane, points, count, color);

            // display the graph title
            sprintf(title_str_ext, "%c: %s - %s", 
                    'A' + i,
                    units == UNITS_VOLTS ? "VOLTAGE" : "CURRENT", 
                    title_str);
            x_title_str = x_left + GRAPH_XSPAN/2 - COL2X(strlen(title_str_ext),FPSZ_SMALL)/2;
            if (x_title_str < x_left) x_title_str = x_left;
            sdl_render_printf(pane, x_title_str, y_top-FPSZ_SMALL, FPSZ_SMALL, color, WHITE, "%s", title_str_ext);

            // display y axis units
            val_to_str(ymax, units, ymax_str2, true);
            val_to_str(ymin, units, ymin_str2, true);
            sdl_render_printf(pane, x_left+1, y_top, FPSZ_SMALLER, color, WHITE, "%s", ymax_str2);
            sdl_render_printf(pane, x_left+1, y_top+GRAPH_YSPAN-FPSZ_SMALLER-1, FPSZ_SMALLER, color, WHITE, "%s", ymin_str2);

            // register the scope select events; if a scope is clicked then it is 
            // selected and will be displayed in BLUE, and the associated gridlocs and
            // component will also be displayed in BLUE
            rect_t loc = { x_left, y_top, GRAPH_XSPAN, GRAPH_YSPAN };
            sdl_register_event(pane, 
                               &loc, 
                               SDL_EVENT_SCOPE_SELECT_A + i,
                               SDL_EVENT_TYPE_MOUSE_CLICK,
                               pane_cx);

            // done
            continue;

no_scope:   // scope is now off or improperly defined
            if (i == scope_select_idx-1) {
                scope_select_idx = 0;
            }
        }

        // display header line
        rect_t loc = {0, 0, pane->w, sdl_font_char_height(FPSZ_MEDIUM)};
        sdl_render_fill_rect(pane, &loc, WHITE);
        sdl_render_printf(pane, 0, 0, FPSZ_MEDIUM, BLACK, WHITE, 
                          "%s  SPAN=%s",
                          val_to_str(history_t, UNITS_SECONDS, s1, true),
                          val_to_str(param_num_val(PARAM_SCOPE_SPAN_T), UNITS_SECONDS, s2, true));

        // scope trigger control
        // - display MODE button
        sdl_render_text_and_register_event(
            pane, pane->w-COL2X(9,FPSZ_MEDIUM), 0, FPSZ_MEDIUM, "MODE", LIGHT_BLUE, WHITE,
            SDL_EVENT_SCOPE_MODE, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
        if (strcasecmp(param_str_val(PARAM_SCOPE_MODE), "continuous") == 0) {
            sdl_render_printf(
               pane, pane->w-COL2X(4,FPSZ_MEDIUM), 0, 
               FPSZ_MEDIUM, BLACK, WHITE, "CONT");
        } else {
            sdl_render_text_and_register_event(
                pane, pane->w-COL2X(4,FPSZ_MEDIUM), 0, FPSZ_MEDIUM, "TRIG", LIGHT_BLUE, WHITE,
                SDL_EVENT_SCOPE_TRIG, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
        }

        return PANE_HANDLER_RET_NO_ACTION;
    }

    // -----------------------
    // -------- EVENT --------
    // -----------------------

    if (request == PANE_HANDLER_REQ_EVENT) {
        switch(event->event_id) {
        case SDL_EVENT_SCOPE_MODE: {
            bool continuous = (strcasecmp(param_str_val(PARAM_SCOPE_MODE),"continuous") == 0);
            param_set(PARAM_SCOPE_MODE, continuous ? "trigger" : "continuous");
            break; }
        case SDL_EVENT_SCOPE_TRIG:
            param_set(PARAM_SCOPE_TRIGGER, "1");
            break;
        case SDL_EVENT_SCOPE_SELECT_A...SDL_EVENT_SCOPE_SELECT_A+MAX_SCOPE-1: {
            int32_t i = event->event_id - SDL_EVENT_SCOPE_SELECT_A + 1;
            scope_select_idx = (i == scope_select_idx ? 0 : i);
            break; }
        case SDL_EVENT_MOUSE_WHEEL2:
            vars->y_scroll -= event->mouse_wheel.delta_y * 20;
            break;
        case SDL_EVENT_MOUSE_MOTION2:
            vars->y_scroll -= event->mouse_motion.delta_y;
            break;
        }
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // ---------------------------
    // -------- TERMINATE --------
    // ---------------------------

    if (request == PANE_HANDLER_REQ_TERMINATE) {
        free(vars);
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // not reached
    assert(0);
    return PANE_HANDLER_RET_NO_ACTION;
}

