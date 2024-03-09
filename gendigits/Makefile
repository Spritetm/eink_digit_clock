
font_test: font_decode.c font_digits.c test.c
	$(CC) -o $@ $^

font_digits.c: genfontinc.py
	./genfontinc.py > $@