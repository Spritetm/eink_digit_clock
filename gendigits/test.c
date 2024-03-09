#include <stdlib.h>
#include <stdint.h>
#include <stdio.h>
#include "font_decode.h"
#include "font_digits.h"


int w=240;
int h=416;

int main(int argc, char **argv) {
	int dig=atoi(argv[1]);
	printf("P1\n%d %d\n", h, w);
	font_digit_reset(font_digits[dig]);
	for (int x=0; x<w; x++) {
		for (int y=0; y<h; y++) {
			printf("%d ", font_digit_get_pixel());
		}
		printf("\n");
	}
	return 0;
}