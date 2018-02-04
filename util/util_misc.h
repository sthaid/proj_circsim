/*
Copyright (c) 2017 Steven Haid

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
*/

#ifndef __UTIL_MISC_H__
#define __UTIL_MISC_H__

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

// -----------------  GENERAL  -----------------------------------

// show the value of a define
#define SHOW_DEFINE(x) INFO("define %s = %s\n", #x, SHOW_DEFINE_STR(x))
#define SHOW_DEFINE_STR(x) #x

// for use in call to madvise XXX comment
#define PAGE_SIZE 4096L
#define ROUND_UP(x,n) (((uint64_t)(x) + ((uint64_t)(n) - 1)) & ~((uint64_t)(n) - 1))  // XXX pwr 2

// XXX use of int32_t vs int  in this directory

// -----------------  LOGGING  -----------------------------------

#define ENABLE_LOGGING_AT_DEBUG_LEVEL

#define INFO(fmt, args...) \
    do { \
        logmsg("INFO", __func__, fmt, ## args); \
    } while (0)
#define WARN(fmt, args...) \
    do { \
        logmsg("WARN", __func__, fmt, ## args); \
    } while (0)
#define ERROR(fmt, args...) \
    do { \
        logmsg("ERROR", __func__, fmt, ## args); \
    } while (0)

#ifdef ENABLE_LOGGING_AT_DEBUG_LEVEL
    #define DEBUG(fmt, args...) \
        do { \
            logmsg("DEBUG", __func__, fmt, ## args); \
        } while (0)
#else
    #define DEBUG(fmt, args...) 
#endif

#define BLANK_LINE \
    do { \
        logmsg("", "", "blankline"); \
    } while (0)

#define FATAL(fmt, args...) \
    do { \
        logmsg("FATAL", __func__, fmt, ## args); \
        exit(1); \
    } while (0)

void logmsg(char * lvl, const char * func, char * fmt, ...) __attribute__ ((format (printf, 3, 4)));

// -----------------  TIME  --------------------------------------

#define MAX_TIME_STR 50

uint64_t microsec_timer(void);
uint64_t get_real_time_us(void);
char * time2str(char * str, int64_t us, bool gmt, bool display_ms, bool display_date);

// -----------------  CONFIG READ/WRITE  --------------------------------------------

#define MAX_CONFIG_VALUE_STR 100

typedef struct {
    const char * name;
    char         value[MAX_CONFIG_VALUE_STR];
} config_t;

int config_read(char * config_path, config_t * config, int config_version);
int config_write(char * config_path, config_t * config, int config_version);

// -----------------  NETWORKING  ----------------------------------------

int getsockaddr(char * node, int port, struct sockaddr_in * ret_addr);
char * sock_addr_to_str(char * s, int slen, struct sockaddr * addr);
int do_recv(int sockfd, void * recv_buff, size_t len);
int do_send(int sockfd, void * send_buff, size_t len);

// -----------------  RANDOM NUMBERS  ------------------------------------

float random_range(float min, float max);
float random_triangular(float min, float max);
void random_vector(float magnitude, float * x, float * y, float * z);

// -----------------  MATH  ----------------------------------------------

bool solve_quadratic_equation(float a, float b, float c, float *x1, float *x2);
float hypotenuse(float x, float y, float z);

#endif
