#include <stdio.h>
#include <string.h>

#define BUF_SIZE 1024

typedef unsigned long long u64;

void print_help(const char *prog)
{
	fprintf(stderr,
		"Convert binary to text, for easier post-processing with eg. grep\n"
		"Usage: %s [options]\n"
		"Options:\n"
		"   -x\n"
		"   --hex\n"
		"      Print data as hex\n"
		"      Overrides the --sparse and --replace options\n"
		"   -s\n"
		"   --sparse\n"
		"      Skip non-ascii sections of the input\n"
		"   -a <base,digits>\n"
		"   --addr-base <base,digits>\n"
		"      Print addresses in the base <base> with a minimum of <digits> digits\n"
		"      eg. -a 10\n"
		"          -a 16,8\n"
		"   -b <every,from>\n"
		"   --break <every,from>\n"
		"      Insert a newline every <every> bytes from offset <from>\n"
		"      eg. -b 16,7\n"
		"          -b 32\n"
		"      Defaults to 0,0\n"
		"   -r <character>\n"
		"   --replace <character>\n"
		"      Replace non-ascii characters with <character>\n"
		"      Defaults to \\n (newline)\n"
		"   -i <file>\n"
		"   --input <file>\n"
		"      Specify input file\n"
		"      Defaults to stdin\n"
		"   -h\n"
		"   --help\n"
		"      Print this help menu\n"
	, prog);
}

void get_numbers(char *str, int *first, int *second)
{
	*first = 0;
	if (second) *second = 0;

	int *n = first;
	for (int i = 0; str[i]; i++) {
		if (str[i] >= '0' && str[i] <= '9') {
			*n *= 10;
			*n += str[i] - '0';
		}
		else {
			if (second)
				n = second;
			else
				break;
		}
	}
}

int main(int argc, char **argv)
{
	FILE *input = stdin;
	char *in_name = NULL;
	int offset_base = 0;
	int offset_digits = 0;
	int should_skip_non_ascii = 0;
	int nl_every = 0;
	int nl_from = 0;
	int hexdump = 0;
	char replace_ch = '\n';

	for (int i = 1; i < argc; i++) {
		int len = strlen(argv[i]);
		if (len < 2 || argv[i][0] != '-') {
			in_name = argv[i];
			continue;
		}

		if (len > 2 && argv[i][1] == '-') {
			char *opt = argv[i] + 2;
			if (!strcmp(opt, "help")) {
				print_help(argv[0]);
				return 0;
			}
			else if (!strcmp(opt, "hex")) {
				hexdump = 1;
			}
			else if (!strcmp(opt, "sparse")) {
				should_skip_non_ascii = 1;
			}
			else if (i < argc-1) {
				if (!strcmp(opt, "addr-base")) {
					get_numbers(argv[i+1], &offset_base, &offset_digits);
					if (offset_base <= 0)
						offset_base = 16;
				}
				if (!strcmp(opt, "break")) {
					get_numbers(argv[i+1], &nl_every, &nl_from);
				}
				else if (!strcmp(opt, "replace")) {
					replace_ch = argv[i+1][0];
				}
				else if (!strcmp(opt, "input")) {
					in_name = argv[i+1];
				}
				i++;
			}

			continue;
		}

		char c = argv[i][1];
		if (c == 'h') {
			print_help(argv[0]);
			return 0;
		}
		else if (c == 'x') {
			hexdump = 1;
		}
		else if (c == 's') {
			should_skip_non_ascii = 1;
		}
		else if (i < argc-1) {
			if (c == 'a') {
				get_numbers(argv[i+1], &offset_base, &offset_digits);
				if (offset_base <= 0)
					offset_base = 16;
			}
			else if (c == 'b') {
				get_numbers(argv[i+1], &nl_every, &nl_from);
			}
			else if (c == 'r') {
				replace_ch = argv[i+1][0];
			}
			else if (c == 'i') {
				in_name = argv[i+1];
			}
			i++;
		}
	}

	if (nl_every > 0)
		nl_from = nl_every - (nl_from % nl_every);

	if (in_name)
		input = fopen(in_name, "rb");

	if (!input) {
		fprintf(stderr, "Could not open file \"%s\" for reading\n", in_name);
		return 1;
	}

	char in[BUF_SIZE];
	char out[BUF_SIZE * 16];
	char number[64];

	char *in_ptr = in;
	int n_digits = 0;
	int should_print_off = 1;
	const int out_end = BUF_SIZE * 15;

	u64 offset = 0;

	int sz = fread(in, 1, BUF_SIZE, input);
	while (sz > 0) {
		int len = 0;
		int was_non_ascii = 0;

		int i = 0;
		for (; i < sz && len < out_end; i++) {
			if (should_print_off && offset_base) {
				u64 off = offset + (u64)i;
				if (off > 0) {
					n_digits = 0;
					while (off) {
						char c = '0' + (char)(off % offset_base);
						off /= offset_base;
						if (c > '9') c += 'a' - '9' - 1;
						number[n_digits++] = c;
					}
					for (int j = 0; j < offset_digits - n_digits; j++)
						out[len++] = '0';
					while (--n_digits >= 0)
						out[len++] = number[n_digits];
				}
				else {
					for (int j = 0; j < offset_digits; j++)
						out[len++] = '0';
				}
				out[len++] = ' ';
				should_print_off = 0;
			}

			for (; i < sz && len < out_end; i++) {
				int use_breaks = nl_every > 0 && nl_every >= nl_from;
				int non_ascii = !hexdump && (in_ptr[i] < ' ' || in_ptr[i] > '~');

				if (!non_ascii) {
					if (hexdump) {
						const char *hexlut = "0123456789abcdef";
						out[len++] = hexlut[(in_ptr[i] >> 4) & 0xf];
						out[len++] = hexlut[in_ptr[i] & 0xf];
						out[len++] = ' ';
					}
					else {
						out[len++] = (char)in_ptr[i];
					}
					was_non_ascii = 0;
				}
				else if (!was_non_ascii) {
					out[len++] = replace_ch;
					should_print_off = !use_breaks;
					was_non_ascii = should_skip_non_ascii;
				}

				u64 point = offset + (u64)i + (u64)nl_from;
				if (use_breaks && point > 0 && (point+1) % (u64)nl_every == 0) {
					out[len++] = '\n';
					should_print_off = 1;
					i++;
					break;
				}
				if (non_ascii) {
					i++;
					break;
				}
			}
			i--; // spicy
		}

		fwrite(out, 1, len, stdout);

		if (i < sz) {
			in_ptr = &in[i];
			sz -= i;
		}
		else {
			in_ptr = in;
			sz = fread(in, 1, BUF_SIZE, input);
		}

		offset += (u64)i;
	}

	putchar('\n');

	if (in_name)
		fclose(input);

	return 0;
}
