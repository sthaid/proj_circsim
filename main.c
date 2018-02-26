// XXX use of assert vs FATAL
//     could make my ASSERT
#define MAIN
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

static char last_filename_used[200];

//
// prototypes
//

static void main_init(void);
static void help(void);

static void * cli_thread(void * cx);
static int32_t process_cmd(char * cmdline);
static int32_t cmd_help(char *args);
static int32_t cmd_set(char *args);
static int32_t cmd_show(char *args);
static int32_t cmd_clear_schematic(char *args);
static int32_t cmd_read(char *args);
static int32_t cmd_write(char *args);
static int32_t cmd_add(char *args);
static int32_t cmd_del(char *args);
static int32_t cmd_ground(char *args);
static int32_t cmd_model(char *args);

static int32_t add_component(char *type_str, char *gl0_str, char *gl1_str, char *value_str);
static int32_t del_component(char * comp_str);

static void identify_grid_ground(gridloc_t *gl);
static void init_grid(void);

// -----------------  MAIN  -----------------------------------------------

int32_t main(int32_t argc, char ** argv)
{
    pthread_t thread_id;

    // call initialization routines
    main_init();
    display_init();
    model_init();

    // get and process options
    while (true) {
        char opt_char = getopt(argc, argv, "h");
        if (opt_char == -1) {
            break;
        }
        switch (opt_char) {
        case 'h':
            help();
            return 0;
        default:
            return 1;
            break;
        }
    }

    // if filename arg is supplied then call cmd_read to 
    // open the filename and read/process the cmds
    if (argc - optind >= 1) {
        char *filename = argv[optind];
        if (cmd_read(filename) < 0) {
            return -1;
        }
    }

    // create thread for cli
    pthread_create(&thread_id, NULL, cli_thread, NULL);

    // call display handler
    display_handler();

    // done
    return 0;
}

static void main_init(void)
{
    init_grid();
}

static void help(void)
{
    INFO("usage: model [-h] [<filename>]\n");
    INFO("  -h: help\n");
    BLANK_LINE;

    INFO("commands:\n");
    cmd_help(NULL);
}

// -----------------  CLI THREAD  -----------------------------------------

