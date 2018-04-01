#include <stdio.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <assert.h>
#include <math.h>

typedef struct {
    int64_t max_values;
    long double sum;
    int64_t count;
    long double current;
    long double values[0];
} ma_t;
long double moving_average(long double val, ma_t *ma);
long double moving_average_query(ma_t *ma);
ma_t * moving_average_alloc(int32_t max_values);
void moving_average_free(ma_t * ma) ;
void moving_average_reset(ma_t * ma);

typedef struct {
    long double time_span;
    int64_t max_bins;
    ma_t * ma;
    bool first_call;
    int64_t last_idx;
    long double sum;
    int64_t count;
    long double current;
} tma_t;
long double timed_moving_average(long double val, long double time_arg, tma_t *tma);
long double timed_moving_average_query(tma_t *tma);
tma_t * timed_moving_average_alloc(long double time_span, int64_t max_bins);
void timed_moving_average_free(tma_t * tma);
void timed_moving_average_reset(tma_t * tma);

// - - - - - - - - - - - - - - - - - - - - 

long double moving_average(long double val, ma_t *ma)
{
    int64_t idx;

    idx = (ma->count % ma->max_values);
    ma->sum += (val - ma->values[idx]);
    ma->values[idx] = val;
    ma->count++;
    ma->current = ma->sum / (ma->count <= ma->max_values ? ma->count : ma->max_values);
    return ma->current;
}

long double moving_average_query(ma_t *ma)
{
    return ma->current;
}

ma_t * moving_average_alloc(int32_t max_values)
{
    ma_t * ma;
    size_t size = sizeof(ma_t) + max_values * sizeof(ma->values[0]);

    ma = malloc(size);
    assert(ma);

    ma->max_values = max_values;

    moving_average_reset(ma);

    return ma;
}

void moving_average_free(ma_t * ma) 
{
    free(ma);
}

void moving_average_reset(ma_t * ma)
{
    ma->sum = 0;
    ma->count = 0;
    ma->current = NAN;
    memset(ma->values,0,ma->max_values*sizeof(ma->values[0]));
}

// - - - - - - - - - - - - - - - - - - - - 

long double timed_moving_average(long double val, long double time_arg, tma_t *tma)
{
    int64_t idx, i;
    long double maq;

    idx = time_arg * (tma->max_bins / tma->time_span);

    if (idx == tma->last_idx || tma->first_call) {
        tma->sum += val;
        tma->count++;
        tma->last_idx = idx;
        tma->first_call = false;
    } else if (idx > tma->last_idx) {
        for (i = tma->last_idx; i < idx; i++) {
            long double v;
            if (i == idx-1 && tma->count > 0) {
                v = tma->sum / tma->count;
            } else {
                v = 0;
            }
            moving_average(v,tma->ma);
        }
        tma->sum = 0;
        tma->count = 0;
        tma->last_idx = idx;
    } else {
        // time can't go backwards
        assert(0);
    }

    maq = moving_average_query(tma->ma);
    tma->current = (isnan(maq) ? tma->sum/tma->count : maq);

    return tma->current;
}

long double timed_moving_average_query(tma_t *tma)
{
    return tma->current;
}

tma_t * timed_moving_average_alloc(long double time_span, int64_t max_bins)
{
    tma_t * tma;

    tma = malloc(sizeof(tma_t));
    assert(tma);

    tma->time_span = time_span;
    tma->max_bins  = max_bins;
    tma->ma        = moving_average_alloc(max_bins);

    timed_moving_average_reset(tma);

    return tma;
}

void timed_moving_average_free(tma_t * tma)
{
    if (tma == NULL) {
        return;
    }

    free(tma->ma);
    free(tma);
}

void timed_moving_average_reset(tma_t * tma)
{
    tma->first_call = true;
    tma->last_idx   = 0;
    tma->sum        = 0;
    tma->count      = 0;
    tma->current    = NAN;

    moving_average_reset(tma->ma);
}

// - - - - - - - - - - - - - - - - - - - - 

int32_t main(int argc, char **argv)
{
    ma_t * ma;
    tma_t * tma;
    int32_t i, count;
    long double val, avg, t;

    printf("MOVING AVG TEST\n");
    ma = moving_average_alloc(10);
    for (i = 0; i < 20; i++) {
        val = i;
        avg = moving_average(val, ma);
        printf("i=%d  val=%Lf  avg=%Lf\n", i, val, avg);
    }
    printf("\n");

    printf("TIMED MOVING AVG TEST\n");
    tma = timed_moving_average_alloc(1,1000);
    for (count = 0, t = 0; count <= 20001; t += .0000999999999L, count++) {
        val = t * 10;   // 0 to 20
        avg = timed_moving_average(val, t, tma);
        printf("t=%.10Lf  val=%.10Lf  avg=%.10Lf\n", t, val, avg);
    }
    printf("\n");

    return 0;
}

