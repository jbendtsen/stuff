/*
   The purpose of this program is to convert numbers between bases and to and from strings.
   For base conversion, this program can convert to and from any base from 2 to 36.

   The scale of this program is due to the fact that input and output numbers
   are not used as traditional integers but instead as arrays of digits, allowing for
   numbers virtually unlimited in size. A consequence of the decision to use digit arrays
   over integers is that all arithmetic and other such operations must be written from
   the ground up, as standard integers function completely differently to an array of digits.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char u8;

// The digit array struct
typedef struct {
	u8 *d;
	int len;
	int sign;
	int base;
} da_t;

// --- General Purpose Functions ---

/*
   This one function can convert characters to one-digit numbers and back again.
   This is due to the fact that no digit used in a digit array exceeds a decimal value of 35,
   and no character has an integer value of less than decimal 48.
   On failure, -2 is returned in case the return value is incremented before being checked.
*/
int charconv(int c) {
	if (c < 10) return c + '0';
	if (c >= 10 && c < 36) return c + ('a' - 10);

	if (c >= '0' && c <= '9') return c - '0';
	if (c >= 'A' && c <= 'Z') return c - ('A' - 10);
	if (c >= 'a' && c <= 'z') return c - ('a' - 10);

	return -2;
}

/*
   This function takes a string (src) and reviews it to check for any
   characters that can't be converted to a digit of a certain base (base).
   Every invalid character gets omitted for the output string (dst).
*/
void prune(char *dst, char *src, int base) {
	if (!dst || !src || base < 2 || base > 36) return;

	char set[36];
	int i;
	for (i = 0; i < base; i++) set[i] = charconv(i);

	char *d = dst;
	char *s = src;
	while (*s) {
		for (i = 0; i < base; i++) {
			if (*s == set[i]) {
				*d++ = *s;
				break;
			}
		}
		s += 1;
	}
}

/*
   To demonstrate how much simpler this program would be if it just used integers instead of digit arrays,
   this function converts an integer to a string of digits in any base from 2 to 36.
   It can be considered as inverse strtol().
*/
void int_to_str(char *str, int base, int value) {
	if (base < 2 || base > 36 || value < 0) return;
	if (value == 0) {
		str[0] = '0';
		return;
	}

	int buf[32] = {0};
	int bp = 0;
	while (value && bp < 32) {
		buf[bp++] = value % base;
		value /= base;
	}

	bp--;
	char *p = str;
	while (bp >= 0) {
		*p++ = charconv(buf[bp]);
		bp--;
	}
}

// --- Digit Array Functions ---

int count_leading_zeroes(da_t *array) {
	if (!array || !array->d) return 0;
	int i;
	for (i = 0; i < array->len && !array->d[i]; i++);
	return i;
}

/*
   This function changes the number of digits in a digit array by a certain value (shift).
   If 'shift' is a positive value, it will insert that many zeroes at the start of the digit array.
   If 'shift' is a negative value, it will remove that many digits, starting from the highest digit.
   Example: | input | shift | output  |
            | 12345 |   2   | 0012345 |
            | 12345 |  -2   |   345   |
*/
void resize_digit_array(da_t *array, int shift) {
	if (!array || !array->d || array->len < 1 || shift <= -array->len || shift == 0) return;

	int i, len = array->len + shift;
	u8 *out = NULL;
	if (shift > 0) {
		out = realloc(array->d, len);
		for (i = array->len - 1; i >= 0; i--) out[i+shift] = out[i];
		for (i = 0; i < shift; i++) out[i] = 0;
	}
	else {
		shift = -shift;
		for (i = shift; i < array->len; i++) array->d[i-shift] = array->d[i];
		out = realloc(array->d, len);
	}
	array->len = len;
	array->d = out;
}

/*
   This function removes all leading zeroes from a digit array.
   Unless the whole digit array equates to zero, in which case all digits but the last are removed.
*/
void prune_digit_array(da_t *array) {
	int cl = count_leading_zeroes(array);
	if (cl == array->len) cl--;
	resize_digit_array(array, -cl);
}

