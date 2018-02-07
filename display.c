#include "common.h"

//
// defines
//

#define DEFAULT_WIN_WIDTH  1900
#define DEFAULT_WIN_HEIGHT 1000

//
// typedefs
//

//
// variables
//

//
// prototypes
//

static int32_t pane_hndlr_schematic(pane_cx_t * pane_cx, int32_t request, void * init, sdl_event_t * event);

/* XXX TODO
display()
{
    // display all components

    // display dots for all grid locations with connections
    // - green dots for locations that are ground
    // - white dots otherwise

    // display the voltages for every grid location with connections
}

display_params()
{
    // time
    // delta_time
    // ground loc
}

display_graph()
{
}
*/

// -----------------  PUBLIC  ---------------------------------------------

void display_handler(void)
{
    int32_t win_width, win_height;

    // use sdl to display the schematic
    win_width  = DEFAULT_WIN_WIDTH;
    win_height = DEFAULT_WIN_HEIGHT;
    if (sdl_init(&win_width, &win_height, true, false) < 0) {
        FATAL("sdl_init %dx%d failed\n", win_width, win_height);
    }
    sdl_pane_manager(
        NULL,           // context
        NULL,           // called prior to pane handlers
        NULL,           // called after pane handlers
        50000,          // 0=continuous, -1=never, else us 
        1,              // number of pane handler varargs that follow
        pane_hndlr_schematic, NULL, 0, 0, 1600, 1000, PANE_BORDER_STYLE_MINIMAL);
}

// -----------------  PANE HANDLERS  --------------------------------------

