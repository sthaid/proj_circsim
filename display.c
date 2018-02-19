#include "common.h"

//
// defines
//

#define DEFAULT_WIN_WIDTH  1900
#define DEFAULT_WIN_HEIGHT 1000

#define PH_SCHEMATIC_W     1500
#define PH_SCHEMATIC_H     1000

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
static double  grid_scale;

//
// prototypes
//

static void display_start(void * cx);
static void display_end(void * cx);
static int32_t pane_hndlr_schematic(pane_cx_t * pane_cx, int32_t request, void * init, sdl_event_t * event);
static bool has_comp_power(grid_t * g);
static int32_t pane_hndlr_status(pane_cx_t * pane_cx, int32_t request, void * init, sdl_event_t * event);

// -----------------  PUBLIC  ---------------------------------------------

void display_init(void)
{
    pthread_mutexattr_t attr;

    pthread_mutexattr_init(&attr);
    pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&mutex, &attr);

    display_center();
}

void display_lock(void)
{
    pthread_mutex_lock(&mutex);
}

void display_unlock(void)
{
    pthread_mutex_unlock(&mutex);
}

int32_t display_center(void)
{
    int32_t rc;
    gridloc_t gl;
    double new_grid_scale;

    rc = str_to_gridloc(PARAM_CENTER, &gl);
    if (rc < 0) {
        ERROR("invalid grid center loc '%s'\n", PARAM_CENTER);
        return -1;
    }

    if (sscanf(PARAM_SCALE, "%lf", &new_grid_scale) != 1) {
        ERROR("invalid grid scale '%s'\n", PARAM_SCALE);
        return -1;
    }
    if (new_grid_scale < MIN_GRID_SCALE) new_grid_scale = MIN_GRID_SCALE;
    if (new_grid_scale > MAX_GRID_SCALE) new_grid_scale = MAX_GRID_SCALE;

    display_lock();
    grid_scale = new_grid_scale;
    grid_xoff = -grid_scale * gl.x + PH_SCHEMATIC_W/2;
    grid_yoff = -grid_scale * gl.y + PH_SCHEMATIC_H/2;
    display_unlock();

    return 0;
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
        2,              // number of pane handler varargs that follow
        pane_hndlr_schematic, NULL, 0,              0,  PH_SCHEMATIC_W, PH_SCHEMATIC_H, PANE_BORDER_STYLE_MINIMAL,
        pane_hndlr_status,    NULL, PH_SCHEMATIC_W, 0,  400,            400,            PANE_BORDER_STYLE_MINIMAL);
}

// -----------------  xxxxxxxxxxxx  ---------------------------------------

static void display_start(void * cx)
{
    display_lock();
}