/*
   This function compares the values of two digit arrays, a and b, to determine if a is less than, greater than or equal to b.
   If a = b, the return value is 0.
   If a < b, the return value is 1.
   If a > b, the return value is 2.
   In case of an error, the return value is -1.
   If 'mode' is non-zero, a comparison with |a| and |b| is made instead of with a and b.
*/
int compare_digit_array(da_t *a, da_t *b, int mode) {
	if (a->base != b->base) return -1; // Don't make a comparison if the bases aren't the same

	// Eliminate cases where digit by digit comparison isn't necessary
	int al = count_leading_zeroes(a);
	int bl = count_leading_zeroes(b);
	int asz = a->len - al;
	int bsz = b->len - bl;
	if (asz == 0 && bsz == 0) return 0; // a == b
	if (!mode) {
		if (a->sign < 0 && b->sign >= 0) return 1; // a < b
		if (a->sign >= 0 && b->sign < 0) return 2; // a > b
	}
	if (asz < bsz) return 1;
	if (asz > bsz) return 2;

	// Go over each digit to see if any of them differ.
	int i;
	for (i = 0; i < asz; i++) {
		if (a->d[al + i] != b->d[bl + i]) {
			int r = 0;
			if (!mode && a->sign < 0) r = 1;
			if (a->d[al + i] > b->d[bl + i]) r = !r;
			return r+1;
		}
	}
	return 0;
}

// This function creates a digit array from scratch with a value of 0.
void new_digit_array(da_t *array, int base) {
	if (array->d) free(array->d);
	array->d = calloc(1, 1);
	array->len = 1;
	array->sign = 1;
	array->base = base;
}

/*
   This function makes a copy an existing digit array. Due to the nature of pointers and memory allocation,
   this is not quite as simple as saying a "da_t b = a".
*/
void copy_digit_array(da_t *out, da_t *in) {
	if (!out || !in) return;
	if (out->d) {
		free(out->d);
		out->d = NULL;
	}
	memcpy(out, in, sizeof(da_t));
	out->d = calloc(out->len, 1);
	memcpy(out->d, in->d, out->len);
}

// Frees the contents of a digit array struct
void delete_digit_array(da_t *array) {
	if (array->d) {
		memset(array->d, 0, array->len);
		free(array->d);
	}
	memset(array, 0, sizeof(da_t));
}

/*
   This function adds two digit arrays to create a third for output. The same digit array can be used more than once.
   The basic format is 'out' = 'in1' + 'in2', where if 'in2' is negative, subtraction is performed instead.
   It can take all combinations of positive and negative numbers and always produce the correct results.
*/
void add_digit_array(da_t *out, da_t *in1, da_t *in2) {
	if (!out || !in1 || !in2 || (!in1->d && !in2->d) || (!in1->len && !in2->len) || in1->base != in2->base) return;

	int base = (in1->d && in1->len) ? in1->base : in2->base;
	if (base < 2 || base > 36) return;

	if (!in1->d) new_digit_array(in1, in2->base);
	if (!in2->d) new_digit_array(in2, in1->base);

	if (out->d && out->d != in1->d && out->d != in2->d) {
		free(out->d);
		out->d = NULL;
	}

	// Create output digit array;
	da_t temp = {0};
	temp.base = base;

	// Swap 'in1' and 'in2' if 'in1' is smaller than 'in2' so that 'a' is always the largest
	da_t *a = in1, *b = in2;
	if (compare_digit_array(a, b, 1) == 1) { // if |a| < |b|
		void *ptr = a;
		a = b;
		b = (da_t*)ptr;
	}
	// Make the output digit array filled with 0s to the size of the largest digit array
	temp.len = a->len;
	temp.d = calloc(temp.len, 1);

	// so = output sign, sa = first input sign, sb = second input sign
	int so = 1, sa = a->sign < 0 ? -1 : 1, sb = b->sign < 0 ? -1 : 1;

	// If the sign for first input digit array is negative, flip all of the signs, including the output sign.
	// This ensures that adding negative numbers in various combinations works more effectively.
	if (sa < 0) {
		sa = -sa;
		sb = -sb;
		so = -so;
	}
	temp.sign = so;

	// Start from the 1s digit (at the end)
	u8 *pa = &a->d[a->len - 1], *pb = &b->d[b->len - 1];

	int i, carry = 0;
	for (i = a->len-1; i >= 0; i--, pa--, pb--) {
		int na = pa < a->d ? 0 : *pa;
		int nb = pb < b->d ? 0 : *pb;
		int n = na + (nb * sb) + carry;
		carry = n < 0 ? -1 : n / base;

		int digit = (n < 0 ? n+base : n) % base;
		temp.d[i] = digit;

		if (i == 0 && carry > 0) {
			resize_digit_array(&temp, 1);
			temp.d[0] = carry;
		}
	}
	prune_digit_array(&temp);
	// If temp == 0, make it positive. We don't want -0 as a number.
	if (temp.d[0] == 0 && temp.len == 1) temp.sign = 1;

	if (out == in1 || out == in2) delete_digit_array(out);
	memcpy(out, &temp, sizeof(da_t));
}

