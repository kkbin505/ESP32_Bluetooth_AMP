#ifndef LMS_FILTER_H
#define LMS_FILTER_H

#ifdef __cplusplus
extern "C" {
#endif

// 初始化
void lms_init(int filter_len, float mu);

// 单次更新，返回抗噪输出
float lms_calculate(float x, float d);

#ifdef __cplusplus
}
#endif

#endif // LMS_FILTER_H
