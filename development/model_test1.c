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

long double model_t = 0;
long double delta_t = .001;
long double vp      = 300;
long double vn1     = 0;
long double vn2     = 0;
long double vg      = 0;
#if 0
long double r1      = 19;
long double c1      = .05;
long double r2      = 1;
#else
long double r1      = 10e6;
long double c1      = .001;
long double r2      = 10e6;
#endif

long double ir1, ir2, ic1;

void run(int64_t steps);
long double node_eval(
    long double v,
    long double v_r_other,
    long double dvdt_c_other,
    long double ohms,
    long double farads);
void show(void);
void basic_exponential_smoothing(long double x, long double *s, long double alpha);
void sig_handler(int32_t signum);

// -----------------  MAIN  --------------------------------------

// i = C dv/dt
// i = v / r

// Circuit
//    power  ----  resistor  ---- cap  ---- resistor  ---- ground

// commands
// - set <name> <value>
//      delta_t <secs>
//      r1 <ohms>
//      r2 <ohms>
//      c1 <farads>
//      vp <volts>
//      vn1 <volts>
//      vn2 <volts>
// - run <secs>        or 0 means 1 interval
// - show              displays the circuit and values
// - reset             resets to time 0, and 0 volts on node1,node2
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
                printf("error: usage - set <delta_t|r1|r2|c1|vp|vn1|vn2> <value>\n");
                continue;
            }
            if (strcmp(arg1_str, "delta_t") == 0) {
                delta_t = val;
            } else if (strcmp(arg1_str, "r1") == 0) {
                r1 = val;
            } else if (strcmp(arg1_str, "r2") == 0) {
                r2 = val;
            } else if (strcmp(arg1_str, "c1") == 0) {
                c1 = val;
            } else if (strcmp(arg1_str, "vp") == 0) {
                vp = val;
            } else if (strcmp(arg1_str, "vn1") == 0) {
                vn1 = val;
            } else if (strcmp(arg1_str, "vn2") == 0) {
                vn2 = val;
            } else {
                printf("error: usage - set <delta_t|r1|r2|c1|vp|vn1|vn2> <value>\n");
            }
        } else if (strcmp(cmd_str, "run") == 0 || strcmp(cmd_str, "r") == 0) {
            long double secs = 0;
            int64_t steps;
            if (arg1_str && sscanf(arg1_str, "%Lg", &secs) != 1) {
                printf("error: not a number '%s'\n", arg1_str);
                continue;
            }
            steps = secs / delta_t;
            if (steps <= 0) steps = 1;
            run(steps);
            printf("info: %Lg  (%Lg)\n", (vn1-vn2)/300, 1-expl(-1));
            printf("\n");
            show();
        } else if (strcmp(cmd_str, "show") == 0) {
            show();
        } else if (strcmp(cmd_str, "reset") == 0) {
            model_t = 0;
            vn1 = vn2 = 0;
            ir1 = ir2 = ic1 = 0;
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

void run(int64_t steps) 
{
    int64_t count, i;
    long double vn1_next, vn2_next;
    long double ir1_next, ic1_next, ir2_next;
    time_t time_print_progress=time(NULL), time_now;
    bool print_progress_flag;

    static long double dvdt1, dvdt2;

    if (verbose) {
        printf("      TIME        VN1        VN2        IR1        IC1        IR2\n");
    }

    for (i = 0; i < steps; i++) {
        // check for request to interrupt
        if (interrupt) {
            printf("interrupt at model_t %Lg\n", model_t);
            interrupt = false;
            return;
        }

        // print progress every 10 seconds
        print_progress_flag = false;
        time_now = time(NULL) ;
        if (time_now - time_print_progress > 10) {
            time_print_progress = time_now;
            print_progress_flag = true;
        }
        if (print_progress_flag) {
            printf("model_t = %Lg\n", model_t);
        }

        // update time
        model_t += delta_t;

        // determine the next voltages and currents,
        // exit the loop when all node current sums are close to 0
        count = 0;
        while (true) {
            vn1_next = node_eval(vn1, vp, dvdt2, r1, c1);
            vn2_next = node_eval(vn2, vg, dvdt1, r2, c1);
            dvdt1 = (vn1_next - vn1) / delta_t;
            dvdt2 = (vn2_next - vn2) / delta_t;

            ir1_next = (vp - vn1_next) / r1;
            ic1_next = c1 * (dvdt1 - dvdt2);
            ir2_next = (vn2_next - vg) / r2;

            count++;
            if ((fabsl((ir1_next - ic1_next) / ic1_next) < .005) &&
                (fabsl((ir2_next - ic1_next) / ic1_next) < .005))
            {
                if (count > 10000) {
                    printf("WARNING - CURRENT BREAK count=%ld T=%Lg - %Lg %Lg %Lg - %Lg %Lg\n",
                           count, model_t, ir1_next, ic1_next, ir2_next, vn1_next, vn2_next);
                }
                break;
            }
            if (count == 1000000000L) {
                printf("WARNING - FORCE BREAK count=%ld T=%Lg - %Lg %Lg %Lg - %Lg %Lg\n",
                       count, model_t, ir1_next, ic1_next, ir2_next, vn1_next, vn2_next);
                break;
            }
            if ((count % 10000000L) == 0) {
                printf("INFO count=%ld T=%Lg - %Lg %Lg %Lg - %Lg %Lg\n",
                       count, model_t, ir1_next, ic1_next, ir2_next, vn1_next, vn2_next);
                if (interrupt) {
                    printf("interrupt at model_t %Lg\n", model_t);
                    interrupt = false;
                    return;
                }
            }
        }

#if 0
        long double sum_abs, sum;

        // determine the next currents;
        // this is a double check
        ir1_next = (vp - vn1_next) / r1;
        ic1_next = c1 * (dvdt1 - dvdt2);
        ir2_next = (vn2_next - vg) / r2;

        // warn if node current not close to zero;
        // this is a double check
        sum_abs = fabsl(ir1_next) + fabsl(ic1_next);
        sum     = ir1_next - ic1_next;
        if (sum_abs && fabsl(sum / sum_abs) > .01) {
            printf("WARNING - CURRENT FLUCTUATION-A T=%Lg - %Lg %Lg %Lg\n", 
                   model_t, ir1_next, ic1_next, ir2_next);
        }
        sum_abs = fabsl(ir2_next) + fabsl(ic1_next);
        sum     = ir2_next - ic1_next;
        if (sum_abs && fabsl(sum / sum_abs) > .01) {
            printf("WARNING - CURRENT FLUCTUATION-B T=%Lg - %Lg %Lg %Lg\n", 
                   model_t, ir1_next, ic1_next, ir2_next);
        }
#endif

        // print the next values
        if (verbose) {
            printf("%10Lg %10Lg %10Lg %10Lg %10Lg %10Lg\n",
                   model_t, vn1_next, vn2_next, ir1_next, ic1_next, ir2_next);
        }

        // init for next iteration
        vn1 = vn1_next;
        vn2 = vn2_next;
        ir1 = ir1_next;
        ir2 = ir2_next;
        ic1 = ic1_next;
    }

    if (verbose) {
        printf("      TIME        VN1        VN2        IR1        IC1        IR2\n");
    }
}

long double node_eval(
    long double v,
    long double v_r_other,
    long double dvdt_c_other,
    long double ohms,
    long double farads)
{
    long double num = 0;
    long double denom = 0;

    num += (v_r_other / ohms);
    num += (farads / delta_t) * v + farads * dvdt_c_other;   

    denom += (1 / ohms);
    denom += farads / delta_t;

    return num / denom;
}

// -----------------  SHOW CMD  ----------------------------------

void show(void)
{
    printf("time    = %Lg\n", model_t);
    printf("delta_t = %Lg\n", delta_t);
    printf("\n");
    printf("%12s %12Lg %12s %12Lg %12s %12Lg %12s\n",
           "", r1, "", c1, "", r2, "");
    printf("%12Lg %12s %12Lg %12s %12Lg %12s %12Lg\n",
           vp, "resistor", vn1, "capacitor", vn2, "resistor", vg);
    printf("%12s %12Lg %12s %12Lg %12s %12Lg %12s\n",
           "", ir1, "", ic1, "", ir2, "");
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
