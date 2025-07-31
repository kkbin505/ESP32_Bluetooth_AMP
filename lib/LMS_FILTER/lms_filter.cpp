#include "lms_filter.h"
#include <stdlib.h>
#include <string.h>  // 增加内存操作函数


// 静态变量初始化（避免未定义行为）
static float *w = NULL;
static float *x_hist = NULL;
static int FILTER_LEN = 0;  // 初始化为0，确保初始化后才使用
static int hist_index = 0;
static float step_mu = 0.0f;
#define MAX_WEIGHT 100.0f

// 初始化函数增强：增加参数校验和内存清零
void lms_init(int filter_len, float mu)
{
    // 校验滤波器长度（避免无效值）
    if (filter_len <= 0) {
        filter_len = 64;  // 默认长度
    }
    FILTER_LEN = filter_len;
    step_mu = mu;
    hist_index = 0;

    // 释放原有内存（避免内存泄漏）
    if (w != NULL) {
        free(w);
        w = NULL;
    }
    if (x_hist != NULL) {
        free(x_hist);
        x_hist = NULL;
    }

    // 分配内存并强制清零（calloc确保初始值为0）
    w = (float*)calloc(FILTER_LEN, sizeof(float));
    x_hist = (float*)calloc(FILTER_LEN, sizeof(float));

    // 可选：设置初始权重（简单直通滤波器，便于测试）
    if (w != NULL && FILTER_LEN > 0) {
        w[0] = 1.0f;  // 仅第一阶有系数，其余为0
    }
}

// 计算函数优化：修复索引逻辑，移除权重更新
float lms_calculate(float x, float d)
{
    // 检查初始化状态（未初始化则直接返回误差）
    if (w == NULL || x_hist == NULL || FILTER_LEN <= 0) {
        return d - x;  // 退化处理：假设x直接作为参考
    }

    // 更新输入历史缓冲区（当前值存入索引位置）
    x_hist[hist_index] = x;

    // 计算滤波器输出y（使用固定权重）
    float y = 0.0f;
    int idx = hist_index;  // 从当前索引开始
    for (int i = 0; i < FILTER_LEN; i++) {
        y += w[i] * x_hist[idx];
        // 索引向前移动（循环缓冲区）
        idx = (idx - 1 + FILTER_LEN) % FILTER_LEN;  // 避免负数索引
    }

    // 计算误差（不更新权重）
    float e = d - y;

    // idx = hist_index;
    // for (int i = 0; i < FILTER_LEN; i++) {
    //     w[i] += step_mu * e * x_hist[idx];
    //     if (w[i] > MAX_WEIGHT) w[i] = MAX_WEIGHT;
    //     else if (w[i] < -MAX_WEIGHT) w[i] = -MAX_WEIGHT;
    //     idx = (idx - 1 + FILTER_LEN) % FILTER_LEN;啊
    // }

    // hist_index = (hist_index + 1) % FILTER_LEN;

    return e;
}

// 新增：释放资源函数（便于动态重新初始化）
void lms_deinit()
{
    if (w != NULL) {
        free(w);
        w = NULL;
    }
    if (x_hist != NULL) {
        free(x_hist);
        x_hist = NULL;
    }
    FILTER_LEN = 0;
    hist_index = 0;
    step_mu = 0.0f;
}