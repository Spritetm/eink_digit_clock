#include <stdio.h>
#include "font_digits.h"

static const uint8_t *cur_digit_data_ptr;
static int cur_run_len;
static int cur_color;

static int get_new_run() {
	int r=0;
	int shift=0;
	while(1) {
		uint8_t v=*cur_digit_data_ptr++;
		fprintf(stderr, "Data %x\n", v);
		if (v<0xf0) {
			r|=(v<<shift);
			fprintf(stderr, "Run 0x%X digits\n", r);
			return r;
		} else {
			r|=((v&0xf)<<shift);
			shift+=4;
		}
	}
}

void font_digit_reset(const uint8_t *digit) {
	cur_digit_data_ptr=digit;
	cur_color=0;
	cur_run_len=get_new_run();
}

int font_digit_get_pixel() {
	if (cur_run_len==0) {
		cur_run_len=get_new_run();
		cur_color=(cur_color==0)?1:0;
	}
	cur_run_len--;
	return cur_color;
}