static void * cli_thread(void * cx)
{
    char *cmd_str = NULL, prompt_str[100];
    sdl_event_t event;

    // give display time to initialize before starting cli
    sleep(1);

    // use readline/add_history to read commands, and
    // call process_cmd to process them
    while (true) {
        free(cmd_str);
        sprintf(prompt_str, "%s> ", MODEL_STATE_STR(model_state));
        if ((cmd_str = readline(prompt_str)) == NULL) {
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

    { "clear_schematic", cmd_clear_schematic, "",                                },
    { "read",            cmd_read,            "<filename>"                       },
    { "write",           cmd_write,           "[<filename>]"                     },
    { "add",             cmd_add,             "<type> <gl0> <gl1> [<values>]"    },
    { "del",             cmd_del,             "<comp_str>"                       },
    { "ground",          cmd_ground,          "<gl>"                             },

    { "model",           cmd_model,           "<reset|run|stop|cont|step>"       },
                    };

#define MAX_CMD_TBL (sizeof(cmd_tbl) / sizeof(cmd_tbl[0]))

static int32_t process_cmd(char * cmdline)
{
    char *comment_char;
    char *cmd, *args;
    int32_t i, rc, len;
    char cmdline_orig[1000];

    // terminate cmdline at the comment ('#') character, if any
    comment_char = strchr(cmdline,'#');
    if (comment_char) {
        *comment_char = '\0';
    }

    // make a copy of cmdline, to be used if an error is encountered
    strcpy(cmdline_orig, cmdline);
    len = strlen(cmdline_orig);
    if (len > 0 && cmdline_orig[len-1] == '\n') {
        cmdline_orig[len-1] = '\0';
    }

    // get command, if no cmd then return 
    cmd = strtok(cmdline, " \n");
    if (cmd == NULL) {
        return 0;
    }

    // get args, and remove leading and trailing spaces
    args = strtok(NULL, "\n");
    if (args == NULL) {
        args = "";
    }
    while (*args == ' ') {
        args++;
    }
    len = strlen(args); 
    while (len > 0) {
        if (args[len-1] == ' ') {
            args[len-1] = '\0';
            len--;
        } else {
            break;
        }
    }

    // find cmd in cmd_tbl
    for (i = 0; i < MAX_CMD_TBL; i++) {
        if (strcmp(cmd, cmd_tbl[i].name) == 0) {
            display_lock();
            rc = cmd_tbl[i].proc(args);
            display_unlock();
            if (rc < 0) {
                ERROR("failed: '%s'\n", cmdline_orig);
                return -1;
            }
            break;
        }
    } 
    if (i == MAX_CMD_TBL) {
        ERROR("cmd not found: '%s'\n", cmd);
        return -1;
    }

    // return success
    return 0;
}

// the following cmd routines return
// -1 for error and 0 for success

static int32_t cmd_help(char *args)
{
    int32_t i;

    // print the usage strings
    for (i = 0; i < MAX_CMD_TBL; i++) {
        INFO("%-16s %s\n", cmd_tbl[i].name, cmd_tbl[i].usage);
    }
    return 0;
}

static int32_t cmd_set(char *args)
{
    char *name, *value;
    int32_t i;

    // tokenize args, and verify all supplied
    name = strtok(args, " ");
    value = strtok(NULL, " ");
    if (name == NULL || value == NULL) {
        ERROR("insufficient args\n");
        return -1;
    }

    // try to find name in params_tbl; 
    // if found then set the param
    for (i = 0; params_tbl[i].name; i++) {
        if (strcmp(name, params_tbl[i].name) == 0) {
            break;
        }
    }
    if (params_tbl[i].name == NULL) {
        ERROR("param '%s' not found\n", name);
        return -1;
    }

//XXX routine
    // store new param value
    strcpy(params_tbl[i].value, value);
    param_update_count++;

    // success
    return 0;
}

static int32_t cmd_show(char *args)
{
    int32_t i;
    bool show_all, printed=false;
    char *what;
    char s[100];

    // determine what is to be shown
    what = strtok(args, " ");
    show_all = (what == NULL);

    // show params
    if (show_all || strcmp(what,"params") == 0) {
        INFO("PARAMS\n");
        for (i = 0; params_tbl[i].name; i++) {
            INFO("  %-12s %s",
                 params_tbl[i].name,
                 params_tbl[i].value);
        }
        BLANK_LINE;
        printed = true;
    }

    // show components
    if (show_all || strcmp(what,"components") == 0) {
        INFO("COMPONENTS\n");
        for (i = 0; i < max_component; i++) {
            component_t * c = &component[i];
            if (c->type == COMP_NONE) {
                continue;
            }
            INFO("  %-8s %s\n", c->comp_str, component_to_full_str(c,s));
        }
        BLANK_LINE;
        printed = true;
    }

    // show ground
    if (show_all || strcmp(what,"ground") == 0) {
        INFO("GROUND\n");
        INFO("  ground %s\n", ground_is_set ? gridloc_to_str(&ground,s) : "NOT_SET");
        BLANK_LINE;
        printed = true;
    }

    // if nothing was shown then print error
    if (printed == false) {
        ERROR("not supported '%s'\n", what);
    }

    // return success
    return 0;
}

static int32_t cmd_clear_schematic(char *args)                                        
{
    // reset model
    model_reset();

    // remove all components
    max_component = 0;
    memset(component,0,sizeof(component));

    // remove ground
    memset(&ground, 0, sizeof(ground));
    ground_is_set = false;
    identify_grid_ground(NULL);

    // re-initialize the grid
    init_grid();

    // success
    return 0;
}

static int32_t cmd_read(char *args)
{
    FILE * fp;
    char s[200], *filename;
    int32_t rc, fileline=0;

    // tokenize and verify args
    filename = strtok(args, " ");
    if (filename == NULL) {
        ERROR("insufficient args\n");
        return -1;
    }

    // open the file for reading
    fp = fopen(filename, "r");
    if (fp == NULL) {
        ERROR("unable to open '%s', %s\n", filename, strerror(errno));
        return -1;
    }

    // keep track of the last_filename_used, so if the write command
    // is issued without a filenmae arg it will use the last_filename_used
    strcpy(last_filename_used, filename);

    // read cmd lines from file, and call process_cmd 
    while (fgets(s, sizeof(s), fp) != NULL) {
        fileline++;
        rc = process_cmd(s);
        if (rc < 0) {
            ERROR("aborting read of command from '%s', line %d\n", filename, fileline);
            fclose(fp);
            return -1;
        }
    }

    // close, and return success
    fclose(fp);
    return 0;
}

static int32_t cmd_write(char *args)
{
    FILE * fp;
    int32_t i;
    char *filename;
    char s[100];

    // get the filename to be written
    filename = strtok(args, " ");
    if (filename == NULL) {
        filename = last_filename_used;
        if (filename[0] == '\0') {
            ERROR("no filename\n");
            return -1;
        }
    }

    // open the file for writing
    fp = fopen(filename, "w");
    if (fp == NULL) {
        ERROR("unable to open '%s', %s\n", filename, strerror(errno));
        return -1;
    }

    // print 
    INFO("saving to file '%s'\n", filename);

    // keep track of the last_filename_used, so that a subsequent write
    // will use the same filename by default
    if (filename != last_filename_used) {
        strcpy(last_filename_used, filename);
    }

    // print the commands to the file
    fprintf(fp, "clear_schematic\n");
    fprintf(fp, "\n");

    for (i = 0; i < max_component; i++) {
        char s[100];
        component_t *c = &component[i];
        if (c->type == COMP_NONE) {
            continue;
        }
        fprintf(fp, "add %s\n", component_to_full_str(c,s));
    }
    fprintf(fp, "\n");

    if (ground_is_set) {  // xxx is there a default
        fprintf(fp, "ground %s\n", gridloc_to_str(&ground,s));
        fprintf(fp, "\n");
    }

    for (i = 0; params_tbl[i].name; i++) {
        fprintf(fp, "set %-12s %s\n", params_tbl[i].name, params_tbl[i].value);
    }
    fprintf(fp, "\n");

    // close the file
    fclose(fp);

    // success
    return 0;
}

static int32_t cmd_add(char *args)
{
    char *type, *gl0, *gl1, *value;

    // tokenize and verify args supplied
    type = strtok(args, " ");
    gl0 = strtok(NULL, " ");
    gl1 = strtok(NULL, " ");
    value = strtok(NULL, " ");

    // value is not needed for some components
    if (type == NULL || gl0 == NULL || gl1 == NULL) {
        ERROR("insufficient args\n");
        return -1;
    }

    // call add_component to do the work
    return add_component(type, gl0, gl1, value);
}

static int32_t cmd_del(char *args)
{
    char * comp_str;

    // get the comp_str which identifies the component, such as 'R1'
    comp_str = strtok(args, " ");
    if (comp_str == NULL) {
        ERROR("insufficient args\n");
        return -1;
    }

    // call del_component to do the work
    return del_component(comp_str);
}

static int32_t cmd_ground(char *args)
{
    int32_t rc;
    gridloc_t new_ground;
    char *gl;

    // tokenize and verify arg is supplied
    gl = strtok(args, " ");
    if (gl == NULL) {
        ERROR("insufficient args\n");
        return -1;
    }

    // construct gridloc for the ground
    rc = str_to_gridloc(gl, &new_ground);
    if (rc < 0) {
        ERROR("invalid gridloc '%s'\n", gl);
        return -1;
    }

    // reset model
    // set the new_ground,
    // identify grid ground locations
    model_reset();
    ground = new_ground;
    ground_is_set = true;
    identify_grid_ground(NULL);
    return 0;
}

static int32_t cmd_model(char *args)
{
    char *cmdline = args;

    // pass cmd to the model
    return model_cmd(cmdline);
}

// -----------------  ADD & DEL COMPOENTS  --------------------------------

// XXX review
static int32_t add_component(char *type_str, char *gl0_str, char *gl1_str, char *value_str)
{
    component_t new_comp, *c;
    int32_t idx, x0, y0, x1, y1, i, rc, type=-1;
    char *gl_str;
    bool ok;

    static char * component_type_str[] = {
                    "none",
                    "wire",
                    "power",
                    "resistor",
                    "capacitor",
                    "inductor",
                    "diode",
                        };
    static int32_t wire_id, power_id, resistor_id, capacitor_id, inductor_id;

    // if max_component is zero then reset resistor,capacitor,... id variables
    if (max_component == 0) {
        wire_id = 1;
        power_id = 1;
        resistor_id = 1;
        capacitor_id = 1;
        inductor_id = 1;
    }

    // convert type_str to type; 
    // if type_str does not exist then return error
    for (i = 0; i <= COMP_LAST; i++) {
        if (strcmp(component_type_str[i], type_str) == 0) {
            type = i;
            break;
        }
    }
    if (type == -1) {
        ERROR("component type '%s' not supported\n", type_str);
        return -1;
    }

    // find the first available component tbl entry
    for (idx = 0; idx < MAX_COMPONENT; idx++) {
        if (component[idx].type == COMP_NONE) {
            break;
        }        
    }
    if (idx == MAX_COMPONENT) {
        ERROR("too many components, max allowed = %d\n", MAX_COMPONENT);
        return -1;
    }
    assert(idx <= max_component);

    // init new_comp ...
    // - zero new_comp struct
    memset(&new_comp, 0, sizeof(new_comp));
    // - set type, type_str
    new_comp.type = type;
    new_comp.type_str = component_type_str[i];
    // - set comp_str
    switch (new_comp.type) {
    case COMP_WIRE:
        sprintf(new_comp.comp_str, "W%d", wire_id++);
        break;
    case COMP_POWER:
        sprintf(new_comp.comp_str, "P%d", power_id++);
        break;
    case COMP_RESISTOR:
        sprintf(new_comp.comp_str, "R%d", resistor_id++);
        break;
    case COMP_CAPACITOR:
        sprintf(new_comp.comp_str, "C%d", capacitor_id++);
        break;
    case COMP_INDUCTOR:
        sprintf(new_comp.comp_str, "C%d", inductor_id++);
        break;
    }
    // - set term
    for (i = 0; i < 2; i++) {
        new_comp.term[i].component = &component[idx];
        new_comp.term[i].termid = i;
        gl_str = (i == 0 ? gl0_str : gl1_str);
        rc = str_to_gridloc(gl_str, &new_comp.term[i].gridloc);
        if (rc < 0) {
            ERROR("invalid gridloc '%s'\n", gl_str);
            return -1;
        }
    }
    // - set component value
    switch (new_comp.type) {
    case COMP_WIRE:
        value_str = strtok(value_str, ",");
        if (value_str) {
            if (strcmp(value_str,"remote") != 0) {
                ERROR("invalid value '%s' for %s\n", value_str, new_comp.type_str);
                return -1;
            }
            value_str = strtok(value_str, "");
            // xxx parse color  NOT GREEN or WHITE
            new_comp.wire.remote = true;
            new_comp.wire.remote_color = RED;
        }
        break;
    case COMP_POWER:
        value_str = strtok(value_str, ",");
        rc = str_to_val(value_str, UNITS_VOLTS, &new_comp.power.volts);
        if (rc == -1) {
            ERROR("invalid value '%s' for %s\n", value_str, new_comp.type_str);
            return -1;
        }
        value_str = strtok(NULL, "");
        if (value_str != NULL) {
            rc = str_to_val(value_str, UNITS_HZ, &new_comp.power.hz);
            if (rc == -1) {
                ERROR("invalid value '%s' for %s\n", value_str, new_comp.type_str);
                return -1;
            }
        }
        break;
    case COMP_RESISTOR:
        rc = str_to_val(value_str, UNITS_OHMS, &new_comp.resistor.ohms);
        if (rc == -1) {
            ERROR("invalid value '%s' for %s\n", value_str, new_comp.type_str);
            return -1;
        }
        break;
    case COMP_CAPACITOR:
        rc = str_to_val(value_str, UNITS_FARADS, &new_comp.capacitor.farads);
        if (rc == -1) {
            ERROR("invalid value '%s' for %s\n", value_str, new_comp.type_str);
            return -1;
        }
        break;
    case COMP_INDUCTOR:
        rc = str_to_val(value_str, UNITS_HENRYS, &new_comp.inductor.henrys); 
        if (rc == -1) {
            ERROR("invalid value '%s' for %s\n", value_str, new_comp.type_str);
            return -1;
        }
        break;
    }

    // verify terminals are adjacent, except for:
    // - COMP_WIRE where they just need to be in the same row or column
    // xxx comment
    // xxx also check x0,y0 not x0,y1
    x0 = new_comp.term[0].gridloc.x;
    y0 = new_comp.term[0].gridloc.y;
    x1 = new_comp.term[1].gridloc.x;
    y1 = new_comp.term[1].gridloc.y;
    ok = false;
    if (new_comp.type == COMP_WIRE) {
        ok = ((new_comp.wire.remote == true) ||
              ((x0 == x1 && y0 != y1) ||
               (y0 == y1 && x0 != x1)));
    } else {
        ok = (x0 == x1 && (y0 == y1-1 || y0 == y1+1)) ||
             (y0 == y1 && (x0 == x1-1 || x0 == x1+1));
    }
    if (!ok) {
        ERROR("invalid terminal locations '%s' '%s'\n", gl0_str, gl1_str);
        return -1;
    }

    // if the new component is a remote wire then verify that the
    // 2 grid locations don't already have a remote wire
    if (new_comp.type == COMP_WIRE && new_comp.wire.remote) {
        for (i = 0; i < 2; i++) {
            grid_t * g = &grid[new_comp.term[i].gridloc.x][new_comp.term[i].gridloc.y];
            if (g->has_remote_wire) {
                ERROR("gridloc %s already has a remote wire\n", g->glstr);
                return -1;
            }
        }
    }
        
    // xxx verify not overlapping with existing component

    // commit the new component ...

    // - reset the model
    model_reset();

    // - add new_comp to component list
    c = &component[idx];
    *c = new_comp;
    if (idx == max_component) {
        max_component++;
    }

    // - add the new component to grid
    // xxx how do we know max_term is okay
    for (i = 0; i < 2; i++) {
        int32_t x = c->term[i].gridloc.x;
        int32_t y = c->term[i].gridloc.y;
        grid_t * g = &grid[x][y];
        if (g->max_term == MAX_GRID_TERM) {
            FATAL("all terminals used on gridloc %s\n", g->glstr);
            return -1;
        }
        g->term[g->max_term++] = &c->term[i];
        if (c->type == COMP_WIRE && c->wire.remote == true) {
            assert(g->has_remote_wire == false);
            g->has_remote_wire = true;
            g->remote_wire_color = c->wire.remote_color;
        }
    }

    // - search grid to identify the ground locations
    identify_grid_ground(NULL);

    // return success
    return 0;
}

static int32_t del_component(char * comp_str)
{
    int32_t i, j;
    component_t *c;

    // locate the component to be deleted
    for (i = 0; i < max_component; i++) {
        c = &component[i];
        if (c->type != COMP_NONE && strcmp(c->comp_str, comp_str) == 0) {
            break;
        }
    }
    if (i == max_component) {
        ERROR("component '%s' does not exist\n", comp_str);
        return -1;
    }

    // remove the component ...

    // - reset the model
    model_reset();

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

    // - if the component is a remote wire then clear the 
    //   grid has_remote_wire flag
    if (c->type == COMP_WIRE && c->wire.remote) {
        for (i = 0; i < 2; i++) {
            grid_t * g = &grid[c->term[i].gridloc.x][c->term[i].gridloc.y];
            assert(g->has_remote_wire);
            g->has_remote_wire = false;
        }
    }

    // - remove from component list
    c->type = COMP_NONE;
    memset(c, 0, sizeof(component_t));

    // - search grid to identify ground locations that have possibly changed
    //   as a result of the component's deletion
    identify_grid_ground(NULL);

    // success0
    return 0;
}

// -----------------  PUBLIC UTILS  -------------------------------------------------------------

// convert gridloc to string
char * gridloc_to_str(gridloc_t * gl, char * s)
{
    assert(gl->x >= 0 && gl->x < MAX_GRID_X);
    assert(gl->y >= 0 && gl->y < MAX_GRID_Y);

    sprintf(s, "%c%d", 
            gl->y + (gl->y < 26 ? 'a' : 'A' - 26),
            gl->x + 1);
    return s;
}

// convert string to gridloc, return -1 on error
int32_t str_to_gridloc(char *glstr, gridloc_t * gl)
{
    int32_t x=-1, y=-1;

    if (glstr == NULL) {
        return -1;
    }

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

// convert component to value string
char * component_to_value_str(component_t * c, char *s)
{
    int32_t len;

    s[0] = '\0';

    switch (c->type) {
    case COMP_POWER:
        val_to_str(c->power.volts, UNITS_VOLTS, s);
        if (c->power.hz > 0) {
            len = strlen(s);
            strcpy(s+len, ",");
            val_to_str(c->power.hz, UNITS_HZ, s+len+1);
        }
        break;
    case COMP_RESISTOR:
        val_to_str(c->resistor.ohms, UNITS_OHMS, s);
        break;
    case COMP_CAPACITOR:
        val_to_str(c->capacitor.farads, UNITS_FARADS, s);
        break;
    case COMP_INDUCTOR:
        val_to_str(c->inductor.henrys, UNITS_HENRYS, s);
        break;
    case COMP_WIRE:
    case COMP_DIODE:
    default:
        break;
    }

    return s;
}

// convert component to full string
char * component_to_full_str(component_t * c, char * s)
{
    char *p, s1[100], s2[100];

    p = s;
    p += sprintf(p, "%-10s %-4s %-4s ",
                 c->type_str,
                 gridloc_to_str(&c->term[0].gridloc, s1),
                 gridloc_to_str(&c->term[1].gridloc, s2));
    component_to_value_str(c, p);

    return s;
}

typedef struct {
    char * units;
    double factor;  // 0 is table terminator
} convert_t;
static convert_t volts_tbl[]  = { {"kV",1e3}, {"V",1}, {"mV",1e-3}, {"uV",1e-6},                  {"V",0} };
static convert_t amps_tbl[]   = { {"A",1}, {"mA",1e-3}, {"uA",1e-6},                              {"A",0} };
static convert_t ohms_tbl[]   = { {"M",1e6}, {"K",1e3}, {"",1},                                   {"OHMS",0} };
static convert_t farads_tbl[] = { {"F",1}, {"mF",1e-3}, {"uF",1e-6}, {"nF",1e-9}, {"pF",1e-12},   {"F",0} };
static convert_t henrys_tbl[] = { {"H", 1}, {"mH",1e-3}, {"uH",1e-6},                             {"H",0} };
static convert_t hz_tbl[]     = { {"MHz",1e6}, {"kHz",1e3}, {"Hz",1},                             {"Hz",0} };
static convert_t time_tbl[]   = { {"s",1}, {"ms",1e-3}, {"us",1e-6}, {"ns",1e-9},                 {"s",0} };

// returns -1 on error, otherwise the number of chars from s that were processed;
//
// if no units_str is supplied then the value is returned;
// otherwise, this routine requires a match of the units_str with tbl units prior to 
// reaching the table terminator
int32_t str_to_val(char * s, int32_t units, double * val_result)
{
    convert_t *tbl, *t;
    int32_t cnt, n;
    char units_str[100];
    double v;

    // verify string is supplied
    if (s == NULL) {
        return -1;
    }

    // pick the conversion table
    tbl = (units == UNITS_VOLTS    ? volts_tbl  :
           units == UNITS_AMPS     ? amps_tbl   :
           units == UNITS_OHMS     ? ohms_tbl   :
           units == UNITS_FARADS   ? farads_tbl :
           units == UNITS_HENRYS   ? henrys_tbl :
           units == UNITS_HZ       ? hz_tbl     :
           units == UNITS_SECONDS  ? time_tbl   :
                                     NULL);
    assert(tbl);

    // scanf the string, return error if failed
    units_str[0] = '\0';
    cnt = sscanf(s, "%lf%n%s%n", &v, &n, units_str, &n);
    if (cnt != 1 && cnt != 2) {
        return -1;
    }
    
    // if the scanned units_str is empty then 
    //   just return the scanned value
    // else
    //   search the units tbl for a conversion factor that 
    //    matches the units_str;  if no match found then return 
    //    error, otherwise return the value converted to the 
    //    specified units;  for example if units_str is 'pF' then
    //    return the value multiplied by 1e-12
    // endif
    if (units_str[0] == '\0') {
        *val_result = v;
        return 0;        
    } else {
        t = tbl;
        while (true) {
            if (t->factor == 0) {
                return -1;
            }
            if (strcmp(units_str, t->units) == 0) {
                *val_result = v * t->factor;
                return n;
            }
            t++;
        }
    }
}

// if the tbl terminator is reached (because the value is too small) then 
// the value is printed using "%.2g" format, followed by the tbl terminator units
char * val_to_str(double val, int32_t units, char *s)
{
    char *fmt;
    convert_t *tbl, *t;
    double absval = fabs(val);

    // pick the conversion table
    tbl = (units == UNITS_VOLTS    ? volts_tbl  :
           units == UNITS_AMPS     ? amps_tbl   :
           units == UNITS_OHMS     ? ohms_tbl   :
           units == UNITS_FARADS   ? farads_tbl :
           units == UNITS_HENRYS   ? henrys_tbl :
           units == UNITS_HZ       ? hz_tbl     :
           units == UNITS_SECONDS  ? time_tbl   :
                                     NULL);
    assert(tbl);
    
    // scan the conversion table for the appropriate units string to use, 
    // and sprint the value and units
    t = tbl;
    while (true) {
        if (absval >= (t->factor * 0.999) || t->factor == 0) { 
            if (t->factor == 0) {
                sprintf(s, "%.2g%s", val, t->units);
            } else {
                val /= t->factor;
                absval = fabs(val);
                fmt = (absval > 99.99 ? "%.0f%s" :
                       absval > 9.999 ? "%.1f%s" :
                                        "%.2f%s");
                sprintf(s, fmt, val, t->units);
            }
            break;
        }
        t++;
    }

    // return the static string
    return s;
}

// -----------------  PRIVATE UTILS  ------------------------------------------------------------

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
    // if the terminal is a WIRE then determine the
    // gridloc on the other side of the wire and 
    // call identify_grid_ground for that gridloc
    for (i = 0; i < g->max_term; i++) {
        component_t * c = g->term[i]->component;
        if (c->type == COMP_WIRE) {
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

static void init_grid()
{
    int32_t glx, gly;
    char s[100];

    // clear the grid
    memset(&grid, 0, sizeof(grid));

    // reset the grid's gridloc strings
    for (glx = 0; glx < MAX_GRID_X; glx++) {
        for (gly = 0; gly < MAX_GRID_Y; gly++) {
            gridloc_t gl = {glx, gly};
            strcpy(grid[glx][gly].glstr, gridloc_to_str(&gl,s));
        }
    }
}
