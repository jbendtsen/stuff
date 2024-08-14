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
		"   -c\n"
		"   --collapse16\n"
		"      Collapse 16-bit strings into 8-bit strings\n"
		"      Only works if each 16-bit character in the intended string\n"
		"       has a code-point of less than 256\n"
		"      Only works with --hex disabled\n"
		"   -t <minimum length>\n"
		"   --threshold <minimum length>\n"
		"      Skip strings with a length below <minimum length> bytes\n"
		"      eg. -t 4\n"
		"         will keep ABCD but not ABC\n"
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
	int min_length = 0;
	int should_skip_non_ascii = 0;
	int nl_every = 0;
	int nl_from = 0;
	int hexdump = 0;
	int collapse_16 = 0;
	char replace_ch = '\n';

	for (int i = 1; i < argc; i++) {
		int len = strlen(argv[i]);
		if (len < 2 || argv[i][0] != '-') {
			in_name = argv[i];
			continue;
		}

		if (len > 2 && argv[i][1] == '-') {
			char *opt = argv[i] + 2;
			if (!strcmp(opt, "collapse16")) {
				collapse_16 = 1;
			}
			else if (!strcmp(opt, "help")) {
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
				else if (!strcmp(opt, "threshold")) {
				    get_numbers(argv[i+1], &min_length, NULL);
				}
				i++;
			}

			continue;
		}

		char c = argv[i][1];
		if (c == 'c') {
			collapse_16 = 1;
		}
		else if (c == 'h') {
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
			else if (c == 't') {
			    get_numbers(argv[i+1], &min_length, NULL);
			}
			i++;
		}
	}

	if (nl_every > 0)
		nl_from = nl_every - (nl_from % nl_every);

    if (hexdump)
        min_length *= 3;

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
		int was_actually_non_ascii = 0;
		//int skip_delta = 0;

		int i = 0;
		for (; i < sz && len < out_end; i++) {
			int old_len = len;
			int addr_len = 0;

			for (; i < sz && len < out_end; i++) {
				int use_breaks = nl_every > 0 && nl_every >= nl_from;
				int non_ascii = !hexdump && (in_ptr[i] < ' ' || in_ptr[i] > '~');

				int was_ascii = !was_actually_non_ascii;
				was_actually_non_ascii = in_ptr[i] < ' ' || in_ptr[i] > '~';

				int is_collapse = collapse_16 && !hexdump && in_ptr[i] == 0 && was_ascii;
				int will_print = !non_ascii || (!was_non_ascii && !is_collapse);

                if (should_print_off && will_print && offset_base) {
                    addr_len = len;
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
					    for (int j = 0; j < offset_digits || j < 1; j++)
						    out[len++] = '0';
				    }
				    out[len++] = ' ';
				    addr_len = len - addr_len;
				    should_print_off = 0;
			    }

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
				else if (!was_non_ascii && !is_collapse) {
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

				if (non_ascii && !is_collapse) {
					i++;
					break;
				}
			}

            if (min_length > 0 && len - addr_len < old_len + min_length + 1) {
                //skip_delta += len - old_len;
                //should_print_off = 0;
                len = old_len;
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

		offset += (u64)i; //(u64)(i - skip_delta);
	}

	putchar('\n');

	if (in_name)
		fclose(input);

	return 0;
}
