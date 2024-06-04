#include "font_digits.h"

//Reset internal decode pointer to a given digit (which is
//supposed to be one of font_digit[] as defined in font_digit.h)
void font_digit_reset(const uint8_t *digit);

//Get the next pixel of the selected digit
int font_digit_get_pixel();

//Get the next 8 pixels, packed into a byte, of the
//selected digit.
uint8_t font_digit_get_byte();