/*
   hexed.c - A command-line driven file editor
   This tool can do most common tasks that regular hex editors can, such as:
   inserting, replacing, removing and copying data, searching and diffing files,
   and viewing parts of files with hex+ASCII.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char u8;

// this struct is used for three unique purposes:
// files, arrays, and "filler" (empty) content
typedef struct {
	char *name;
	u8 *data;
	int size;
} buffer;

void strip(char *str) {
	if (!str) return;
	int i;
	for (i = 0; str[i] >= ' ' && str[i] <= '~'; i++);
	str[i] = 0;
}

int is_empty(buffer *buf) {
	if (!buf) return 1;
	if (!buf->data || buf->size < 1) return 2;
	return 0;
}

void close_buffer(buffer *buf) {
	if (!buf) return;
	if (buf->name) free(buf->name);
	if (buf->data) free(buf->data);
	memset(buf, 0, sizeof(buffer));
}

int load_file(buffer *buf, char *name) {
	if (!buf || !name) return -1;
	if (buf->data) free(buf->data);
	memset(buf, 0, sizeof(buffer));

	FILE *f = fopen(name, "rb");
	if (!f) return -2;
	buf->name = strdup(name);

	fseek(f, 0, SEEK_END);
	int sz = ftell(f);
	if (sz < 1) return 0; // All good but nothing else to do
	rewind(f);

	buf->size = sz;
	buf->data = malloc(buf->size);
	fread(buf->data, 1, buf->size, f);
	fclose(f);
	return 0;
}

void save_buffer(buffer *buf) {
	if (!buf || !buf->name) return;

	FILE *f = fopen(buf->name, "wb");
	if (!f || !buf->data || buf->size < 1) return;

	fwrite(buf->data, 1, buf->size, f);
	fclose(f);
}

void create_filler(buffer *buf, u8 fill, int size) {
	if (!buf || size < 1) return;

	if (buf->data) free(buf->data);
	memset(buf, 0, sizeof(buffer));

	buf->data = calloc(size, 1);
	buf->size = size;
	memset(buf->data, fill, buf->size);
}

void swap_buffer(buffer *buf, int off, int len, int swap) {
	if (is_empty(buf) || swap <= 1 || off > buf->size - swap) return;

	if (off < 0) off = 0;
	if (len < 1) len = buf->size;
	if (off+len > buf->size) len = buf->size - off;

	len -= len % swap;
	if (len <= 0) return;

	u8 *temp = malloc(swap);
	int i, j;
	for (i = off; i < off+len; i += swap) {
		memcpy(temp, buf->data + i, swap);
		for (j = 0; j < swap; j++) buf->data[i+j] = temp[swap-j-1];
	}
	free(temp);
}

void add_byte(buffer *buf, u8 x) {
	buf->data = realloc(buf->data, ++buf->size);
	buf->data[buf->size - 1] = x;
}

void read_array(buffer *array, char **str, int n_strs) {
	if (!array || !str || n_strs < 1) return;
	if (array->name) free(array->name);
	if (array->data) free(array->data);
	memset(array, 0, sizeof(buffer));

	int i, j, len = 0, mode = 0;
	for (i = 0; i < n_strs; i++) {
		if (!str[i]) continue;

		int dg_left = 2, digit = 0, byte = 0;
		for (j = 0; str[i][j]; j++) {
			char c = str[i][j];
			if (c < ' ' || c > '~') continue;
			if (c == '"') {
				mode = !mode;
				continue;
			}

			if (!mode) {
				int sub = 0;
				if (c >= '0' && c <= '9') sub = '0';
				if (c >= 'A' && c <= 'F') sub = 'A' - 0xa;
				if (c >= 'a' && c <= 'f') sub = 'a' - 0xa;
				if (!sub) continue;

				digit = c - sub;
				dg_left--;
			}
			else {
				digit = c;
				dg_left = 0;
			}
			byte <<= 4;
			byte |= digit;

			if (dg_left <= 0) {
				add_byte(array, byte);
				byte = 0;
				dg_left = 2;
			}
		}
		if (mode && i < n_strs-1) add_byte(array, ' ');
		if (!mode && dg_left == 1) add_byte(array, byte);
	}
}

void apply_buffer(buffer *dst, buffer *src, int dst_pos, int src_pos, int len, int insert) {
	if (!dst || !src || (!dst->data && !src->data) ||
		(dst->size < 1 && src->size < 1) || src_pos >= src->size) return;

	if (src_pos < 0) src_pos = 0;
	if (len < 1) len = src->size - src_pos;
	if (dst_pos < -len) dst_pos = -len;
	if (dst_pos > dst->size) dst_pos = dst->size;

	if (insert) {
		if (dst_pos < 0) dst_pos = 0;
		dst->data = realloc(dst->data, dst->size + len);
		if (dst_pos < dst->size) {
			memmove(dst->data + dst_pos + len, dst->data + dst_pos, dst->size - dst_pos);
		}
		dst->size += len;
	}
	else {
		if (dst_pos < 0) {
			src_pos = -dst_pos;
			if (src_pos > src->size) return;
			dst_pos = 0;
		}
		if (dst_pos + len > dst->size) {
			len = dst->size - dst_pos;
			if (len < 1) return;
		}
	}

	memcpy(dst->data + dst_pos, src->data + src_pos, len);
}

void remove_buffer(buffer *buf, int off, int len) {
	if (is_empty(buf) || off >= buf->size || off+len <= 0) return;

	if (len <= 0) len = buf->size;
	if (off < 0) {
		len += off;
		off = 0;
	}
	if (off+len >= buf->size) {
		buf->size = off;
		buf->data = realloc(buf->data, buf->size);
		return;
	}

	memmove(buf->data + off, buf->data + off+len, buf->size - (off+len));
	buf->size -= len;
	buf->data = realloc(buf->data, buf->size);
}

void search_buffer(buffer *results, buffer *buf, buffer *term) {
	if (!results || is_empty(buf) ||
		is_empty(term) || term->size > buf->size) return;

	close_buffer(results);

	int i, j, *res = NULL, count = 0;
	for (i = 0; i < buf->size - term->size; i++) {
		for (j = 0; j < term->size; j++) {
			if (buf->data[i+j] != term->data[j]) break;
		}
		if (j == term->size) {
			res = realloc(res, ++count * sizeof(int));
			res[count-1] = i;
			printf("%#x\n", i);
		}
	}
	printf("\nTotal results: %d\n\n", count);

	results->data = (u8*)res;
	results->size = count * sizeof(int);
}

void diff_buffer(buffer *results, buffer *a, buffer *b) {
	if (!results || is_empty(a) || is_empty(b)) return;

	close_buffer(results);

	int sz = a->size < b->size ? a->size : b->size;
	int i, *diff = NULL, count = 0;
	for (i = 0; i < sz; i++) {
		if (a->data[i] == b->data[i]) continue;

		diff = realloc(diff, ++count * sizeof(int));
		diff[count-1] = i;
		printf("%#x\n", i);
	}
	printf("\nTotal results: %d\n", count);

	results->data = (u8*)diff;
	results->size = count * sizeof(int);
}

#ifndef _WIN32_
	#define PRINT_HL printf("\x1b[7;40m")
	#define PRINT_HL_END printf("\x1b[0m")
#else
	#define PRINT_HL
	#define PRINT_HL_END
#endif

void view_buffer(buffer *b, int off, int len, buffer *offsets) {
	if (is_empty(b) || off >= b->size) return;

	if (len <= 0) len = b->size;
	if (off+len <= 0) return;

	if (off < 0) {
		len += off;
		off = 0;
	}
	if (off+len > b->size)
		len = b->size - off;

	int n_digits = 0, x = (off+len-1) & ~0xf;
	if (x) {
		while (x) {
			n_digits++;
			x >>= 4;
		}
	}
	else n_digits = 1;

	char *blank = calloc(50, 1);
	memset(blank, ' ', 48);

	int hl_idx = 0, old_idx = 0;
	int i = 0, idx, mode = 0, hl = 0, row_end;
	int *offs = offsets ? (int*)offsets->data : NULL;
	u8 *p = NULL;
	while (i < len) {
		idx = off + i;
		p = b->data + idx;
		row_end = i >= ((len-1) & ~0xf) ?
			((len-1) & 0xf) : 0xf;

		if (offsets && hl_idx < offsets->size / sizeof(int) &&
		  idx == offs[hl_idx]) {
			PRINT_HL;
			hl = 1;
			hl_idx++;
		}
		if (!mode) {
			if (i % 16 == 0) {
				printf(" %0*x | ", n_digits, idx);
				old_idx = hl_idx;
			}
			printf("%02x ", *p);
			if (i % 16 == row_end) {
				printf("%s| ", blank + 48 - (15 - row_end) * 3);
				hl_idx = old_idx;
				mode = 1;
				i -= row_end + 1;
			}
		}
		else {
			putchar((*p >= ' ' && *p <= '~') ? *p : '.');
			if (i % 16 == row_end) {
				printf("%s |\n", blank + 48 - (15 - row_end));
				mode = 0;
			}
		}
		if (hl) {
			PRINT_HL_END;
			hl = 0;
		}
		i++;
	}
	printf(" %0*x\n", n_digits, off+len);

	free(blank);
}

char *options = "nNwpifFcCrsSdv";
// argument type requirements (used in reverse order)
// one digit per argument (multiple digits per command)
// 0 = end, 1 = string, 2 = input file, 3 = number, 4 = byte array
// +8 if optional
int arg_reqs[] = {
	0xc1, 0xbb1, 0xbb32, 0x432, 0x432, // -n, -N, -w, -p, -i
	0xb332, 0xb332, 0xbb232, 0xbb232, // -f, -F, -c, -C
	0xb32, 0x42, 0x42, 0x922, 0xabb2 // -r, -s, -S, -d, -v
};

void close_args(void ***args_ref, int n_args, int mode) {
	if (!args_ref || !*args_ref || n_args <= 0 ||
		mode < 0 || mode >= strlen(options)) return;

	void **args = *args_ref;
	int i, req = arg_reqs[mode];
	for (i = 0; i < n_args; i++) {
		int t = req & 7;
		req >>= 4;
		if ((t == 2 || t == 4) && args[i]) {
			close_buffer(args[i]);
			free(args[i]);
			args[i] = NULL;
		}
	}

	free(args);
	*args_ref = NULL;
}

char *usage_str = "Usage:\n"
	"hexed <option> <file> [args...]\n"
	"Options:\n"
	"  -n: Create file with data\n"
	"     [byte data...]\n"
	"  -N: Create file of size\n"
	"     [new size] [fill byte]\n"
	"  -w: Swap bytes in file\n"
	"     <bytes per swap> [offset] [size]\n"
	"  -p: Replace/patch bytes at offset\n"
	"     <offset> <byte data...>\n"
	"  -i: Insert bytes at offset\n"
	"     <offset> <byte data...>\n"
	"  -f: Fill (pave over) bytes\n"
	"     <offset> <size> [fill byte]\n"
	"  -F: Fill (insert) bytes\n"
	"     <offset> <size> [fill byte]\n"
	"  -c: Copy (pave over) data from another file\n"
	"     <offset> <src file> [src offset] [size]\n"
	"  -C: Copy (insert) data from another file\n"
	"     <offset> <src file> [src offset] [size]\n"
	"  -r: Remove data from file\n"
	"     <offset> [size]\n"
	"  -s: Search for byte array (optional output)\n"
	"     <byte data...>\n"
	"  -S: Search for byte array (optional replace)\n"
	"     <byte data...>\n"
	"  -d: Find differences in two files\n"
	"     <2nd input file> [output file of offsets]\n"
	"  -v: Print/view data\n"
	"     [offset] [size] [input file of offsets]\n";

int main(int argc, char **argv) {
	if (argc < 3) {
		printf("Invalid arguments\n");
		puts(usage_str);
		return 1;
	}

	int i, j, mode = -1;
	for (i = 0; options[i]; i++) {
		if (argv[1][1] == options[i]) mode = i;
	}
	if (mode < 0) {
		printf("Invalid option %s\n", argv[1]);
		return 2;
	}

	// This block of code will read command-line arguments and interpret them according to the specified option.
	// Once processed, the arguments are placed in a separate list to be used by the selected mode of operation.
	int a = 0, reqs = arg_reqs[mode], n_args = 0;
	void **args = NULL; // list of args (not always pointers)
	for (i = 0; i < 8; i++) {
		a = reqs & 0xf;
		reqs >>= 4;
		if (!a) break;

		if (i >= argc-2) {
			if (a & 8) break;
			else {
				printf("Not enough arguments to power %s\n", argv[1]);
				close_args(&args, n_args, mode);
				return 3;
			}
		}

		a &= 7;
		if (a > 4) continue;

		args = realloc(args, (n_args+1) * sizeof(void*));
		memset(args+n_args, 0, sizeof(void*));

		if (a == 1) args[n_args] = argv[i+2];

		else if (a == 2) {
			args[n_args] = calloc(1, sizeof(buffer));
			if (load_file(args[n_args], argv[i+2]) < 0) {
				printf("Could not open \"%s\"\n", argv[2]);
				close_args(&args, n_args + 1, mode);
				return 2;
			}
		}
		else if (a == 3) {
			int n = memcmp(argv[i+2], "0x", 2) ?
				atoi(argv[i+2]) : strtol(argv[i+2], NULL, 16);
			// Fancy way of saying args[n_args] = n, but should be platform independent
			args[n_args] = NULL;
			memcpy(&args[n_args], &n, sizeof(int));
		}
		else if (a == 4) {
			args[n_args] = calloc(1, sizeof(buffer));
			read_array(args[n_args], &argv[i+2], argc-(i+2));
		}

		n_args++;
	}

	char msg[1024] = {0}, *str = NULL, c;
	buffer *buf = NULL, temp = {0}, replace = {0};
	char *ptr = NULL;
	u8 byte = 0;
	int off = 0, len = 0, sz, *res = NULL;

	switch (mode) {
	case 0: // Create new file with byte array
		if (n_args > 1) buf = args[1];
		else buf = &temp;
		buf->name = strdup(args[0]);
		save_buffer(buf);
		break;

	case 1: // Create file with fill
		if (n_args > 2) byte = *((u8*)&args[2]);
		if (n_args > 1) create_filler(&temp, byte, *((int*)&args[1]));
		temp.name = strdup(args[0]);
		save_buffer(&temp);
		break;

	case 2: // Swap bytes of data
		if (n_args > 3) len = *((int*)&args[3]);
		if (n_args > 2) off = *((int*)&args[2]);
		swap_buffer(args[0], off, len, *((int*)&args[1]));
		save_buffer(args[0]);
		break;
		
	case 3: // Replace data
		apply_buffer(args[0], args[2], *((int*)&args[1]), 0, 0, 0);
		save_buffer(args[0]);
		break;

	case 4: // Insert data
		apply_buffer(args[0], args[2], *((int*)&args[1]), 0, 0, 1);
		save_buffer(args[0]);
		break;

	case 5: // Fill (pave over) data
		if (n_args > 3) byte = *((u8*)&args[3]);
		create_filler(&temp, byte, *((int*)&args[2]));
		apply_buffer(args[0], &temp, *((int*)&args[1]), 0, 0, 0);
		save_buffer(args[0]);
		break;

	case 6: // Fill (insert into) data
		if (n_args > 3) byte = *((u8*)&args[3]);
		create_filler(&temp, byte, *((int*)&args[2]));
		apply_buffer(args[0], &temp, *((int*)&args[1]), 0, 0, 1);
		save_buffer(args[0]);
		break;

	case 7: // Copy (pave over) data
		if (n_args > 4) len = *((int*)&args[4]);
		if (n_args > 3) off = *((int*)&args[3]);
		apply_buffer(args[0], args[2], *((int*)&args[1]), off, len, 0);
		save_buffer(args[0]);
		break;

	case 8: // Copy (insert into) data
		if (n_args > 4) len = *((int*)&args[4]);
		if (n_args > 3) off = *((int*)&args[3]);
		apply_buffer(args[0], args[2], *((int*)&args[1]), off, len, 1);
		save_buffer(args[0]);
		break;

	case 9: // Remove data from file
		if (n_args > 2) len = *((int*)&args[2]);
		remove_buffer(args[0], *((int*)&args[1]), len);
		save_buffer(args[0]);
		break;

	case 10: // Search data for array (optional file output)
		search_buffer(&temp, args[0], args[1]);
		if (!temp.size) break;
		printf("File to save results (optional)\n> ");
		fgets(msg, 1024, stdin);
		strip(msg);
		if (strlen(msg)) {
			temp.name = strdup(msg);
			save_buffer(&temp);
		}
		break;

	case 11: // Search data for array (optional replace)
		search_buffer(&temp, args[0], args[1]);
		if (!temp.size) break;
		printf("Replace results with array (optional)\n> ");
		fgets(msg, 1024, stdin);
		strip(msg);
		if (!strlen(msg)) break;

		sz = ((buffer*)args[1])->size;
		ptr = &msg[0];
		read_array(&replace, &ptr, 1);
		if (replace.size != sz) {
			str = replace.size < sz ? "smaller" : "larger";
			printf("Note: replacement array is %s than search array.\n"
				"This will result in a %s file. Continue? [y/n]: ", str, str);
			c = getchar();
			if (c != 'Y' && c != 'y') break;
		}

		len = replace.size - sz;
		for (i = 0, ptr = temp.data; ptr < (char*)(temp.data + temp.size); i++, ptr += sizeof(int)) {
			off = *((int*)ptr) + (i * len);
			remove_buffer(args[0], off, sz);
			apply_buffer(args[0], &replace, off, 0, 0, 1);
		}
		save_buffer(args[0]);
		break;

	case 12: // Find differences in two files
		diff_buffer(&temp, args[0], args[1]);
		if (!temp.size) break;

		if (n_args > 2) {
			temp.name = strdup(args[2]);
			save_buffer(&temp);
		}
		break;

	case 13: // View part of or the whole file
		if (n_args > 1) off = *((int*)&args[1]);
		if (n_args > 2) len = *((int*)&args[2]);
		if (n_args > 3) buf = args[3];
		view_buffer(args[0], off, len, buf);
		break;
	}

	close_buffer(&temp);
	close_buffer(&replace);
	close_args(&args, n_args, mode);
	return 0;
}