static int32_t pane_hndlr_schematic(pane_cx_t * pane_cx, int32_t request, void * init, sdl_event_t * event) 
{
    #define SDL_EVENT_MOUSE_MOTION  (SDL_EVENT_USER_DEFINED + 0)
    #define SDL_EVENT_MOUSE_WHEEL   (SDL_EVENT_USER_DEFINED + 1)

    #define DEFAULT_GRID_XOFF  100
    #define DEFAULT_GRID_YOFF  100
    #define MIN_GRID_XOFF      (pane->w - ((MAX_GRID_X-1) * vars->grid_scale) - 100)
    #define MAX_GRID_XOFF      100
    #define MIN_GRID_YOFF      (pane->h - ((MAX_GRID_Y-1) * vars->grid_scale) - 100)
    #define MAX_GRID_YOFF      100

    #define MIN_GRID_SCALE     100
    #define MAX_GRID_SCALE     400

    #define FONT_ID 1

    typedef struct {
        struct {
            int32_t x;
            int32_t y;
        } points[20][20];
    } component_image_t;

    static component_image_t dcpower_image = 
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

#if 0
    static component_image_t open_switch = 
        { { { {0,0}, {350,0}, {-1,-1} },
            { {350,0}, {560,-160}, {-1,-1} },
            { {650,0}, {1000,0}, {-1,-1} },
            { {-1,-1} } } };
    static component_image_t closed_switch = 
        { { { {0,0}, {350,0}, {-1,-1} },
            { {350,0}, {640,-20}, {-1,-1} },
            { {650,0}, {1000,0}, {-1,-1} },
            { {-1,-1} } } };
#endif

    static component_image_t * component_image[] = {  // xxx better way of selecting these
            NULL, 
            NULL, 
            &dcpower_image,
            &resistor_image, 
            &capacitor_image, 
            &diode_image };

    struct {
        int32_t grid_xoff;  // xxx name
        int32_t grid_yoff;
        float   grid_scale;
    } * vars = pane_cx->vars;
    rect_t * pane = &pane_cx->pane;

    // ----------------------------
    // -------- INITIALIZE --------
    // ----------------------------

    if (request == PANE_HANDLER_REQ_INITIALIZE) {
        vars = pane_cx->vars = calloc(1,sizeof(*vars));
        vars->grid_xoff = 75;
        vars->grid_yoff = 75;
        vars->grid_scale = 200.;
        return PANE_HANDLER_RET_NO_ACTION;
    }

    // ------------------------
    // -------- RENDER --------
    // ------------------------

    if (request == PANE_HANDLER_REQ_RENDER) {
        // if grid is enabled then draw it
        int32_t i, j, k, x, y, count;
        point_t points[MAX_GRID_X*MAX_GRID_Y];

        // draw grid, if enabled  xxx on/off
        if (1) {
            // x labelling
            for (i = 0; i < MAX_GRID_X; i++) {
                x = i * vars->grid_scale + vars->grid_xoff - sdl_font_char_width(FONT_ID)/2;
                sdl_render_printf(pane, x, 0, FONT_ID, BLUE, BLACK, "%d", i);
            }
            // y labelling
            for (j = 0; j < MAX_GRID_Y; j++) {
                y = j * vars->grid_scale + vars->grid_yoff - sdl_font_char_height(FONT_ID)/2;
                sdl_render_printf(pane, 0, y, FONT_ID, BLUE, BLACK, "%c", 'A'+j);
            }
            // points
            count = 0;
            for (i = 0; i < MAX_GRID_X; i++) {
                for (j = 0; j < MAX_GRID_Y; j++) {
                    x = i * vars->grid_scale + vars->grid_xoff;
                    y = j * vars->grid_scale + vars->grid_yoff;
                    points[count].x = x;
                    points[count].y = y;
                    count++;
                }
            }
            sdl_render_points(pane, points, count, BLUE, 2);
        }

        // draw components
        for (i = 0; i < max_component; i++) {
            component_t * c  = &component[i];

            switch (c->type) {
            case COMP_CONNECTION: {
                int32_t x1 = c->term[0].gridloc.x * vars->grid_scale + vars->grid_xoff;
                int32_t y1 = c->term[0].gridloc.y * vars->grid_scale + vars->grid_yoff;
                int32_t x2 = c->term[1].gridloc.x * vars->grid_scale + vars->grid_xoff;
                int32_t y2 = c->term[1].gridloc.y * vars->grid_scale + vars->grid_yoff;
                sdl_render_line(pane, x1, y1, x2, y2, WHITE);
                break; }
            case COMP_DCPOWER:
            case COMP_RESISTOR:
            case COMP_CAPACITOR:
            case COMP_DIODE: {
                component_image_t * ci = component_image[c->type];
                x = c->term[0].gridloc.x * vars->grid_scale + vars->grid_xoff;
                y = c->term[0].gridloc.y * vars->grid_scale + vars->grid_yoff;
                for (j = 0; ci->points[j][0].x != -1; j++) {
                    for (k = 0; ci->points[j][k].x != -1; k++) {
                        if (c->term[1].gridloc.x == c->term[0].gridloc.x + 1) {
                            points[k].x = x + ci->points[j][k].x * vars->grid_scale / 1000;  // right
                            points[k].y = y + ci->points[j][k].y * vars->grid_scale / 1000;
                        } else if (c->term[1].gridloc.x == c->term[0].gridloc.x - 1) {
                            points[k].x = x - ci->points[j][k].x * vars->grid_scale / 1000;  // leftt
                            points[k].y = y + ci->points[j][k].y * vars->grid_scale / 1000;
                        } else if (c->term[1].gridloc.y == c->term[0].gridloc.y + 1) {
                            points[k].x = x + ci->points[j][k].y * vars->grid_scale / 1000;  // down
                            points[k].y = y + ci->points[j][k].x * vars->grid_scale / 1000; 
                        } else if (c->term[1].gridloc.y == c->term[0].gridloc.y - 1) {
                            points[k].x = x + ci->points[j][k].y * vars->grid_scale / 1000;  // up
                            points[k].y = y - ci->points[j][k].x * vars->grid_scale / 1000; 
                        } else {
                            FATAL("xxxx\n");
                        }
                    }
                    sdl_render_lines(pane, points, k, WHITE);
                }
                break; }
            case COMP_NONE:
                break;
            default:
                FATAL("xxx\n");
                break;
            }
        }

        // XXX omment
        int32_t glx, gly, color;
        for (glx = 0; glx < MAX_GRID_X; glx++) {
            for (gly = 0; gly < MAX_GRID_Y; gly++) {
                grid_t * g = &grid[glx][gly];
                if (g->max_term == 0) {
                    continue;
                }
                x = glx * vars->grid_scale + vars->grid_xoff;  // xxx need larger pt size
                y = gly * vars->grid_scale + vars->grid_yoff;
                color = (g->max_term == 1 ? RED :
                         g->ground        ? GREEN :
                                            WHITE);
                sdl_render_point(pane, x, y, color, 2);
            }
        }

        // xxx comments
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
            vars->grid_xoff += event->mouse_motion.delta_x;
            vars->grid_yoff += event->mouse_motion.delta_y;
            if (vars->grid_xoff < MIN_GRID_XOFF) vars->grid_xoff = MIN_GRID_XOFF;
            if (vars->grid_xoff > MAX_GRID_XOFF) vars->grid_xoff = MAX_GRID_XOFF;
            if (vars->grid_yoff < MIN_GRID_YOFF) vars->grid_yoff = MIN_GRID_YOFF;
            if (vars->grid_yoff > MAX_GRID_YOFF) vars->grid_yoff = MAX_GRID_YOFF;
            return PANE_HANDLER_RET_DISPLAY_REDRAW;
        case SDL_EVENT_MOUSE_WHEEL: {  // xxx ctrl wheel
            if (event->mouse_motion.delta_y > 0 && vars->grid_scale < MAX_GRID_SCALE) {
                vars->grid_scale *= 1.1;
                vars->grid_xoff = pane->w/2 - 1.1 * (pane->w/2 - vars->grid_xoff);
                vars->grid_yoff = pane->h/2 - 1.1 * (pane->h/2 - vars->grid_yoff);
            }
            if (event->mouse_motion.delta_y < 0 && vars->grid_scale > MIN_GRID_SCALE) {
                vars->grid_scale *= (1./1.1);
                vars->grid_xoff = pane->w/2 - (1./1.1) * (pane->w/2 - vars->grid_xoff);
                vars->grid_yoff = pane->h/2 - (1./1.1) * (pane->h/2 - vars->grid_yoff);
            }
            if (vars->grid_xoff < MIN_GRID_XOFF) vars->grid_xoff = MIN_GRID_XOFF;
            if (vars->grid_xoff > MAX_GRID_XOFF) vars->grid_xoff = MAX_GRID_XOFF;
            if (vars->grid_yoff < MIN_GRID_YOFF) vars->grid_yoff = MIN_GRID_YOFF;
            if (vars->grid_yoff > MAX_GRID_YOFF) vars->grid_yoff = MAX_GRID_YOFF;
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
