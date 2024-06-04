#include <stdint.h>

//This routine returns the time, in us, we need to wait for the indicated digit
//to flip over, given the wallclock time in h,m,s,us.

int64_t wait_time_us_for_digit(int cur_h, int cur_m, int cur_s, int cur_us, int digit);
