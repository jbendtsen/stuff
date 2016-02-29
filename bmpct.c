// bmpct.c
/*
Input: 24-bit uncompressed BMP file
Output: another 24-bit uncompressed BMP file
Description:
	Once the pixel buffer in the BMP is extracted, the program iterates over every pixel,
	  deciding whether a pixel is closer in brightness to black or white.
	If it is closer to white, then the corresponding pixel in the output BMP will be white,
	  and vice versa if it's closer to black.
	Since the BMP file format encodes pixels in the RGB format,
	  the true brightness of a pixel is (in this program) determined
	  by the mean brightness of each of its red, green and blue channels.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char u8;

void contrast(u8 p[3]) {
	int a = p[0] + p[1] + p[2];
	a /= 3;
	u8 c = a;
	if (c > 0x7f) c = 0xff;
	else c = 0;
	p[0] = p[1] = p[2] = c;
}

int le16(u8 *s) {
	int t = s[0];
	t |= s[1] << 8;
	//printf("%02X, %02X\n", s[0], s[1]);
	return t;
}

int main(int argc, char **argv) {
	printf("BMP Contrast v0.1\n\n");
	if (argc < 3) {
		printf("Invalid arguments\n"
			"Usage: %s <input 24-bit bmp> <output 24-bit bmp>\n", argv[0]);
		return 1;
	}

	FILE *f = fopen(argv[1], "rb");
	if (!f) {
		printf("\"%s\" doesn't exist\n", argv[1]);
		return 2;
	}

	fseek(f, 0, SEEK_END);
	int sz = ftell(f);
	fseek(f, 0, SEEK_SET);
	if (sz < 58) {
		printf("\"%s\" is too small to be a BMP\n", argv[1]);
		return 3;
	}

	u8 header[54];
	fread(header, 1, 54, f);
	if (header[0] != 0x42 || header[1] != 0x4d) {
		printf("\"%s\" is not a BMP\n", argv[1]);
		return 4;
	}

	int bits = header[0x1c];
	if (bits != 24) {
		printf("Must be a 24-bit bmp (R8G8B8)\n");
		return 5;
	}

	int off = le16(header+0xa);
	int x = le16(header+0x12);
	int y = le16(header+0x16);
	int xr = (x * 3) + (x % 4);

	if (sz != off + xr * y) {
		printf("Incorrect size\n"
			"Projected size: %d, actual size: %d\n"
			"X: %d, Y: %d, X_Row: %d, Offset: %d\n", off+(xr*y), sz, x, y, xr, off);
		return 6;
	}

	u8 hdr[off];
	u8 buf[xr * y];
	u8 fill[3] = {0};

	rewind(f);
	fread(hdr, 1, off, f);
	fread(buf, 1, xr * y, f);
	fclose(f);

	int i, j, o = 0;
	for (i = 0; i < y; i++) {
		for (j = 0; j < x; j++, o += 3) contrast(buf+o);
		o += xr - x * 3;
	}

	f = fopen(argv[2], "wb");
	fwrite(hdr, 1, off, f);
	fwrite(buf, 1, xr * y, f);
	fclose(f);
	return 0;
}
