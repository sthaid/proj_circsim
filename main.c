#include "common.h"

//
// defines
//

#define DEFAULT_WIN_WIDTH  1900
#define DEFAULT_WIN_HEIGHT 1000

#define MB 0x100000

//
// typedefs
//

//
// variables
//

char last_filename_used[200];

//
// prototypes
//

static void help(void);

static void * cli_thread(void * cx);
static int32_t process_cmd(char * cmdline);
static int32_t cmd_help(char *arg1, char *arg2, char *arg3, char *arg4);
static int32_t cmd_clear(char *arg1, char *arg2, char *arg3, char *arg4);
static int32_t cmd_read(char *filename, char *arg2, char *arg3, char *arg4);
static int32_t cmd_write(char *filename, char *arg2, char *arg3, char *arg4);
static int32_t cmd_show(char *arg1, char *arg2, char *arg3, char *arg4);
static int32_t cmd_add(char *type, char *gl0, char *gl1, char *value);
static int32_t cmd_del(char *compid, char *arg2, char *arg3, char *arg4);
static int32_t cmd_ground(char *gl, char *arg2, char *arg3, char *arg4);
static int32_t cmd_prep(char *arg1, char *arg2, char *arg3, char *arg4);

static int32_t add_component(char *type_str, char *gl0_str, char *gl1_str, char *value_str);
static int32_t del_component(char * compid_str);

// -----------------  MAIN  -----------------------------------------------

int32_t main(int32_t argc, char ** argv)
{
    pthread_t thread_id;
    int32_t rc;

    // xxx
    INFO("sizeof(component) = %ld MB\n", sizeof(component)/MB);
    INFO("sizeof(grid)      = %ld MB\n", sizeof(grid)/MB);
    INFO("sizeof(node)      = %ld MB\n", sizeof(node)/MB);

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
    { "help",     cmd_help,    0, 1, ""                           },
    { "clear",    cmd_clear,   0, 0, ""                           },
    { "read",     cmd_read,    1, 1, "<filename>"                 },
    { "write",    cmd_write,   0, 1, "[<filename>]"               },
    { "show",     cmd_show,    0, 0, ""                           },
    { "add",      cmd_add,     3, 4, "<type> <gl0> <gl1> <value>" },
    { "del",      cmd_del,     1, 1, "<compid>"                   },
    { "ground",   cmd_ground,  1, 1, "<gl>"                       },
    { "prep",     cmd_prep,    0, 0, ""                           },  // XXX temp
                    };

#define MAX_CMD_TBL (sizeof(cmd_tbl) / sizeof(cmd_tbl[0]))

static int32_t process_cmd(char * cmdline)
{
    char *comment_char;
    char *cmd, *arg1, *arg2, *arg3, *arg4;
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
    for (i = 0; i < MAX_CMD_TBL; i++) {
        if (strcmp(cmd, cmd_tbl[i].name) == 0) {
            if (arg_count < cmd_tbl[i].min_args || arg_count > cmd_tbl[i].max_args) {
                ERROR("incorrect number of args\n");
                return -1;
            }

            rc = cmd_tbl[i].proc(arg1,arg2,arg3,arg4);
            if (rc < 0) {
                ERROR("failed: '%s'\n", cmdline);
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
    memset(component,0,sizeof(component));

    memset(&ground, 0, sizeof(ground));
    ground_is_set = false;
    return 0;
}

static int32_t cmd_read(char *filename, char *arg2, char *arg3, char *arg4)
{
    FILE * fp;
    char s[200];
    int32_t rc, fileline=0;

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

static int32_t cmd_write(char *filename, char *arg2, char *arg3, char *arg4)
{
    FILE * fp;
    int32_t i;

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

    fprintf(fp, "clear\n");

    for (i = 0; i < max_component; i++) {
        component_t *c = &component[i];
        if (c->type == COMP_NONE) {
            continue;
        }
        fprintf(fp, "add %s\n", make_component_definition_str(c));
    }

    if (ground_is_set) {
        fprintf(fp, "ground %s\n", make_gridloc_str(&ground));
    }

    fclose(fp);
    return 0;
}

static int32_t cmd_show(char *arg1, char *arg2, char *arg3, char *arg4)
{
    int32_t i;

    for (i = 0; i < max_component; i++) {
        component_t * c = &component[i];
        if (c->type == COMP_NONE) {
            continue;
        }
        INFO("%-3d %s\n", i, make_component_definition_str(c));
    }

    if (ground_is_set) {
        INFO("ground %s\n", make_gridloc_str(&ground));
    }
    return 0;
}

static int32_t cmd_add(char *type, char *gl0, char *gl1, char *value)
{
    return add_component(type, gl0, gl1, value);
}

static int32_t cmd_del(char *compid, char *arg2, char *arg3, char *arg4)
{
    return del_component(compid);
}

static int32_t cmd_ground(char *gl, char *arg2, char *arg3, char *arg4)
{
    int32_t rc;

    rc = make_gridloc(gl, &ground);
    if (rc < 0) {
        ERROR("invalid gridloc '%s'\n", gl);
        ground_is_set = false;
        memset(&ground, 0, sizeof(ground));
        return -1;
    }

    ground_is_set = true;
    return 0;
}

static int32_t cmd_prep(char *arg1, char *arg2, char *arg3, char *arg4) // XXX temp
{
    return cs_prep();
}

// -----------------  ADD & DEL COMPOENTS  --------------------------------

static int32_t add_component(char *type_str, char *gl0_str, char *gl1_str, char *value_str)
{
    component_t new_comp;
    int32_t compid, x0, y0, x1, y1, i, rc, type;
    char *gl_str;
    bool ok;
    float value;

    // convert type_str to type
    for (i = 0; i <= COMP_LAST; i++) {
        if (strcasecmp(component_type_str(i), type_str) == 0) {
            type = i;
            break;
        }
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

    // init new_comp ...
    // - zero new_comp struct
    memset(&new_comp, 0, sizeof(new_comp));
    // - set type
    new_comp.type = type;
    // - set term
    for (i = 0; i < 2; i++) {
        new_comp.term[i].component = &component[compid];
        new_comp.term[i].id = i;
        gl_str = (i == 0 ? gl0_str : gl1_str);
        rc = make_gridloc(gl_str, &new_comp.term[i].gridloc);
        if (rc < 0) {
            ERROR("invalid gridloc '%s'\n", gl_str);
            return -1;
        }
    }
    // - set value
    value = 0;
    if (value_str && sscanf(value_str, "%f", &value) != 1) {
        ERROR("invalid value '%s'\n", value_str);
        return -1;
    }
    new_comp.values[0] = value;

    // verify terminals are adjacent, except for COMP_CONNECTION where they just
    // need to be in the same row or column
    x0 = new_comp.term[0].gridloc.x;
    y0 = new_comp.term[0].gridloc.y;
    x1 = new_comp.term[1].gridloc.x;
    y1 = new_comp.term[1].gridloc.y;
    ok = false;
    if (type == COMP_CONNECTION) {
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

    // xxx verify not overlapping

    // commit the new component
    component[compid] = new_comp;
    assert(compid <= max_component);
    if (compid == max_component) {
        max_component++;
    }

    // return the compid
    return compid;
}

static int32_t del_component(char * compid_str)
{
    int32_t compid;

    if (sscanf(compid_str, "%d", &compid) != 1 ||
        compid < 0 || compid >= max_component ||
        &component[compid].type == COMP_NONE)
    {
        ERROR("component %d does not exist\n", compid);
        return -1;
    }

    component[compid].type = COMP_NONE;
    memset(&component[compid], 0, sizeof(component_t));
    return 0;
}

