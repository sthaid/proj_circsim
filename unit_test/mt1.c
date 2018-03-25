#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <signal.h>

#include <readline/readline.h>
#include <readline/history.h>

bool verbose = false;
bool interrupt = false;

long double vp      = 300;
long double vn1     = 0;
long double vn2     = 0;
long double vg      = 0;
long double r1      = 10;
long double r2      = 10;
long double r3      = 10;

long double ir1, ir2, ir3;

void run(void);
long double node_eval(
    long double v,
    long double v_ra,
    long double ohms_ra,
    long double v_rb,
    long double ohms_rb);
void show(void);
void basic_exponential_smoothing(long double x, long double *s, long double alpha);
void sig_handler(int32_t signum);

// -----------------  MAIN  --------------------------------------

// i = C dv/dt
// i = v / r

// Circuit
//    power  ----  resistor  ---- resistor  ---- resistor  ---- ground

// commands
// - set <name> <value>
//      r1 <ohms>
//      r2 <ohms>
//      r3 <ohms>   
//      vp <volts>
//      vn1 <volts>
//      vn2 <volts>
// - run <secs>        or 0 means 1 interval
// - show              displays the circuit and values
// - reset             resets to 0 volts on node1,node2
// - verbose on|off

int32_t main(int32_t argc, char **argv)
{
    char *cmdline=NULL, *cmd_str=NULL, *arg1_str=NULL, *arg2_str=NULL;
    
    // register signal handler
    static struct sigaction act;
    act.sa_handler = sig_handler;
    sigaction(SIGINT, &act, NULL);
    
    // loop 
    while (true) {
        // read command
        free(cmdline);
        if ((cmdline = readline("? ")) == NULL) {
            break;
        }
        if (cmdline[0] != '\0') {
            add_history(cmdline);
        }

        // use strtok to parse cmdline
        cmd_str = strtok(cmdline, " \n");
        arg1_str = strtok(NULL, " \n");
        arg2_str = strtok(NULL, " \n");

        // if no command then continue
        if (cmd_str == NULL) {
            continue;
        }

        // clear interrupt flag, which is set by sig_handler to interrupt a run
        interrupt = false;

        // process command
        if (strcmp(cmd_str, "set") == 0) {
            long double val;
            if (arg2_str == NULL || sscanf(arg2_str, "%Lg", &val) != 1) {
                printf("error: usage - set <r1|r2|r3|vp|vn1|vn2> <value>\n");
                continue;
            }
            if (strcmp(arg1_str, "r1") == 0) {
                r1 = val;
            } else if (strcmp(arg1_str, "r2") == 0) {
                r2 = val;
            } else if (strcmp(arg1_str, "r3") == 0) {
                r3 = val;
            } else if (strcmp(arg1_str, "vp") == 0) {
                vp = val;
            } else if (strcmp(arg1_str, "vn1") == 0) {
                vn1 = val;
            } else if (strcmp(arg1_str, "vn2") == 0) {
                vn2 = val;
            } else {
                printf("error: usage - set <r1|r2|r3|vp|vn1|vn2> <value>\n");
            }
        } else if (strcmp(cmd_str, "run") == 0 || strcmp(cmd_str, "r") == 0) {
            run();
            show();
        } else if (strcmp(cmd_str, "show") == 0) {
            show();
        } else if (strcmp(cmd_str, "reset") == 0) {
            vn1 = vn2 = 0;
            ir1 = ir2 = ir3 = 0;
        } else if (strcmp(cmd_str, "verbose") == 0) {
            if (arg1_str == NULL) {
                printf("verbose is %s\n", verbose ? "on" : "off");
            } else if (strcmp(arg1_str, "on") == 0) {
                verbose = true;
            } else if (strcmp(arg1_str, "off") == 0) {
                verbose = false;
            } else {
                printf("error: expected 'on' or 'off'\n");
            }
        } else {
            printf("error: invalid command '%s'\n", cmd_str);
        }
    }

    return 0;
}

// -----------------  RUN CMD  -----------------------------------

void run(void)
{
    int64_t count;
    long double vn1_next, vn2_next;
    long double ir1_next, ir3_next, ir2_next;

    // determine the next voltages and currents,
    // exit the loop when all node current sums are close to 0
    count = 0;
    vn1_next = vn1; // xxx dvdt
    vn2_next = vn2; // xxx dvdt
    while (true) {
        vn1_next = node_eval(vn1, vp, r1, vn2_next, r3);
        vn2_next = node_eval(vn2, vn1_next, r3, vg, r2);

        ir1_next = (vp - vn1_next) / r1;
        ir3_next = (vn1_next - vn2_next) / r3;
        ir2_next = (vn2_next - vg) / r2;

        count++;
        if ((fabsl((ir1_next - ir3_next) / ir3_next) < .005) &&
            (fabsl((ir2_next - ir3_next) / ir3_next) < .005))
        {
            if (count > 10000) {
                printf("WARNING - CURRENT BREAK count=%ld - %Lg %Lg %Lg - %Lg %Lg\n",
                       count, ir1_next, ir3_next, ir2_next, vn1_next, vn2_next);
            }
            break;
        }
        if (count == 1000000000L) {
            printf("WARNING - FORCE BREAK count=%ld - %Lg %Lg %Lg - %Lg %Lg\n",
                   count, ir1_next, ir3_next, ir2_next, vn1_next, vn2_next);
            break;
        }
        if ((count % 10000000L) == 0) {
            printf("INFO count=%ld - %Lg %Lg %Lg - %Lg %Lg\n",
                   count, ir1_next, ir3_next, ir2_next, vn1_next, vn2_next);
        }
        if (interrupt) {
            printf("interrupt\n");
            interrupt = false;
            break;
        }
    }

    // print the next values
    if (verbose) {
        printf("       VN1        VN2        IR1        IR3        IR2\n");
        printf("%10Lg %10Lg %10Lg %10Lg %10Lg\n",
               vn1_next, vn2_next, ir1_next, ir3_next, ir2_next);
    }

    // save results
    vn1 = vn1_next;
    vn2 = vn2_next;
    ir1 = ir1_next;
    ir2 = ir2_next;
    ir3 = ir3_next;
}

long double node_eval(
    long double v,
    long double v_ra,
    long double ohms_ra,
    long double v_rb,
    long double ohms_rb)
{
    long double num = 0;
    long double denom = 0;

    num += (v_ra / ohms_ra);
    num += (v_rb / ohms_rb);

    denom += (1 / ohms_ra);
    denom += (1 / ohms_rb);

    return num / denom;
}

// -----------------  SHOW CMD  ----------------------------------

void show(void)
{
    printf("%12s %12Lg %12s %12Lg %12s %12Lg %12s\n",
           "", r1, "", r3, "", r2, "");
    printf("%12Lg %12s %12Lg %12s %12Lg %12s %12Lg\n",
           vp, "resistor", vn1, "resistor", vn2, "resistor", vg);
    printf("%12s %12Lg %12s %12Lg %12s %12Lg %12s\n",
           "", ir1, "", ir3, "", ir2, "");
}

// -----------------  MISC CMD  ----------------------------------

void basic_exponential_smoothing(long double x, long double *s, long double alpha)
{
    long double s_last = *s;
    *s = alpha * x + (1 - alpha) * s_last;
}

void sig_handler(int32_t signum)
{
    interrupt = true;
}
