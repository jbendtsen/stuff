#include <stdio.h>
#include <string.h>

#define BUF_SIZE 1024

typedef unsigned long long u64;

void print_help(const char *prog) {
	fprintf(stderr,
		"Convert binary to text, for easier post-processing with eg. grep\n"
		"Usage: %s [options]\n"
		"Options:\n"
		"   -d\n"
		"   --decimal\n"
		"      Print offsets in decimal\n"
		"   -x\n"
		"   --hex\n"
		"      Print offsets in hexadecimal\n"
		"   -s\n"
		"   --sparse\n"
		"      Skip non-ascii sections of the input\n"
		"   -i <file>\n"
		"   --input <file>\n"
		"      Specify input file.\n"
		"      Defaults to stdin.\n"
		"   -h\n"
		"   --help\n"
		"      Print this help menu\n"
	, prog);
}

int main(int argc, char **argv)
{
	FILE *input = stdin;
	char *in_name = NULL;
	int offset_base = 0;
	int should_skip_non_ascii = 0;

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
			else if (!strcmp(opt, "decimal")) {
				offset_base = 10;
			}
			else if (!strcmp(opt, "hex")) {
				offset_base = 16;
			}
			else if (!strcmp(opt, "sparse")) {
				should_skip_non_ascii = 1;
			}
			else if (i < argc-1) {
				if (!strcmp(opt, "input")) {
					in_name = argv[i+1];
				}
			}

			continue;
		}

		char c = argv[i][1];
		if (c == 'h') {
			print_help(argv[0]);
			return 0;
		}
		else if (c == 'd') {
			offset_base = 10;
		}
		else if (c == 'x') {
			offset_base = 16;
		}
		else if (c == 's') {
			should_skip_non_ascii = 1;
		}
		else if (i < argc-1) {
			if (c == 'i') {
				in_name = argv[i+1];
			}
		}
	}

	if (in_name)
		input = fopen(in_name, "rb");

	if (!input) {
		fprintf(stderr, "Could not open file \"%s\" for reading\n", in_name);
		return 1;
	}

	char in[BUF_SIZE];
	char out[BUF_SIZE * 16];
	char number[64];

	int sz = BUF_SIZE;
	int n_digits = 0;
	int should_print_off = 1;
	const int out_end = BUF_SIZE * 15;

	u64 offset = 0;

	while (sz == BUF_SIZE) {
		sz = fread(in, 1, 1024, input);
		if (sz <= 0)
			break;

		int len = 0;
		int was_non_ascii = 0;

		for (int i = 0; i < sz && len < out_end; i++) {
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
					while (--n_digits >= 0)
						out[len++] = number[n_digits];
				}
				else {
					out[len++] = '0';
				}
				out[len++] = ' ';
			}

			for (; i < sz && len < out_end; i++) {
				if (in[i] < ' ' || in[i] > '~') {
					if (was_non_ascii)
						continue;

					out[len++] = '\n';
					should_print_off = 1;
					was_non_ascii = should_skip_non_ascii;
					break;
				}
				out[len++] = (char)in[i];
				was_non_ascii = 0;
			}
		}

		fwrite(out, 1, len, stdout);

		offset += (u64)sz;
	}

	putchar('\n');

	if (in_name)
		fclose(input);

	return 0;
}