static void display_end(void * cx)
{
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
        } points[20][20];
    } component_image_t;

    static component_image_t power_image =
        { { { {0,0}, {300,0}, {-1,-1} },
            { {300,0}, {358,142}, {500,200}, {642,142}, {700,0,}, {642,-142}, {500,-200}, {358,-142}, {300,0}, {-1,-1} },
            { {350,0}, {400, 0}, {-1,-1} },
            { {375,25}, {375, -25}, {-1,-1} },
            { {625,25}, {625, -25}, {-1,-1} },
            { {700,0}, {1000,0}, {-1,-1} },
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
        int32_t x_min = -grid_scale;                  // used by OUT_OF_PANE
        int32_t x_max = PH_SCHEMATIC_W + grid_scale;  //   '      '
        int32_t y_min = -grid_scale;                  //   '      '
        int32_t y_max = PH_SCHEMATIC_H + grid_scale;  //   '      '
        int32_t fpsz;

        // select font point size based on grid_scale
        fpsz = grid_scale * 40 / MAX_GRID_SCALE;

        // draw grid, if enabled
        if (strcmp(PARAM_GRID, "on") == 0) {
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
            case COMP_CONNECTION: {
                int32_t x1 = c->term[0].gridloc.x * grid_scale + grid_xoff;
                int32_t y1 = c->term[0].gridloc.y * grid_scale + grid_yoff;
                int32_t x2 = c->term[1].gridloc.x * grid_scale + grid_xoff;
                int32_t y2 = c->term[1].gridloc.y * grid_scale + grid_yoff;
                if (OUT_OF_PANE(x1,y1) || OUT_OF_PANE(x2,y2)) {
                    continue;
                }
                sdl_render_line(pane, x1, y1, x2, y2, WHITE);
                break; }
            case COMP_POWER:
            case COMP_RESISTOR:
            case COMP_CAPACITOR:
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
                            // COMP_POWER may or may not be adjacent
                            break;
                        }
                    }
                    if (k == 0) {
                        continue;
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
        // - just one terminal connected : YELLOW  (this is a warning)
        // - power                       : RED
        // - ground                      : GREEN
        // - otherwise                   : WHITE
        { int32_t glx, gly, color;
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

                color = (g->max_term == 1  ? YELLOW :
                         has_comp_power(g) ? RED :
                         g->ground         ? GREEN :
                                             WHITE);
                sdl_render_point(pane, x, y, color, 4);
            }
        } }

        // display component id or value, if enabled
        if (strcmp(PARAM_COMPONENT, "id") == 0 ||
            strcmp(PARAM_COMPONENT, "value") == 0)
        {
            bool display_id = (strcmp(PARAM_COMPONENT, "id") == 0 );
            int32_t i, x, y;
            char *s, s1[100];
            component_t *c;

            for (i = 0; i < max_component; i++) {
                c = &component[i];
                if (c->type == COMP_NONE || c->type == COMP_CONNECTION) {
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
                    x += 2;
                    y += 2;
                }
                sdl_render_printf(pane, x, y, fpsz, WHITE, BLACK, "%s", s);
            }
        } 

        // display the voltage at all node gridlocs;
        // note that this will intentionally overwrite the gridloc_str if PARAM_GRID is enabled
        if (strcmp(PARAM_VOLTAGE, "on") == 0) {
            int32_t i, j, x, y;
            component_t *c;
            terminal_t *term;
            node_t *n;
            char s[100];

            for (i = 0; i < max_component; i++) {
                c  = &component[i];
                if (c->type == COMP_NONE || c->type == COMP_CONNECTION) {
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

                    val_to_str(NODE_V_CURR(n), UNITS_VOLTS, s);
                    sdl_render_printf(pane, x, y, fpsz, WHITE, BLACK, "%s", s);
                }
            }
        }

        // display current at all node terminals, if enabled
        // XXX if the terms dont match, display in red, and print a message
        if (strcmp(PARAM_CURRENT, "on") == 0) {
            int32_t i, x, y;
            component_t *c;
            double current;
            char current_str[100], *pre_str, *post_str;

            for (i = 0; i < max_component; i++) {
                c  = &component[i];
                if (c->type == COMP_NONE || c->type == COMP_CONNECTION) {
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

                current = c->term[0].current;
                val_to_str(fabs(current), UNITS_AMPS, current_str);
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
        case SDL_EVENT_MOUSE_MOTION:
            grid_xoff += event->mouse_motion.delta_x;
            grid_yoff += event->mouse_motion.delta_y;
            return PANE_HANDLER_RET_DISPLAY_REDRAW;
        case SDL_EVENT_MOUSE_WHEEL: {
            if (event->mouse_motion.delta_y > 0 && grid_scale < MAX_GRID_SCALE) {
                grid_scale *= 1.1;
                grid_xoff = pane->w/2 - 1.1 * (pane->w/2 - grid_xoff);
                grid_yoff = pane->h/2 - 1.1 * (pane->h/2 - grid_yoff);
            }
            if (event->mouse_motion.delta_y < 0 && grid_scale > MIN_GRID_SCALE) {
                grid_scale *= (1./1.1);
                grid_xoff = pane->w/2 - (1./1.1) * (pane->w/2 - grid_xoff);
                grid_yoff = pane->h/2 - (1./1.1) * (pane->h/2 - grid_yoff);
            }
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

static bool has_comp_power(grid_t * g)
{
    int32_t i;

    for (i = 0; i < g->max_term; i++) {
        terminal_t *t = g->term[i];
        if (t->termid == 0 && t->component->type == COMP_POWER) {
            return true;
        }
    }
    return false;
}

// -----------------  PANE HNDLR STATUS  ----------------------------------

static int32_t pane_hndlr_status(pane_cx_t * pane_cx, int32_t request, void * init, sdl_event_t * event) 
{
    #define FPSZ_MEDIUM 30

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
        int32_t i;

        // state
        sdl_render_printf(pane, 0, ROW2Y(0,FPSZ_MEDIUM), FPSZ_MEDIUM, WHITE, BLACK, 
                          "%s", MODEL_STATE_STR(model_state));
        sdl_render_printf(pane, 0, ROW2Y(1,FPSZ_MEDIUM), FPSZ_MEDIUM, WHITE, BLACK, 
                          "%0.6f SECS", model_time);

        // params
        for (i = 0; params_tbl[i].name; i++) {
            sdl_render_printf(pane, 0, ROW2Y(3+i,FPSZ_MEDIUM), FPSZ_MEDIUM, WHITE, BLACK, 
                              "%-12s %s",
                              params_tbl[i].name,
                              params_tbl[i].value);
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