/*
   This function converts a digit array of a certain base to another base,
     making it conceptually the most important function.

   It works by breaking down the input digit array into
     multiples of powers of the output base
     (eg. for base 4: 1, 2, 3, 4, 8, 12, 16, 32, 48, 64, 128...)
     and adds them successively to the output digit array.

   Since implementing true division for this task would be somewhat impractical,
     the algorithm used here builds a list of the multiples while comparing them
     to the input digit array to see when the current multiple becomes larger,
     at which point the previous multiple is subtracted from the input digit array
     and added to the output digit array.
*/
void convert_digit_array_base(da_t *dst, da_t *src, int base) {
	if (!dst || !src || base < 2 || base > 36) return;

	da_t in = {0};
	memcpy(dst, &in, sizeof(da_t));
	copy_digit_array(&in, src);
	in.sign = 1;

	dst->base = base;
	dst->sign = src->sign;

	da_t *in_mul = calloc(1, sizeof(da_t));
	da_t *out_mul = calloc(1, sizeof(da_t));
	new_digit_array(&in_mul[0], in.base);
	new_digit_array(&out_mul[0], base);

	// Initialise them to 1 so they can be used to start the iterative addition sequence
	in_mul[0].d[0] = 1;
	out_mul[0].d[0] = 1;
	int n_muls = 1;

	int i, c = 0;
	while (count_leading_zeroes(&in) != in.len) {
		while (1) {
			if (c >= n_muls) {
				c = n_muls - 1;
				n_muls++;

				in_mul = realloc(in_mul, n_muls * sizeof(da_t));
				out_mul = realloc(out_mul, n_muls * sizeof(da_t));
				memset(&in_mul[n_muls-1], 0, sizeof(da_t));
				memset(&out_mul[n_muls-1], 0, sizeof(da_t));

				// Get the latest power of the output base
				int pm = c - (c % (base-1));
				add_digit_array(&in_mul[c+1], &in_mul[c], &in_mul[pm]);
				add_digit_array(&out_mul[c+1], &out_mul[c], &out_mul[pm]);

				c++;
			}
			if (compare_digit_array(&in, &in_mul[c], 1) == 1) break; // if in < in_mul[c]
			c++;
		}
		c--;

		in_mul[c].sign = -1;
		add_digit_array(&in, &in, &in_mul[c]);
		in_mul[c].sign = 1;

		add_digit_array(dst, dst, &out_mul[c]);
		c = 0;
	}
	delete_digit_array(&in);
	for (c = 0; c < n_muls; c++) {
		delete_digit_array(&in_mul[c]);
		delete_digit_array(&out_mul[c]);
	}
	free(in_mul);
	free(out_mul);
}

// This function converts a string of digits to a digit array
void str_to_digit_array(da_t *array, char *str) {
	if (!array | !str) return;

	array->sign = 1;
	if (str[0] == '-') {
		array->sign = -1;
		str++;
	}

	array->len = strlen(str);
	array->d = calloc(array->len, 1);

	int i;
	for (i = 0; str[i]; i++) array->d[i] = charconv(str[i]);
}

// And this function does the exact opposite
char *digit_array_to_str(da_t *array) {
	int ssz = array->len + (array->sign < 0);
	char *str = calloc(ssz+1, 1), *p = str;
	if (array->sign < 0) *p++ = '-';

	int i;
	for (i = 0; i < array->len; i++) *p++ = charconv(array->d[i]);
	return str;
}

