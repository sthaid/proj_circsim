#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <math.h>

long double delta_t = .001;
long double model_t;

void run(int32_t steps);
long double node_eval(
    long double v,
    long double v_r_other,
    long double dvdt_c_other,
    long double ohms,
    long double farads);
void basic_exponential_smoothing(long double x, long double *s, long double alpha);

// ---------------------------------------------------------------

int32_t main(int32_t argc, char **argv)
{
    char s[100], *cmd, *arg;
    long double argval;
    int32_t steps;
    
    // loop 
    while (true) {
        // read command
        // - dt <secs>
        // - r <secs>    or 0 means 1 interval
        printf("? ");
        if (fgets(s, sizeof(s), stdin) == NULL) {
            break;
        }
        cmd = strtok(s, " \n");
        if (cmd == NULL) {
            continue;
        }
        argval = 0;
        arg = strtok(NULL, " \n");
        if (arg && sscanf(arg, "%Lf", &argval) != 1) {
            printf("error: %s not a number\n", arg);
            continue;
        }

// XXX cmds to set r1,r2,c1

        // process command
        if (strcmp(cmd, "dt") == 0) {
            delta_t = argval;
        } else if (strcmp(cmd, "r") == 0) {
            steps = argval / delta_t;
            if (steps == 0) steps = 1;
            run(steps);
        } else {
            printf("error: invalid command\n");
        }
    }

    return 0;
}

// ---------------------------------------------------------------

//    power  ----  resistor  ---- cap  ---- resistor  ---- ground
//
//     vp                    vn1       vn2             vg
//     300v                  200v      100v            0v
//
//                   r1           c1           c2
//                   10          .05           10
//
//
// i = C dv/dt
// i = v / r

long double vp  = 300;
long double vn1 = 200;
long double vn2 = 100;
long double vg  = 0;
#if 0
long double r1  = 10;
long double c1  = .05;
long double r2  = 10;
#else
long double r1  = 19;
long double c1  = .05;
long double r2  = 1;
#endif

void run(int32_t steps) 
{
    int32_t i, iter;
    long double dvdt1, dvdt2;
    long double vn1_next, vn2_next, dvdt1_next, dvdt2_next;
    long double ir1, ic1, ir2;

// XXX print r1,r2,c1, and initial voltages
    for (i = 0; i < steps; i++) {
        model_t += delta_t;
        printf("model_t = %Lf\n", model_t);

        dvdt1 = 0;
        dvdt2 = 0;
        dvdt1_next = 0;
        dvdt2_next = 0;
        printf("       VN1        VN2        IR1        IC1        IR2\n");
        for (iter = 0; iter < 1000; iter++) {
            // determine the next node voltages
            vn1_next = node_eval(vn1, vp, dvdt2, r1, c1);
            vn2_next = node_eval(vn2, vg, dvdt1, r2, c1);

            // xxx
#if 0
            dvdt1_next = (vn1_next - vn1) / delta_t;
            dvdt2_next = (vn2_next - vn2) / delta_t;
#else
            basic_exponential_smoothing((vn1_next - vn1) / delta_t, &dvdt1_next, 0.5);
            basic_exponential_smoothing((vn2_next - vn2) / delta_t, &dvdt2_next, 0.5);
#endif

            // determine the next component currents
            ir1 = (vp - vn1_next) / r1;
            ic1 = c1 * (dvdt1_next - dvdt2_next);
            ir2 = (vn2_next - vg) / r2;

            // print the next values
            printf("%10Lf %10Lf %10Lf %10Lf %10Lf\n",
                   vn1, vn2, ir1, ic1, ir2);

            // init for next iteration
            dvdt1 = dvdt1_next;
            dvdt2 = dvdt2_next;
            vn1 = vn1_next;
            vn2 = vn2_next;
        }
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

void basic_exponential_smoothing(long double x, long double *s, long double alpha)
{
    long double s_last = *s;
    *s = alpha * x + (1 - alpha) * s_last;
}

