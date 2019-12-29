// Converts GEOMETRY.BIN files from Need For Speed Underground into Wavefront OBJ

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define DWORD_ALIGN(n) ((n) + 3) & ~3

typedef unsigned char u8;
typedef unsigned int u32;

typedef struct {
	u8 *data;
	int size;
} Buffer;

typedef struct {
	u32 unk1[9];
	u32 n_faces;
	u32 unk2[3];
	u32 n_points;
	u32 unk3[3];
} Header;

typedef struct {
	float x, y, z;
	float unk[4];
	float u, v;
} Point;

typedef struct {
	short a, b, c;
} Mapping;

int write_obj(FILE *f, int vert_off, Buffer *header, Buffer *verts, Buffer *map) {
	if (!f || !header || !verts || !map || !header->data || !verts->data || !map->data)
		return -1;

	if (header->size < sizeof(Header))
		return -2;

	Header *hdr = (Header*)(header->data + header->size - sizeof(Header));

	int point_block = DWORD_ALIGN(hdr->n_points * sizeof(Point));
	if (verts->size < point_block)
		return -3;

	int map_block = DWORD_ALIGN(hdr->n_faces * sizeof(Mapping));
	if (map->size < map_block)
		return -4;

	Point *points = (Point*)(verts->data + verts->size - point_block);
	Mapping *faces = (Mapping*)(map->data + map->size - map_block);

	fprintf(f, "o Mesh.%d\n", vert_off);

	for (int i = 0; i < hdr->n_points; i++)
		fprintf(f, "v %g %g %g\n", points[i].x, points[i].y, points[i].z);

	for (int i = 0; i < hdr->n_points; i++)
		fprintf(f, "vt %g %g\n", points[i].u, points[i].v);

	fprintf(f, "s off\n");

	for (int i = 0; i < hdr->n_faces; i++) {
		int a = faces[i].a + vert_off, b = faces[i].b + vert_off, c = faces[i].c + vert_off;
		fprintf(f, "f %d/%d %d/%d %d/%d\n", a, a, b, b, c, c);
	}

	return hdr->n_points;
}

inline void set_buffer(Buffer *b, u32 *file) {
	b->data = (u8*)&file[2];
	b->size = file[1];
}

int main(int argc, char **argv) {
	if (argc != 3) {
		printf("Invalid arguments\n"
			"Usage: %s <input geometry bin> <output obj>\n", argv[0]);
		return 1;
	}

	FILE *f = fopen(argv[1], "rb");
	if (!f) {
		printf("Could not open %s\n", argv[1]);
		return 2;
	}

	fseek(f, 0, SEEK_END);
	int sz = ftell(f);
	rewind(f);
	if (sz < 1) {
		printf("%s is empty\n", argv[1]);
		return 3;
	}

	u8 *buf = malloc(sz);
	fread(buf, 1, sz, f);
	fclose(f);

	f = fopen(argv[2], "w");

	u32 off = 0;
	u32 parent_off = 0, parent_type = 0;
	Buffer header = {0};
	Buffer points = {0};
	Buffer faces = {0};
	int vert_off = 1;

	while (off < sz) {
		off += 8;
		u32 *p = (u32*)(buf + off - 8);

		if (*p & 0x80000000) {
			parent_off = off - 8;
			parent_type = *p;
			header.data = points.data = faces.data = NULL;
			continue;
		}
		else
			off += p[1];

		if (parent_type == 0x80134100) {
			switch (*p) {
				case 0x00134900:
					set_buffer(&header, p);
					break;
				case 0x00134B01:
					set_buffer(&points, p);
					break;
				case 0x00134B03:
					set_buffer(&faces, p);
					break;
			}

			if (header.data && points.data && faces.data) {
				int res = write_obj(f, vert_off, &header, &points, &faces);
				if (res < 0)
					printf("write_obj at %s.%#x failed (%d)\n", argv[1], parent_off, res);
				else
					vert_off += res;

				header.data = points.data = faces.data = NULL;
			}
		}
	}

	fclose(f);
	free(buf);

	return 0;
}
