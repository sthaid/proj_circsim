#if 0

clear
read
save
add-resistor A1 A2 1000
add A1 A2 resistor 10000
add A1 A2 power    dc,5
add A1 A2 switch   open
add A1 A5 wire
del A1 A2
show

reset
run
stop
open and close switches
change values of components


// xxx don't know if I like all the time tags on the prints
#endif


#include "common.h"

#include <readline/readline.h>
#include <readline/history.h>


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

static void help(void);

static void * cli_thread(void * cx);
static int32_t process_cmd(char * cmdline);
static int32_t cmd_help(char *arg1, char *arg2, char *arg3, char *arg4);
static int32_t cmd_clear(char *arg1, char *arg2, char *arg3, char *arg4);
static int32_t cmd_read(char *filename, char *arg2, char *arg3, char *arg4);
static int32_t cmd_save(char *filename, char *arg2, char *arg3, char *arg4);
static int32_t cmd_add_resistor(char *loc0, char *loc1, char *ohms, char *arg4);
static int32_t cmd_add_capacitor(char *loc0, char *loc1, char *uF, char *arg4);
static int32_t cmd_show(char *arg1, char *arg2, char *arg3, char *arg4);
static int32_t add_component(int32_t type, char *loc0, char *loc1, char *valstr);
static char * term2locstr(terminal_t * term);

static int32_t pane_hndlr_schematic(pane_cx_t * pane_cx, int32_t request, void * init, sdl_event_t * event);

// -----------------  MAIN  -----------------------------------------------

