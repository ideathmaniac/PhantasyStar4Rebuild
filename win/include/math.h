#ifndef W_MATH_H
#define W_MATH_H
#define M_PI 3.14159265358979323846
double sin(double);
double cos(double);
double log(double);
double pow(double, double);
double sqrt(double);
double floor(double);
double atan2(double, double);
double exp(double);
double ldexp(double, int);
static inline double fabs(double x) { return x < 0 ? -x : x; }
static inline long lrintf(float x) { return (long)(x < 0 ? x - 0.5f : x + 0.5f); }
#endif
