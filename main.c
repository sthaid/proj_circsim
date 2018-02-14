#define MAIN
#include "common.h"

//
// defines
//

#define MB 0x100000

#define DEFAULT_WIN_WIDTH  1900
#define DEFAULT_WIN_HEIGHT 1000

//
// typedefs
//

//
// variables
//

static char last_filename_used[200];

//
// prototypes
//

static void help(void);

static void * cli_thread(void * cx);
static int32_t process_cmd(char * cmdline);
static int32_t cmd_help(char *args);
static int32_t cmd_set(char *args);
static int32_t cmd_show(char *args);
static int32_t cmd_center(char *args);
static int32_t cmd_clear_schematic(char *args);
static int32_t cmd_read(char *args);
static int32_t cmd_write(char *args);
static int32_t cmd_add(char *args);
static int32_t cmd_del(char *args);
static int32_t cmd_ground(char *args);
static int32_t cmd_sim(char *args);

static int32_t add_component(char *type_str, char *gl0_str, char *gl1_str, char *value_str);
static int32_t del_component(char * compid_str);

static char * make_component_str(component_t * c);
static void identify_grid_ground(gridloc_t *gl);

// -----------------  MAIN  -----------------------------------------------

int32_t main(int32_t argc, char ** argv)
{
    pthread_t thread_id;
    int32_t rc;

    // xxx
    INFO("sizeof(component) = %ld MB\n", sizeof(component)/MB);
    INFO("sizeof(grid)      = %ld MB\n", sizeof(grid)/MB);
    INFO("sizeof(node)      = %ld MB\n", sizeof(node)/MB);

    // call initialization routines
    display_init();
    circsim_init();

    // get and process options
    // -f <file> : read commands from file
    while (true) {
        char opt_char = getopt(argc, argv, "f:h");
        if (opt_char == -1) {
            break;
        }
        switch (opt_char) {
        case 'f':
            rc = cmd_read(optarg);
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

    // create thread for cli
    pthread_create(&thread_id, NULL, cli_thread, NULL);

    // call display handler
    display_handler();

    // done
    return 0;
}

static void help(void)
{
    INFO("HELP xxx TBD\n");
    exit(0);
}

// -----------------  CLI THREAD  -----------------------------------------

static void * cli_thread(void * cx)
{
    char *cmd_str = NULL;
    sdl_event_t event;

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

    // exit must come from the util_sdl display_handler; 
    // so inject the SDL_EVENT_QUIT and pause
    memset(&event,0,sizeof(event));
    event.event_id = SDL_EVENT_QUIT;
    sdl_push_event(&event);
    while (true) {
        pause();
    }
    return NULL;
}

static struct {
    char * name;
    int32_t (*proc)(char *args);
    char * usage;
} cmd_tbl[] = {
    { "help",            cmd_help,            ""                                 },

    { "set",             cmd_set,             "<param_name> <param_value>"       },
    { "show",            cmd_show,            "[<components|params|ground>]"     },
    { "center",          cmd_center,          ""                                 },

    { "clear_schematic", cmd_clear_schematic, "clear schematic"                  },
    { "read",            cmd_read,            "<filename>"                       },
    { "write",           cmd_write,           "[<filename>]"                     },
    { "add",             cmd_add,             "<type> <gl0> <gl1> <value,...>"   },
    { "del",             cmd_del,             "<compid>"                         },
    { "ground",          cmd_ground,          "<gl>"                             },

    { "sim",             cmd_sim,             "<reset|run|pause|cont>"           },
                    };

#define MAX_CMD_TBL (sizeof(cmd_tbl) / sizeof(cmd_tbl[0]))

static int32_t process_cmd(char * cmdline)
{
    char *comment_char;
    char *cmd, *args;
    int32_t i, rc;

    // terminate cmdline at the comment ('#') character, if any
    comment_char = strchr(cmdline,'#');
    if (comment_char) {
        *comment_char = '\0';
    }

    // tokenize
    cmd = strtok(cmdline, " \n");
    args = strtok(NULL, "\n");

    // if no cmd then return success
    if (cmd == NULL) {
        return 0;
    }

    // find cmd in cmd_tbl
    for (i = 0; i < MAX_CMD_TBL; i++) {
        if (strcasecmp(cmd, cmd_tbl[i].name) == 0) {
            display_lock();
            rc = cmd_tbl[i].proc(args);
            display_unlock();
            if (rc < 0) {
                ERROR("failed: '%s'\n", cmdline);  // xxx make a copy of cmdline, strtok clobbered it
                return -1;
            }
            break;
        }
    } 
    if (i == MAX_CMD_TBL) {
        ERROR("not found: '%s'\n", cmdline);
        return -1;
    }

    // return success
    return 0;
}

// XXX check all of these cmds for null args
static int32_t cmd_help(char *args)
{
    int32_t i;

    for (i = 0; i < MAX_CMD_TBL; i++) {
        INFO("%-16s %s\n", cmd_tbl[i].name, cmd_tbl[i].usage);
    }
    return 0;
}

static int32_t cmd_set(char *args)
{
    char *name, *value;
    int32_t i;

    name = strtok(args, " ");
    value = strtok(NULL, "");

    // try to find name in params_tbl; 
    // if found then set the param
    for (i = 0; params_tbl[i].name; i++) {
        if (strcasecmp(name, params_tbl[i].name) == 0) {
            break;
        }
    }
    if (params_tbl[i].name == NULL) {
        ERROR("param '%s' not found\n", name);
        return -1;
    }

    // store new param value
    strcpy(params_tbl[i].value, value);
    return 0;
}

static int32_t cmd_show(char *args)
{
    int32_t i;
    bool show_all, printed=false;
    char *what;

    what = strtok(args, " ");

    show_all = (what == NULL);

    if (show_all || strcasecmp(what,"params") == 0) {
        INFO("PARAMS\n");
        for (i = 0; params_tbl[i].name; i++) {
            INFO("  %-8s %s",
                 params_tbl[i].name,
                 params_tbl[i].value);
        }
        BLANK_LINE;
        printed = true;
    }

    if (show_all || strcasecmp(what,"components") == 0) {
        INFO("COMPONENTS\n");
        for (i = 0; i < max_component; i++) {
            component_t * c = &component[i];
            if (c->type == COMP_NONE) {
                continue;
            }
            // XXX don't show compid
            INFO("  %-3d %s\n", i, make_component_str(c));
        }
        BLANK_LINE;
        printed = true;
    }

    if (show_all || strcasecmp(what,"ground") == 0) {
        INFO("GROUND\n");
        INFO("  ground %s\n", ground_is_set ? make_gridloc_str(&ground) : "NOT_SET");
        BLANK_LINE;
        printed = true;
    }

    if (printed == false) {
        ERROR("not supported '%s'\n", what);
    }

    return 0;
}

static int32_t cmd_center(char *args)
{
    return display_center();
}

static int32_t cmd_clear_schematic(char *args)                                        
{
    // reset circuit simulator
    circsim_cmd("reset");

    // remove all components
    max_component = 0;
    memset(component,0,sizeof(component));

    // remove ground
    memset(&ground, 0, sizeof(ground));
    ground_is_set = false;
    identify_grid_ground(NULL);

    // clear the grid
    memset(&grid, 0, sizeof(grid));

    // success
    return 0;
}

static int32_t cmd_read(char *args)
{
    FILE * fp;
    char s[200], *filename;
    int32_t rc, fileline=0;

    filename = strtok(args, " ");

    // xxx should this automatically clear theschemantic
    // xxx this causes multiple model reset calls

    fp = fopen(filename, "r");
    if (fp == NULL) {
        ERROR("unable to open '%s', %s\n", filename, strerror(errno));
        return -1;
    }

    strcpy(last_filename_used, filename);

    while (fgets(s, sizeof(s), fp) != NULL) {
        fileline++;
        rc = process_cmd(s);
        if (rc < 0) {
            ERROR("aborting read of command from '%s', line %d\n", filename, fileline);
            return -1;
        }
    }

    fclose(fp);
    return 0;
}

static int32_t cmd_write(char *args)
{
    FILE * fp;
    int32_t i;
    char *filename;

    filename = strtok(args, " ");

    if (filename == NULL) {
        filename = last_filename_used;
        if (filename[0] == '\0') {
            ERROR("no filename\n");
            return -1;
        }
    }

    fp = fopen(filename, "w");
    if (fp == NULL) {
        ERROR("unable to open '%s', %s\n", filename, strerror(errno));
        return -1;
    }

    INFO("saving to file '%s'\n", filename);

    if (filename != last_filename_used) {
        strcpy(last_filename_used, filename);
    }

    fprintf(fp, "clear_schematic\n");

    for (i = 0; i < max_component; i++) {
        component_t *c = &component[i];
        if (c->type == COMP_NONE) {
            continue;
        }
        fprintf(fp, "add %s\n", make_component_str(c));
    }

    if (ground_is_set) {
        fprintf(fp, "ground %s\n", make_gridloc_str(&ground));
    }

    fclose(fp);
    return 0;
}

static int32_t cmd_add(char *args)
{
    char *type, *gl0, *gl1, *value;

    type = strtok(args, " ");
    gl0 = strtok(NULL, " ");
    gl1 = strtok(NULL, " ");
    value = strtok(NULL, "");

    return add_component(type, gl0, gl1, value);
}

static int32_t cmd_del(char *args)
{
#if 0
    // XXX maybe del based on grid loc
    return del_component(compid);
#endif
    return -1;
}

static int32_t cmd_ground(char *args)
{
    int32_t rc;
    gridloc_t new_ground;
    char *gl;

    gl = strtok(args, " ");

    // construct gridloc for the ground
    rc = make_gridloc(gl, &new_ground);
    if (rc < 0) {
        ERROR("invalid gridloc '%s'\n", gl);
        return -1;
    }

    // reset circuit simulator,
    // set the new_ground,
    // identify grid ground locations
    circsim_cmd("reset");
    ground = new_ground;   // xxx maybe these 2 dont need to be global
    ground_is_set = true;
    identify_grid_ground(NULL);
    return 0;
}

static int32_t cmd_sim(char *args)
{
    char *cmd;

    cmd = strtok(args, "");

    return circsim_cmd(cmd);
}

// -----------------  ADD & DEL COMPOENTS  --------------------------------

static int32_t add_component(char *type_str, char *gl0_str, char *gl1_str, char *value_str)
{
    component_t new_comp, *c;
    int32_t compid, x0, y0, x1, y1, i, rc, type=-1;
    char *gl_str;
    bool ok;
    float val1, val2;

    static char * component_type_str[] = {
                    "none",
                    "connection",
                    "power",
                    "resistor",
                    "capacitor",
                    "inductor",
                    "diode",
                        };

    // convert type_str to type
    for (i = 0; i <= COMP_LAST; i++) {
        if (strcasecmp(component_type_str[i], type_str) == 0) {
            type = i;
            break;
        }
    }
    if (type == -1) {
        ERROR("component type '%s' not supported\n", type_str);
        return -1;
    }

    // find the first available component tbl entry
    for (compid = 0; compid < MAX_COMPONENT; compid++) {
        if (component[compid].type == COMP_NONE) {
            break;
        }        
    }
    if (compid == MAX_COMPONENT) {
        ERROR("too many components, max allowed = %d\n", MAX_COMPONENT);
        return -1;
    }
    assert(compid <= max_component);

    // init new_comp ...
    // - zero new_comp struct
    memset(&new_comp, 0, sizeof(new_comp));
    // - set type, type_str, compid
    new_comp.type = type;
    new_comp.type_str = component_type_str[i];
    new_comp.compid = compid;
    // - set term
    for (i = 0; i < 2; i++) {
        new_comp.term[i].component = &component[compid];
        new_comp.term[i].termid = i;
        gl_str = (i == 0 ? gl0_str : gl1_str);
        rc = make_gridloc(gl_str, &new_comp.term[i].gridloc);
        if (rc < 0) {
            ERROR("invalid gridloc '%s'\n", gl_str);
            return -1;
        }
    }
    // - set component value
    switch (new_comp.type) {
    case COMP_POWER:
        if (value_str && sscanf(value_str, "%f %f", &val1, &val2) != 2) {
            ERROR("invalid value '%s' for %s\n", value_str, new_comp.type_str);
            return -1;
        }
        new_comp.power.volts = val1;
        new_comp.power.hz = val2;
        break;
    case COMP_RESISTOR:
        if (value_str && sscanf(value_str, "%f", &val1) != 1) {
            ERROR("invalid value '%s' for %s\n", value_str, new_comp.type_str);
            return -1;
        }
        new_comp.resistor.ohms = val1;
        break;
    case COMP_CAPACITOR:
        // xxx tbd
        break;
    case COMP_INDUCTOR:
        // xxx tbd
        break;
    }

    // verify terminals are adjacent, except for:
    // - COMP_CONNECTION where they just need to be in the same row or column
    // - COMP_POWER where the circsim model will enforce restrictions on 
    //   how the power supplies are connected
    x0 = new_comp.term[0].gridloc.x;
    y0 = new_comp.term[0].gridloc.y;
    x1 = new_comp.term[1].gridloc.x;
    y1 = new_comp.term[1].gridloc.y;
    ok = false;
    if (type == COMP_POWER) {
        ok = true;
    } else if (type == COMP_CONNECTION) {
        ok = (x0 == x1 && y0 != y1) ||
             (y0 == y1 && x0 != x1);
    } else {
        ok = (x0 == x1 && (y0 == y1-1 || y0 == y1+1)) ||
             (y0 == y1 && (x0 == x1-1 || x0 == x1+1));
    }
    if (!ok) {
        ERROR("invalid terminal locations '%s' '%s'\n", gl0_str, gl1_str);
        return -1;
    }

    // XXX verify not overlapping with existing component

    // commit the new component ...

    // - reset the circuit simulator
    circsim_cmd("reset");

    // - add new_comp to component list
    c = &component[compid];
    *c = new_comp;
    if (compid == max_component) {
        max_component++;
    }

    // - add the new component to grid
    for (i = 0; i < 2; i++) {
        int32_t x = c->term[i].gridloc.x;
        int32_t y = c->term[i].gridloc.y;
        grid_t * g = &grid[x][y];
        if (g->max_term == MAX_GRID_TERM) {
            FATAL("xxx all terminals used on gridloc %s\n", make_gridloc_str(&c->term[i].gridloc));
            return -1;
        }
        g->term[g->max_term++] = &c->term[i];
    }

    // - search grid to identify the ground locations
    identify_grid_ground(NULL);

    // return the compid
    return compid;
}

static int32_t del_component(char * compid_str)
{
    int32_t compid, i, j;
    component_t *c;

// XXX dont use compid
    // verify the compid_str, and
    // set ptr to the component to be deleted
    if (sscanf(compid_str, "%d", &compid) != 1 ||
        compid < 0 || compid >= max_component ||
        &component[compid].type == COMP_NONE)
    {
        ERROR("component %d does not exist\n", compid);
        return -1;
    }
    c = &component[compid];

    // remove the component ...

    // - reset the circuit simulator
    circsim_cmd("reset");

    // - remove the component's 2 terminals from the grid
    for (i = 0; i < 2; i++) {
        grid_t * g = &grid[c->term[i].gridloc.x][c->term[i].gridloc.y];
        bool found = false;
        for (j = 0; j < g->max_term; j++) {
            if (g->term[j] == &c->term[i]) {
                memmove(&g->term[j], 
                        &g->term[j]+1,
                        sizeof(gridloc_t) * (g->max_term - 1));
                g->max_term--;
                found = true;
                break;
            }
        }
        assert(found);
    }

    // - remove from component list
    component[compid].type = COMP_NONE;
    memset(&component[compid], 0, sizeof(component_t));

    // - search grid to identify the ground locations
    identify_grid_ground(NULL);

    // success0
    return 0;
}

// -----------------  PUBLIC UTILS  -------------------------------------------------------------

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

int32_t make_gridloc(char *glstr, gridloc_t * gl)
{
    int32_t x=-1, y=-1;

    if (glstr[0] >= 'a' && glstr[0] <= 'z') {
        y = glstr[0] - 'a';
    } else if (glstr[0] >= 'A' && glstr[0] <= 'Z') {
        y = glstr[0] - 'A' + 26;
    }

    sscanf(glstr+1, "%d", &x);
    x--;

    if (y < 0 || y >= MAX_GRID_Y || x < 0 || x >= MAX_GRID_X) {
        return -1;
    }

    gl->x = x;
    gl->y = y;
    return 0;
}

// -----------------  PRIVATE UTILS  -------------------------------------------------------------

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

    // XXX better way to select format, and add units comment
    // XXX use a routine to make the valuestr, I have something in PFM
    //        100  100K    100M   10pf   10uf  10mf   etc
    //            will also need comparable routine to scan the strings

    switch (c->type) {
    case COMP_POWER:
        p += sprintf(p, "%.2f %.2f  # volts hz", c->power.volts, c->power.hz);
        break;
    case COMP_RESISTOR:
        p += sprintf(p, "%.2f  # ohms", c->resistor.ohms);
        break;
    case COMP_CAPACITOR:
        // xxx tbd
        break;
    case COMP_INDUCTOR:
        // xxx tbd
        break;
    }

    return s;
}

static void identify_grid_ground(gridloc_t *gl)
{
    #define MAX_GROUND_GRIDLOC 500
    static gridloc_t ground_gridloc[MAX_GROUND_GRIDLOC];
    static int32_t max_ground_gridloc;

    grid_t *g;
    int32_t i;

    // if this is the first_call (not the recursive call) then
    if (gl == NULL) {
        // clear the pre-existing grid ground flags
        for (i = 0; i < max_ground_gridloc; i++) {
            int32_t x = ground_gridloc[i].x;
            int32_t y = ground_gridloc[i].y;
            assert(grid[x][y].ground);
            grid[x][y].ground = false;
        }
        max_ground_gridloc = 0;

        // if ground is not set then return
        if (!ground_is_set) {
            return;
        }

        // set gl to the 'ground' gridloc
        gl = &ground;
    }

    // set this grid location's ground flag, and
    // add to list of ground_gridloc
    g = &grid[gl->x][gl->y];
    g->ground = true;
    assert(max_ground_gridloc < MAX_GROUND_GRIDLOC);
    ground_gridloc[max_ground_gridloc++] = *gl;

    // loop over this grid location's terminals;
    // if the terminal is a CONNECTION then determine the
    // gridloc on the other side of the connection and 
    // call identify_grid_ground for that gridloc
    for (i = 0; i < g->max_term; i++) {
        component_t * c = g->term[i]->component;
        if (c->type == COMP_CONNECTION) {
            int32_t      other_term_id = (g->term[i]->termid ^ 1);
            terminal_t * other_term = &c->term[other_term_id];
            gridloc_t  * other_gl = &other_term->gridloc;
            grid_t     * other_g = &grid[other_gl->x][other_gl->y];
            if (other_g->ground == false)  {
                identify_grid_ground(other_gl);
            }
        }
    }
}