int32_t main(int32_t argc, char ** argv)
{
    int32_t win_width, win_height;
    pthread_t thread_id;
    int32_t rc;

    // get and process options
    // -f <file> : read commands from file
    while (true) {
        char opt_char = getopt(argc, argv, "f:h");
        if (opt_char == -1) {
            break;
        }
        switch (opt_char) {
        case 'f':
            rc = cmd_read(optarg,NULL,NULL,NULL);
            if (rc < 0) {
                exit(1);
            }
            break;
        case 'h':
            help();
            return 0;
        default:
            return 1;
            break;
        }
    }

#if 0
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
#endif

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

static void help(void)
{
    INFO("HELP XXX TBD\n");
    exit(0);
}

// -----------------  CLI THREAD  -----------------------------------------

static void * cli_thread(void * cx)
{
    char *cmd_str = NULL;

    while (true) {
        free(cmd_str);
        if ((cmd_str = readline("> ")) == NULL) {
            break;
        }
        if (cmd_str[0] != '\0') {
            add_history(cmd_str);
        }

        process_cmd(cmd_str);
    }

    // xxx still not sure about this, may need to signal main thread
    exit(0);
}

static struct {
    char * name;
    int32_t (*proc)(char *arg1, char *arg2, char *arg3, char *arg4);
    int32_t min_args;
    int32_t max_args;
    char * usage;
} cmd_tbl[] = {
    { "help",          cmd_help,          0, 1, ""                     },
    { "clear",         cmd_clear,         0, 0, ""                     },
    { "read",          cmd_read,          1, 1, "<filename>"           },
    { "save",          cmd_save,          0, 1, "[<filename>]"         },
    { "add-resistor",  cmd_add_resistor,  3, 3, "<loc0> <loc1> <ohms>" },
    { "add-capacitor", cmd_add_capacitor, 3, 3, "<loc0> <loc1> <uF>"   },
    { "show"         , cmd_show         , 0, 0, ""                     },
                    };

#define MAX_CMD_TBL (sizeof(cmd_tbl) / sizeof(cmd_tbl[0]))

static int32_t process_cmd(char * cmdline)
{
    char *comment_char;
    char *cmd, *arg1, *arg2, *arg3, *arg4;  // xxx do we need arg4
    int32_t i, rc, arg_count=0;

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
    arg4 = strtok(NULL, " \n");
    if (arg1) arg_count++;
    if (arg2) arg_count++;
    if (arg3) arg_count++;
    if (arg4) arg_count++;

    // if no cmd then return success
    if (cmd == NULL) {
        return 0;
    }

    // find cmd in cmd_tbl
    //INFO("CMD '%s %s %s %s %s'\n", cmd, arg1, arg2, arg3, arg4);  // xxx temp
    for (i = 0; i < MAX_CMD_TBL; i++) {
        if (strcmp(cmd, cmd_tbl[i].name) == 0) {
            if (arg_count < cmd_tbl[i].min_args || arg_count > cmd_tbl[i].max_args) {
                ERROR("incorrect number of args\n");
                rc = -1;
            } else {
                rc = cmd_tbl[i].proc(arg1,arg2,arg3,arg4);
            }
            if (rc != 0) {
                ERROR("failed: '%s'\n", cmdline);
                return rc;
            }
            break;
        }
    } 
    if (i == MAX_CMD_TBL) {
        ERROR("not found: '%s'\n", cmdline);
        return rc;
    }

    // return success
    return 0;
}

static int32_t cmd_help(char *arg1, char *arg2, char *arg3, char *arg4)
{
    int32_t i;

    for (i = 0; i < MAX_CMD_TBL; i++) {
        INFO("%-16s %s\n", cmd_tbl[i].name, cmd_tbl[i].usage);
    }
    return 0;
}

static int32_t cmd_clear(char *arg1, char *arg2, char *arg3, char *arg4)
{
    max_component = 0;
    return 0;
}

static int32_t cmd_read(char *filename, char *arg2, char *arg3, char *arg4)
{
    FILE * fp;
    char s[200];
    int32_t rc;

    fp = fopen(filename, "r");
    if (fp == NULL) {
        ERROR("unable to open '%s', %s\n", filename, strerror(errno));
        return -1;
    }

    while (fgets(s, sizeof(s), fp) != NULL) {
        rc = process_cmd(s);
        if (rc != 0) {
// XXX print file line
            ERROR("aborting read of command from '%s'\n", filename);
            return rc;
        }
    }

    fclose(fp);
    return 0;
}

static int32_t cmd_save(char *filename, char *arg2, char *arg3, char *arg4)
{
    // xxx tbd
    return 0;
}

static int32_t cmd_add_resistor(char *loc0, char *loc1, char *ohms, char *arg4)
{
    return add_component(COMP_RESISTOR, loc0, loc1, ohms);
}

static int32_t cmd_add_capacitor(char *loc0, char *loc1, char *uF, char *arg4)
{
    // xxx tbd
    return 0;
}

static int32_t cmd_show(char *arg1, char *arg2, char *arg3, char *arg4)
{
    int32_t i;
    char *loc0, *loc1;

    for (i = 0; i < max_component; i++) {
        component_t * c = &component[i];
        if (c->type == COMP_NONE) {
            continue;
        }
        loc0 = term2locstr(&c->term[0]);
        loc1 = term2locstr(&c->term[1]);
        switch (c->type) {
        case COMP_RESISTOR:
            INFO("RESISTOR  %-3s %-3s ohms=%f\n",
                 loc0, loc1, c->resistor.ohms);
            break;
        }
    }
    return 0;
}

// XXXX component utils

static int32_t add_component(int32_t type, char *loc0, char *loc1, char *valstr)
{
    component_t * c = &component[max_component];

    // xxx since we're deleting components, could scan for an empty slot

    #define ADD_TERM(_id,_loc) \
        do { \
            int32_t grid_x=-1, grid_y=-1; \
            sscanf((_loc)+1, "%d", &grid_x); \
            grid_y = (_loc)[0] - 'A'; \
            if (grid_x < 0 || grid_x >= MAX_GRID_X || grid_y < 0 || grid_y >= MAX_GRID_Y) { \
                ERROR("invalid loc '%s'\n", (_loc)); \
                return -1; \
            } \
            c->term[_id].component = (c); \
            c->term[_id].id = (_id); \
            c->term[_id].grid_x = grid_x; \
            c->term[_id].grid_y = grid_y; \
        } while (0)

    // verify there is room for more components
    if (max_component == MAX_COMPONENT) {
        ERROR("too many components, max allowed = %d\n", MAX_COMPONENT);
        return -1;
    }
        
    // store the component type and terminal locations
    c->type = type;
    ADD_TERM(0,loc0);
    ADD_TERM(1,loc1);

    // xxx verify adjacnet loc

    // store component specific values
    switch (type) {
    case COMP_RESISTOR: {
        float ohms;
        if (valstr == NULL || sscanf(valstr, "%f", &ohms) != 1 || ohms <= 0) {
            ERROR("ohms '%s' invalid\n", valstr);
            return -1;
        }
        c->resistor.ohms = ohms;
        break; }
    default:
        FATAL("invalid type %d\n", type);
        break;
    }

    // commit the new component
    max_component++;

    // return success
    return 0;
}

static char * term2locstr(terminal_t * term)
{
    static char s[8][20];
    static int32_t static_idx;
    int32_t idx;

    idx = __sync_fetch_and_add(&static_idx,1) % 8;
    sprintf(s[idx], "%c%d", term->grid_y+'A', term->grid_x);
    return s[idx];
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

        if (1) {  // xxx grid on/off, cmd and/or click
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
            case COMP_CLOSED_SWITCH:  // xxx should be just one switch
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
