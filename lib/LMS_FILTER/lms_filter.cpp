#include "lms_filter.h"
#include <stdlib.h>

static float *w = NULL;
static float *x_hist = NULL;
static int FILTER_LEN = 64;
static int hist_index = 0;
static float step_mu = 0.000001f;

void lms_init(int filter_len, float mu)
{
    FILTER_LEN = filter_len;
    step_mu = mu;
    hist_index = 0;

    if (w) free(w);
    if (x_hist) free(x_hist);

    w = (float*)calloc(FILTER_LEN, sizeof(float));
    x_hist = (float*)calloc(FILTER_LEN, sizeof(float));
}

// LMS 计算
float lms_caculate(float x, float d)
{
    x_hist[hist_index] = x;

    // y = Σ w_i * x_hist
    float y = 0.0f;
    int idx = hist_index;
    for (int i = 0; i < FILTER_LEN; i++) {
        y += w[i] * x_hist[idx];
        if (--idx < 0) idx = FILTER_LEN - 1;
    }

    float e = d - y;  // 误差

    // 更新权值
    idx = hist_index;
    for (int i = 0; i < FILTER_LEN; i++) {
        w[i] += step_mu * e * x_hist[idx];
        if (--idx < 0) idx = FILTER_LEN - 1;
    }

    hist_index++;
    if (hist_index >= FILTER_LEN) hist_index = 0;

    return e;
}
