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

static void read_cmds_from_file(char *filename);
static void help(void);
static void * cli_thread(void * cx);
static void process_cmd(char * cmdline);
static int32_t pane_hndlr_schematic(pane_cx_t * pane_cx, int32_t request, void * init, sdl_event_t * event);

// -----------------  MAIN  -----------------------------------------------

int32_t main(int32_t argc, char ** argv)
{
    int32_t win_width, win_height;
    pthread_t thread_id;

    // get and process options
    // -f <file> : read commands from file
    while (true) {
        char opt_char = getopt(argc, argv, "f:h");
        if (opt_char == -1) {
            break;
        }
        switch (opt_char) {
        case 'f':
            read_cmds_from_file(optarg);
            break;
        case 'h':
            help();
            return 0;
        default:
            return 1;
            break;
        }
    }

    // XXX init component
    component[0].type = COMP_RESISTOR;
    component[0].term[0].component = &component[0];
    component[0].term[0].term_id   = 0;
    component[0].term[0].grid_y    = 1;
    component[0].term[0].grid_x    = 1;
    component[0].term[1].term_id   = 1;
    component[0].term[1].grid_y    = 1;
    component[0].term[1].grid_x    = 2;

    component[1].type = COMP_CAPACITOR;
    component[1].term[0].component = &component[0]; //xxx
    component[1].term[0].term_id   = 0;
    component[1].term[0].grid_y    = 1;
    component[1].term[0].grid_x    = 1;
    component[1].term[1].term_id   = 1;
    component[1].term[1].grid_y    = 1;
    component[1].term[1].grid_x    = 0;

    component[2].type = COMP_DIODE;
    component[2].term[0].component = &component[0]; //xxx
    component[2].term[0].term_id   = 0;
    component[2].term[0].grid_y    = 1;
    component[2].term[0].grid_x    = 1;
    component[2].term[1].term_id   = 1;
    component[2].term[1].grid_y    = 2;
    component[2].term[1].grid_x    = 1;

    component[3].type = COMP_OPEN_SWITCH;
    component[3].term[0].component = &component[0]; //xxx
    component[3].term[0].term_id   = 0;
    component[3].term[0].grid_y    = 3;
    component[3].term[0].grid_x    = 5;
    component[3].term[1].term_id   = 1;
    component[3].term[1].grid_y    = 3;
    component[3].term[1].grid_x    = 6;

    component[4].type = COMP_CLOSED_SWITCH;
    component[4].term[0].component = &component[0]; //xxx
    component[4].term[0].term_id   = 0;
    component[4].term[0].grid_y    = 3;
    component[4].term[0].grid_x    = 6;
    component[4].term[1].term_id   = 1;
    component[4].term[1].grid_y    = 3;
    component[4].term[1].grid_x    = 7;

    component[5].type = COMP_DC_POWER;
    component[5].term[0].component = &component[0]; //xxx
    component[5].term[0].term_id   = 0;
    component[5].term[0].grid_y    = 4;
    component[5].term[0].grid_x    = 4;
    component[5].term[1].term_id   = 1;
    component[5].term[1].grid_y    = 5;
    component[5].term[1].grid_x    = 4;

    component[6].type = COMP_WIRE;
    component[6].term[0].component = &component[0]; //xxx
    component[6].term[0].term_id   = 0;
    component[6].term[0].grid_y    = 5;
    component[6].term[0].grid_x    = 1;
    component[6].term[1].term_id   = 1;
    component[6].term[1].grid_y    = 5;
    component[6].term[1].grid_x    = 6;

    max_component = 7;



    // create thread for cli
    pthread_create(&thread_id, NULL, cli_thread, NULL);

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

    // done
    return 0;
}

static void read_cmds_from_file(char *filename)
{
    FILE * fp;
    char s[200];

    fp = fopen(filename, "r");
    if (fp == NULL) {
        FATAL("failed to open %s, %s\n", filename, strerror(errno));
    }

    memset(s,0,sizeof(s));
    while (fgets(s, sizeof(s), fp) != NULL) {
        process_cmd(s);
    }

    fclose(fp);
}

static void help(void)
{
    INFO("HELP TBD\n");
    exit(0);
}

// -----------------  CLI THREAD  -----------------------------------------

static void * cli_thread(void * cx)
{
    char s[200];

    memset(s,0,sizeof(s));
    while (fgets(s, sizeof(s), stdin) != NULL) {
        process_cmd(s);
    }

    // xxx this may not be a proper way to exit
    exit(0);
}

#if 0
clear
read
save

add A1 A2 resistor 10000
add A1 A2 power  dc  5
add A1 A2 switch open
add A1 A5 wire
del A1 A2

reset
run
stop

open and close switches
change values of components
#endif

