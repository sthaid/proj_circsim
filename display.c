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
#define PH_STATUS_H        100

#define PH_SCOPE_X         1375
#define PH_SCOPE_Y         100
#define PH_SCOPE_W         525  
#define PH_SCOPE_H         900

#define MIN_GRID_SCALE     100
#define MAX_GRID_SCALE     400

//
// typedefs
//

//
// variables
//

static pthread_mutex_t mutex;
static int32_t win_width, win_height;

static int32_t grid_xoff; 
static int32_t grid_yoff;
static long double  grid_scale;

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

        // determine grid_xoff, grid_yoff, grid_scale from
        // PARAM_CENTER, PARAM_SCALE
        while (true) { 
            gridloc_t gl;
            int32_t cnt, xadj, yadj;
            char *p, str[100];
            if ((cnt = sscanf(PARAM_VALUE(PARAM_SCALE), "%Lf", &grid_scale)) != 1 ||
                grid_scale > MAX_GRID_SCALE || grid_scale < MIN_GRID_SCALE) 
            {
                ERROR("invalid grid scale '%s'\n", PARAM_VALUE(PARAM_SCALE));
                if (cnt != 1) {
                    PARAM_SET_VALUE(PARAM_SCALE, DEFAULT_SCALE);
                } else if (grid_scale < MIN_GRID_SCALE) {
                    sprintf(str, "%d", MIN_GRID_SCALE);
                    PARAM_SET_VALUE(PARAM_SCALE, str);
                } else {
                    sprintf(str, "%d", MAX_GRID_SCALE);
                    PARAM_SET_VALUE(PARAM_SCALE, str);
                }
                continue;
            }

            xadj = yadj = 0;
            if (str_to_gridloc(PARAM_VALUE(PARAM_CENTER), &gl) < 0) {
                ERROR("invalid grid center loc '%s'\n", PARAM_VALUE(PARAM_CENTER));
                PARAM_SET_VALUE(PARAM_CENTER, DEFAULT_CENTER);
                continue;
            }
            if ((p = strchr(PARAM_VALUE(PARAM_CENTER), ',')) != NULL) {
                cnt = sscanf(p+1, "%d,%d", &xadj, &yadj);
                if (cnt != 2) {
                    ERROR("invalid grid center loc '%s'\n", PARAM_VALUE(PARAM_CENTER));
                    PARAM_SET_VALUE(PARAM_CENTER, DEFAULT_CENTER);
                    continue;
                }
            }
            grid_xoff = -grid_scale * gl.x + PH_SCHEMATIC_W/2 + xadj;
            grid_yoff = -grid_scale * gl.y + PH_SCHEMATIC_H/2 + yadj;

            break;
        }

        // initialize range of x,y that will be rendered by the 
        // the subsequent code; these variables are used by the OUT_OF_PANE macro
        x_min = -grid_scale;                  
        x_max = PH_SCHEMATIC_W + grid_scale; 
        y_min = -grid_scale;                
        y_max = PH_SCHEMATIC_H + grid_scale;

        // select font point size based on grid_scale
        fpsz = grid_scale * 40 / MAX_GRID_SCALE;

        // draw grid, if enabled
        if (strcmp(PARAM_VALUE(PARAM_GRID), "on") == 0) {
            int32_t glx, gly, x, y, count=0;
            point_t points[MAX_GRID_X*MAX_GRID_Y];

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
            sdl_render_points(pane, points, count, BLUE, 4);

            // draw gridloc_str at each grid point
            for (glx = 0; glx < MAX_GRID_X; glx++) {
                for (gly = 0; gly < MAX_GRID_Y; gly++) {
                    x = glx * grid_scale + grid_xoff + 2;
                    y = gly * grid_scale + grid_yoff - 2 - sdl_font_char_height(fpsz);
                    if (OUT_OF_PANE(x,y)) {
                        continue;
                    }
                    sdl_render_printf(pane, x, y, fpsz, BLUE, BLACK, "%s", grid[glx][gly].glstr);
                }
            }
        }

        // draw schematic components
        { int32_t i;
        for (i = 0; i < max_component; i++) {
            component_t * c  = &component[i];
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
                    sdl_render_line(pane, x1, y1, x2, y2, WHITE);
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
                    sdl_render_lines(pane, points, k, WHITE);
                }
                break; }
            case COMP_NONE:
                break;
            default:
                FATAL("invalid component type %d\n", c->type);
                break;
            }
        } }

        // draw a point at all grid locations that have at least one terminal as follows:
        // - ground      : GREEN
        // - remote_wire : the color assigned to the remote wire
        // - otherwise   : WHITE
        // if a grid location is both ground and remote-wire then alternate the color
        { int32_t glx, gly, color;
          static int32_t count;
          count++;
        for (glx = 0; glx < MAX_GRID_X; glx++) {
            for (gly = 0; gly < MAX_GRID_Y; gly++) {
                int32_t x, y;
                grid_t * g = &grid[glx][gly];

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
                                                                              : WHITE);
                sdl_render_point(pane, x, y, color, 4);
            }
        } }

        // display component id or value, if enabled
        if (strcmp(PARAM_VALUE(PARAM_COMPONENT), "id") == 0 ||
            strcmp(PARAM_VALUE(PARAM_COMPONENT), "value") == 0)
        {
            bool display_id = (strcmp(PARAM_VALUE(PARAM_COMPONENT), "id") == 0 );
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

                s = (display_id ? c->comp_str : component_to_value_str(c,s1));
                if (s[0] == '\0') {
                    continue;
                }

                // xxx use test/t9 and 'set component id'; long wires are not working
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
                    // this is for remote wire component id
                    x += 2;
                    y += 2;
                }
                sdl_render_printf(pane, x, y, fpsz, WHITE, BLACK, "%s", s);
            }
        } 

        // display the voltage at all node gridlocs;
        // note that this will intentionally overwrite the gridloc_str if PARAM_GRID is enabled
        if (strcmp(PARAM_VALUE(PARAM_VOLTAGE), "on") == 0) {
            int32_t i, j, x, y;
            component_t *c;
            terminal_t *term;
            node_t *n;
            char s[100];

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

                    val_to_str(n->v_current, UNITS_VOLTS, s);
                    sdl_render_printf(pane, x, y, fpsz, WHITE, BLACK, "%s", s);
                }
            }
        }

        // display current at all node terminals, if enabled
        // xxx if the term current dont match, display in red, and print a message
        if (strcmp(PARAM_VALUE(PARAM_CURRENT), "on") == 0) {
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

                current = c->i_current;
                val_to_str(fabsl(current), UNITS_AMPS, current_str);
                pre_str = "";
                post_str = "";

                if (c->term[1].gridloc.x == c->term[0].gridloc.x + 1) {         // right
                    if (current > 0) post_str = " >";
                    else if (current < 0) pre_str = "< ";
                    x += grid_scale / 2 - (strlen(current_str)+2) * sdl_font_char_width(fpsz) / 2;
                    y += 1 * sdl_font_char_height(fpsz);
                } else if (c->term[1].gridloc.x == c->term[0].gridloc.x - 1) {  // left
                    if (current > 0) pre_str = "< ";
                    else if (current < 0) post_str = " >";
                    x -= grid_scale / 2 + (strlen(current_str)+2) * sdl_font_char_width(fpsz) / 2;
                    y += 1 * sdl_font_char_height(fpsz);
                } else if (c->term[1].gridloc.y == c->term[0].gridloc.y + 1) {  // down 
                    if (current > 0) pre_str = "v ";
                    else if (current < 0) pre_str = "^ ";
                    x += 2 * sdl_font_char_width(fpsz);
                    y += grid_scale / 2 - sdl_font_char_height(fpsz) / 2 - 2 * sdl_font_char_height(fpsz);
                } else if (c->term[1].gridloc.y == c->term[0].gridloc.y - 1) {  // up
                    if (current > 0) pre_str = "^ ";
                    else if (current < 0) pre_str = "v ";
                    x += 2 * sdl_font_char_width(fpsz);
                    y -= grid_scale / 2 + sdl_font_char_height(fpsz) / 2 + 2 * sdl_font_char_height(fpsz);
                } else {
                    // COMP_POWER may or may not be adjacent
                    continue;
                }

                sdl_render_printf(pane, x, y, fpsz, WHITE, BLACK, "%s%s%s", pre_str, current_str, post_str);
            }
        }

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
            // the nearest gl 
            xoff = -grid_scale * gl.x + PH_SCHEMATIC_W/2;
            yoff = -grid_scale * gl.y + PH_SCHEMATIC_H/2;
            xadj = (new_grid_xoff - xoff);
            yadj = (new_grid_yoff - yoff);

            // update PARAM_CENTER, using gl,xadj,yadj;  which exactly specifies the center
            sprintf(param_center_str, "%s,%d,%d",
                    gridloc_to_str(&gl,str), xadj, yadj);
            PARAM_SET_VALUE(PARAM_CENTER, param_center_str);
            return PANE_HANDLER_RET_DISPLAY_REDRAW; }
        case SDL_EVENT_MOUSE_WHEEL: {
            long double new_grid_scale = 0;
            char param_scale_str[100];

            // determine new_grid_scale by multiplying or dividing the current
            // grid_scale by 1.1
            if (event->mouse_motion.delta_y > 0) {
                new_grid_scale = grid_scale * 1.1;
            } if (event->mouse_motion.delta_y < 0) {
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
            PARAM_SET_VALUE(PARAM_SCALE, param_scale_str);
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
    #define FPSZ_MEDIUM 30

    #define SDL_EVENT_MODEL_RESET  (SDL_EVENT_USER_DEFINED + 0)
    #define SDL_EVENT_MODEL_RUN    (SDL_EVENT_USER_DEFINED + 1)
    #define SDL_EVENT_MODEL_STOP   (SDL_EVENT_USER_DEFINED + 2)
    #define SDL_EVENT_MODEL_CONT   (SDL_EVENT_USER_DEFINED + 3)
    #define SDL_EVENT_MODEL_STEP   (SDL_EVENT_USER_DEFINED + 4)

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
        // xxx int32_t i;
        char s[100];

        // state
        sdl_render_printf(pane, 0, ROW2Y(0,FPSZ_MEDIUM), FPSZ_MEDIUM, WHITE, BLACK, 
                          "%-8s %s", 
                          MODEL_STATE_STR(model_state),
                          val_to_str(model_t, UNITS_SECONDS, s));

#if 0  //xxx
        // params
        for (i = 0; PARAM_NAME(i); i++) {
            sdl_render_printf(pane, 0, ROW2Y(3+i,FPSZ_MEDIUM), FPSZ_MEDIUM, WHITE, BLACK, 
                              "%-9s %s", PARAM_NAME(i), PARAM_VALUE(i));
        }
#endif

        sdl_render_text_and_register_event(
            pane, COL2X(0,FPSZ_MEDIUM), ROW2Y(1,FPSZ_MEDIUM), FPSZ_MEDIUM, "RESET", LIGHT_BLUE, BLACK,
            SDL_EVENT_MODEL_RESET, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
        switch (model_state) {
        case MODEL_STATE_RESET:
            sdl_render_text_and_register_event(
                pane, COL2X(9,FPSZ_MEDIUM), ROW2Y(1,FPSZ_MEDIUM), FPSZ_MEDIUM, "RUN", LIGHT_BLUE, BLACK,
                SDL_EVENT_MODEL_RUN, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
            break;
        case MODEL_STATE_RUNNING:
            sdl_render_text_and_register_event(
                pane, COL2X(9,FPSZ_MEDIUM), ROW2Y(1,FPSZ_MEDIUM), FPSZ_MEDIUM, "STOP", LIGHT_BLUE, BLACK,
                SDL_EVENT_MODEL_STOP, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
            break;
        case MODEL_STATE_STOPPED:
            sdl_render_text_and_register_event(
                pane, COL2X(9,FPSZ_MEDIUM), ROW2Y(1,FPSZ_MEDIUM), FPSZ_MEDIUM, "CONT", LIGHT_BLUE, BLACK,
                SDL_EVENT_MODEL_CONT, SDL_EVENT_TYPE_MOUSE_CLICK, pane_cx);
            break;
        }
        sdl_render_text_and_register_event(
            pane, COL2X(15,FPSZ_MEDIUM), ROW2Y(1,FPSZ_MEDIUM), FPSZ_MEDIUM, "STEP", LIGHT_BLUE, BLACK,
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
        int32_t       i, j, count, x_left, y_top;
        char          p[100], s[100], *select_str, *ymin_str, *ymax_str, *gl_str;
        long double   ymin, ymax;
        float       * history;
        gridloc_t     gl;
        point_t       points[MAX_HISTORY];

        #define GRAPH_YSPAN   180
        #define GRAPH_YSPACE  30
        #define GRAPH_XSPAN   500

        #define FPSZ_SMALL  24

        // the graph xspan must equal MAX_HISTORY, because there
        // is currently no x-scaling done for the graphs
        if (GRAPH_XSPAN != MAX_HISTORY) {
            FATAL("GRAPH_XSPAN %d must equal MAX_HISTORY %d\n",
                  GRAPH_XSPAN, MAX_HISTORY);
        }

        // display header line
        sdl_render_printf(pane, 0, 0, FPSZ_SMALL, WHITE, BLACK, 
                          "T=%s  T_SPAN=%s",
                          val_to_str(history_t, UNITS_SECONDS, s),
                          PARAM_VALUE(PARAM_SCOPE_T));

        // loop over the 4 scopes, displaying each
        for (i = 0; i < 4; i++) {
            // if the model is reset then continue
            if (model_state == MODEL_STATE_RESET) {
                continue;
            }

            // parse and verify this scope's params; if invalid then continue
            // examples:
            //   off
            //   voltage,0,5v,c3
            strcpy(p, PARAM_VALUE(PARAM_SCOPE_A+i));
            select_str = strtok(p, ",");
            if (strcmp(select_str, "off") == 0) {
                continue;
            }
            ymin_str = strtok(NULL, ",");
            ymax_str = strtok(NULL, ",");
            gl_str = strtok(NULL, ",");
            if (gl_str == NULL) {
                continue;
            }
            if (strcmp(select_str,"voltage") == 0) {
                if (str_to_val(ymin_str, UNITS_VOLTS, &ymin) < 0 ||
                    str_to_val(ymax_str, UNITS_VOLTS, &ymax) < 0 ||
                    str_to_gridloc(gl_str, &gl) < 0 ||
                    grid[gl.x][gl.y].node == NULL) 
                {
                    continue;
                }
                history = grid[gl.x][gl.y].node->v_history;
            } else if (strcmp(select_str,"current") == 0) {
                // XXX tbd
            } else {
                continue;
            }

            // determine the coordinates of the top left corner of the scope graph
            x_left = 12;
            y_top  = 50 + i * (GRAPH_YSPAN + GRAPH_YSPACE);

            // display y axis
            sdl_render_line(pane, x_left, y_top, x_left, y_top + GRAPH_YSPAN - 1, WHITE);

            // display x axis, at the 0 volt y intercept; 
            // if there is no 0 volt y intercept then don't display 
            // the x axis
            if (ymin <= 0 && ymax >= 0) {
                int32_t y = y_top + ymax / (ymax - ymin) * GRAPH_YSPAN;
                sdl_render_line(pane, x_left, y, x_left + GRAPH_XSPAN - 1, y, WHITE);
            }

            // create array of points 
            count = 0;
            for (j = 0; j < max_history; j++) {
                float v = history[j];
                if (v > ymax) {
                    v = ymax;
                } else if (v < ymin) {
                    v = ymin;
                }
                points[count].x = x_left + j;
                points[count].y = y_top + (ymax - v) / (ymax - ymin) * GRAPH_YSPAN;
                count++;
            }

            // display the graph
            sdl_render_lines(pane, points, count, WHITE);

            // display the graph title  xxx improve
            sdl_render_printf(pane, x_left + 10, y_top, FPSZ_SMALL, WHITE, BLACK, 
                              "%s",
                              PARAM_VALUE(PARAM_SCOPE_A+i));
        }

        return PANE_HANDLER_RET_NO_ACTION;
    }

    // -----------------------
    // -------- EVENT --------
    // -----------------------

    if (request == PANE_HANDLER_REQ_EVENT) {
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