/*
   This function acts as a shortcut; it converts a string to a digit array,
   changes the base from 'in_base' to 'out_base' and converts the result back to a string.
*/
char *convert_base(char *in_str, int in_base, int out_base) {
	if (!in_str || in_base < 2 || in_base > 36 || out_base < 2 || out_base > 36) return NULL;

	da_t in_num = {0}, out_num = {0};
	in_num.base = in_base;

	char *pruned = calloc(strlen(in_str) + 1, 1);
	prune(pruned, in_str, in_base);

	str_to_digit_array(&in_num, in_str);
	if (!in_num.len) return NULL;

	convert_digit_array_base(&out_num, &in_num, out_base);

	char *out_str = digit_array_to_str(&out_num);

	free(pruned);
	delete_digit_array(&in_num);
	delete_digit_array(&out_num);

	return out_str;
}

int main(int argc, char **argv) {
	if (argc < 3) {
		printf("Value Converter\n\nInvalid arguments\n"
			"Usage: %s <mode> <values ...>\n"
			"Mode:\n"
			"  The <mode> field determines how <value> is interpreted,\n"
			"  converted and output to the screen. It takes the form of\n"
			"  two characters; the first describes the input type and the\n"
			"  second describes the output type. If a character is not a\n"
			"  number or letter, that character will represent a string.\n"
			"  Else, the character denotes the base - 1 in base 36, eg.:\n\n"
			"  1: base 2\n"
			"  ...\n"
			"  9: base 10\n"
			"  a: base 11\n"
			"  ...\n"
			"  f: base 16\n"
			"  ...\n"
			"  z: base 36\n\n", argv[0]);
		return 1;
	}
	if (strlen(argv[1]) != 2) {
		printf("Invalid conversion mode '%s':\n"
			"Must have exactly 2 characters (not %d)\n", argv[1], strlen(argv[1]));
		return 2;
	}

	// Conveniently, we can still use charconv() to get the operation mode of the program.
	// This is why it's important that it returns -2 on failure
	int in_base = charconv(argv[1][0]) + 1;
	int out_base = charconv(argv[1][1]) + 1;
	int i, j;

	if (in_base == out_base) {
		for (i = 2; i < argc; i++) printf("%s ", argv[i]);
		printf("\b\n");
	}
	else if (in_base < 0) { // input string
		char *str = NULL;
		int c = 0, s = 0;
		i = 2;
		while (i < argc) {
			if ((c & 0x1f) == 0) {
				str = realloc(str, c+33);
				memset(str+c, 0, 33);
			}
			if (!argv[i][s]) {
				str[c++] = ' ';
				i++;
				s = 0;
			}
			else str[c++] = argv[i][s++];
		}
		str[c--] = 0;

		int p = 2;
		printf("  ");
		for (i = 0; i < c; i++) {
			char v[10] = {0};
			int_to_str(v, out_base, str[i]);
			p += printf("%s ", v);
			if (p > 70) {
				p = 2;
				printf("\n  ");
			}
		}
		putchar('\n');
		free(str);
	}
	else if (out_base < 0) { // output string
		int buf_sz = 0;
		u8 *buf = NULL;
		char *arg = NULL;
		for (i = 2; i < argc; i++) {
			char *str = convert_base(argv[i], in_base, 16);
			if (!str) continue;

			int s_sz = strlen(str);
			int a_sz = (s_sz + (s_sz % 2)) / 2;
			u8 *a = calloc(a_sz, 1);

			char *p = str + s_sz - 1;
			int c = 0;
			while (p-str >= 0) {
				a[c/2] |= charconv(*p--) << ((c%2) * 4);
				c++;
			}
			free(str);

			buf = realloc(buf, buf_sz + a_sz);
			memcpy(buf+buf_sz, a, a_sz);
			buf_sz += a_sz;
			free(a);
		}
		if (buf) {
			fwrite(buf, 1, buf_sz, stdout);
			free(buf);
		}
		putchar('\n');
	}
	else { // base conversion
		char *numbers = NULL, *str = NULL;
		int nc = 0, p = 0, s = 0;
		for (i = 2; i < argc; i++) {
			char *str = convert_base(argv[i], in_base, out_base);
			if (!str) continue;

			s = strlen(str) + 1;
			numbers = realloc(numbers, p+s);
			memcpy(numbers+p, str, s);
			free(str);

			p += s;
			nc++;
		}

		char *ptr = numbers;
		int x = 0;
		for (i = 0; i < nc; i++) {
			ptr += printf("%s ", ptr);
			if (i >= nc-1) break;
			x += strlen(&numbers[i+1]) + 1;
			if (x >= 79) {
				putchar('\n');
				x = 0;
			}
		}
		putchar('\n');
		free(numbers);
	}
	return 0;
}