static void process_cmd(char * cmdline)
{
    char *comment_char;
    char *cmd, *arg1, *arg2, *arg3;

    // terminate cmdline at the comment ('#') character, if any
    comment_char = strchr(cmdline,'#');
    if (comment_char) {
        *comment_char = '\0';
    }

    // tokenize
    cmd = strtok(cmdline, " \n");
    arg1 = strtok(NULL, " \n");
    arg2 = strtok(NULL, " \n");
    arg3 = strtok(NULL, " \n");

    // if no cmd then return
    if (cmd == NULL) {
        return;
    }

    // XXX for now just print them cmd
    printf("CMD '%s %s %s %s'\n", cmd, arg1, arg2, arg3);
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

    static component_image_t dc_power = 
        { { { {0,0}, {300,0}, {-1,-1} },
            { {300,0}, {358,142}, {500,200}, {642,142}, {700,0,}, {642,-142}, {500,-200}, {358,-142}, {300,0}, {-1,-1} },
            { {350,0}, {400, 0}, {-1,-1} },
            { {375,25}, {375, -25}, {-1,-1} },
            { {625,25}, {625, -25}, {-1,-1} },
            { {700,0}, {1000,0}, {-1,-1} },
            { {-1,-1} } } };

    static component_image_t * component_image[] = { &resistor_image, &capacitor_image, &diode_image ,
                                 &open_switch, &closed_switch, &dc_power };

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
        point_t points[26*50]; //xxx

        if (1) {
            // x labelling
            for (i = 0; i < MAX_GRID_X; i++) {
                x = i * vars->grid_scale + vars->grid_xoff - sdl_font_char_width(FONT_ID)/2;
                sdl_render_printf(pane, x, 0, FONT_ID, WHITE, BLACK, "%d", i);
            }
            // y labelling
            for (j = 0; j < MAX_GRID_Y; j++) {
                y = j * vars->grid_scale + vars->grid_yoff - sdl_font_char_height(FONT_ID)/2;
                sdl_render_printf(pane, 0, y, FONT_ID, WHITE, BLACK, "%c", 'A'+j);
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
            sdl_render_points(pane, points, count, WHITE, 2);
        }

        // draw components
        for (i = 0; i < max_component; i++) {
            component_t * c  = &component[i];

            switch (c->type) {
            case COMP_RESISTOR:
            case COMP_CAPACITOR:
            case COMP_DIODE:
            case COMP_OPEN_SWITCH:
            case COMP_CLOSED_SWITCH:
            case COMP_DC_POWER: {
                component_image_t * ci = component_image[c->type];
                x = c->term[0].grid_x * vars->grid_scale + vars->grid_xoff;
                y = c->term[0].grid_y * vars->grid_scale + vars->grid_yoff;
                for (j = 0; ci->points[j][0].x != -1; j++) {
                    for (k = 0; ci->points[j][k].x != -1; k++) {
                        if (c->term[1].grid_x == c->term[0].grid_x + 1) {
                            points[k].x = x + ci->points[j][k].x * vars->grid_scale / 1000;  // right
                            points[k].y = y + ci->points[j][k].y * vars->grid_scale / 1000;
                        } else if (c->term[1].grid_x == c->term[0].grid_x - 1) {
                            points[k].x = x - ci->points[j][k].x * vars->grid_scale / 1000;  // leftt
                            points[k].y = y + ci->points[j][k].y * vars->grid_scale / 1000;
                        } else if (c->term[1].grid_y == c->term[0].grid_y + 1) {
                            points[k].x = x + ci->points[j][k].y * vars->grid_scale / 1000;
                            points[k].y = y + ci->points[j][k].x * vars->grid_scale / 1000;  // down
                        } else if (c->term[1].grid_y == c->term[0].grid_y - 1) {
                            points[k].x = x + ci->points[j][k].y * vars->grid_scale / 1000;
                            points[k].y = y - ci->points[j][k].x * vars->grid_scale / 1000;  // up
                        } else {
                            FATAL("xxxx\n");
                        }
                    }
                    sdl_render_lines(pane, points, k, RED);
                }
                x = c->term[0].grid_x * vars->grid_scale + vars->grid_xoff;  // xxx need larger pt size
                y = c->term[0].grid_y * vars->grid_scale + vars->grid_yoff;
                sdl_render_point(pane, x, y, RED, 2);
                x = c->term[1].grid_x * vars->grid_scale + vars->grid_xoff;
                y = c->term[1].grid_y * vars->grid_scale + vars->grid_yoff;
                sdl_render_point(pane, x, y, RED, 2);
                break; }
            case COMP_WIRE: {
                int32_t x1 = c->term[0].grid_x * vars->grid_scale + vars->grid_xoff;
                int32_t y1 = c->term[0].grid_y * vars->grid_scale + vars->grid_yoff;
                int32_t x2 = c->term[1].grid_x * vars->grid_scale + vars->grid_xoff;
                int32_t y2 = c->term[1].grid_y * vars->grid_scale + vars->grid_yoff;
                sdl_render_line(pane, x1, y1, x2, y2, RED);
                break; }
            default:
                FATAL("xxx\n");
                break;
            }
        }





        // XXX comments
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
        case SDL_EVENT_MOUSE_WHEEL: {  // XXX ctrl wheel
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
