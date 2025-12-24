/*
		zextract.c: A ROM extractor for the N64 Zelda titles
	This tool lets you extract the contents of Ocarina of Time or
	   Majora's Mask as the individual files that make it up.
*/
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
	#include <direct.h>
	#define mkdir(dir, mode) _mkdir(dir)
#endif

typedef unsigned char u8;
typedef unsigned int u32;

/*
	This struct is used for each entry in the filesystem table in the N64 Zelda games.
	Each entry contains information about where its file is placed inside the ROM, whether it
	is compressed, and how much space it takes up (in compressed and uncompressed forms).

	virt_start and virt_end are the virtual start and end points for the uncompressed version
	of a file. To determine the size of a file in this form, simply subtract virt_start from
	virt_end. real_start and real_end are the start and end points for the file as it is stored
	inside the ROM, be it in compressed or uncompressed format. real_end is set to 0 if the
	file is naturally uncompressed, otherwise it is the actual end point of the file in the ROM.
*/
typedef struct {
	int virt_start;
	int virt_end;
	int real_start;
	int real_end;
} fs_entry;

// This function retrieves a 32-bit number in big-endian (be32) format, the N64's default endianness.
u32 get_be32(u8 *p) {
	return (p[0] << 24) | (p[1] << 16) | (p[2] << 8) | p[3];
}

// This function returns the position of an ASCII string in a binary file.
int find_str(u8 *buf, int sz, char *str) {
	if (!buf || sz < 1 || !str) return -1;

	int i, j, len = strlen(str);
	for (i = 0; i < sz; i++) {
		for (j = 0; j < len && i+j < sz; j++) {
			if (buf[i+j] != ((u8*)str)[j]) break;
		}
		if (j == len) return i;
	}

	return -1;
}

// This function swaps every second byte with the previous byte across an entire buffer.
// N64 ROMs are commonly encoded as "word-swapped", so this function's purpose is to "swap the words back".
void word_swap(u8 *buf, int sz) {
	if (!buf || sz < 1) return;

	u8 x;
	int i;
	for (i = 0; i < sz; i += 2) {
		x = buf[i];
		buf[i] = buf[i+1];
		buf[i+1] = x;
	}
}

// This function takes the filename, removes the file extension
// and adds "_data" to the end, keeping the result as the folder name.
char *folder_name(char *fn) {
	char *folder = strdup(fn);
	int i, pos;
	for (i = strlen(folder)-1; i >= 0 && folder[i] != '.'; i--);
	pos = i;
	fflush(stdout);
	if (pos >= 0) {
		for (i = strlen(folder)-1; i >= pos; i--) folder[i] = 0;
	}
	folder = realloc(folder, strlen(folder) + 6);
	fflush(stdout);
	strcpy(folder+strlen(folder), "_data");
	return folder;
}

// N64 ROM header title names for OoT and MM, in regular and word-swapped forms.
char *titles[] = {
	"THE LEGEND OF ZELDA ",
	"HT EELEGDNO  FEZDL A",
	"ZELDA MAJORA'S MASK ",
	"EZDL AAMOJARS'M SA K"
};

int main(int argc, char **argv) {
	if (argc != 2) {
		printf("Invalid arguments\n"
				"Usage: %s <N64 Zelda ROM>\n", argv[0]);
		return 1;
	}

	// Read the ROM file
	FILE *f = fopen(argv[1], "rb");
	if (!f) {
		printf("Could not open \"%s\"\n", argv[1]);
		return 2;
	}
	fseek(f, 0, SEEK_END);
	int rom_sz = ftell(f);
	rewind(f);
	if (rom_sz < 1) {
		printf("\"%s\" is empty\n", argv[1]);
		return 2;
	}

	u8 *rom = malloc(rom_sz);
	fread(rom, 1, rom_sz, f);
	fclose(f);

	// Determine which game it is, and word-swap the ROM if necessary
	int i, game = -1, swapped = -1;
	for (i = 0; i < 4; i++) {
		if (find_str(rom, rom_sz, titles[i]) == 0x20) {
			game = i / 2;
			swapped = i % 2;
			if (swapped) word_swap(rom, rom_sz);
			break;
		}
	}

	if (game < 0) {
		printf("\"%s\" is not a valid N64 Zelda rom\n", argv[1]);
		free(rom);
		return 3;
	}

	printf("%s detected\n", game == 0 ? "Ocarina of Time" : "Majora's Mask");

	// Find the filesystem table, which reliably comes 48 (0x30) bytes after the string "zelda@"
	int pos = find_str(rom, rom_sz, "zelda@");
	if (pos < 0) {
		printf("Could not find filesystem table\n");
		free(rom);
		return 4;
	}

	pos += 0x30;
	int fst = get_be32(rom+pos+0x20);
	if (fst != pos) {
		printf("Invalid filesystem table\n");
		free(rom);
		return 5;
	}
	int fst_sz = get_be32(rom+pos+0x24) - fst;

	// Determine the number of digits of the index of the last file entry
	int n_digits = 0, s = fst_sz / 16;
	while (s) {
		s /= 10;
		n_digits++;
	}

	// Create the folder
	char *folder = folder_name(argv[1]);
	mkdir(folder, 777);

	// Create the template name for each file, aka
	// folder + "/" + n_digits of zeroes + ".bin" (eg. OoT_data/0000.bin)
	int sf = strlen(folder);
	char *file = malloc(sf + 1 + n_digits + 5);
	memcpy(file, folder, sf);
	file[sf] = '/';
	memset(file+sf+1, '0', n_digits);
	strcpy(file+sf+1+n_digits, ".bin");

	printf("Extracting contents...\n");

	// Find the contents of each file inside the ROM,
	// and save each one to its own file inside the newly created folder
	int fs_off = fst, size = 0;
	fs_entry fe = {0};

	// pointer to the last digit in the filename, before ".bin"
	char *last_digit = file + sf + 1 + n_digits - 1, *p = last_digit;

	for (i = 0; i < fst_sz / 16; i++, fs_off += 16) {
		// Get the information for an entry
		fe.virt_start = get_be32(rom+fs_off);
		fe.virt_end = get_be32(rom+fs_off + 4);
		fe.real_start = get_be32(rom+fs_off + 8);
		fe.real_end = get_be32(rom+fs_off + 12);

		// If the entry is invalid, skip to the next one
		if (fe.real_start < 0 || fe.real_end < 0 ||
		    fe.real_start >= rom_sz || fe.real_end > rom_sz) continue;

		// If real_end > 0, the file is compressed, so use real_end - real_start as the file's size.
		// Otherwise, use virt_end - virt_start as the file's size.
		size = fe.real_end > 0 ? fe.real_end - fe.real_start : fe.virt_end - fe.virt_start;

		// Convert the current index number to a string padded with zeroes at the front.
		s = i;
		while (s > 0) {
			*p = '0' + (s % 10);
			p--;
			s /= 10;
		}
		p = last_digit;

		// real_start contains the offset of the file inside the ROM,
		// so use that as the starting point of the buffer that will be wrote to a file.
		f = fopen(file, "wb");
		fwrite(rom+fe.real_start, 1, size, f);
		fclose(f);
	}

	printf("Done!\n");

	free(file);
	free(folder);
	free(rom);

	return 0;
}