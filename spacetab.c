#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <dirent.h>

#define NO_SC     0
#define TO_TABS   1
#define TO_SPACES 2

#define NO_LC   0
#define TO_DOS  1
#define TO_UNIX 2

#define TYPE_FILE  0
#define TYPE_DIR   1
#define TYPE_OTHER 2

char *absolute_path(char *in, int fwd) {
	if (!in || strlen(in) < 1)
		return NULL;

	int len = strlen(in);
	// my definition of an absolute path
	if (in[0] == '/' || in[0] == '\\' ||
		(len >= 2 && in[1] == ':'))
	{
		return strdup(in);
	}

	char cwd[256];
	if (getcwd(cwd, 256) == NULL)
		return NULL;

	int path_len = strlen(cwd) + 1 + strlen(in);
	char *path = malloc(path_len + 1);
	sprintf(path, "%s/%s", cwd, in);

	int i;
	for (i = 0; i < path_len; i++) {
		if (fwd && path[i] == '\\')
			path[i] = '/';
		else if (!fwd && path[i] == '/')
			path[i] = '\\';
	}

	return path;
}

typedef unsigned char u8;

typedef struct {
	char *name;
	char *fullName;
	int type;
	int size;
} Entry;

typedef struct {
	u8 *data;
	int size;
} Buffer;

void appendSection(Buffer *dst, Buffer *src, int start, int end) {
	int len = end - start + 1;
	if (len < 1)
		return;

	dst->data = realloc(dst->data, dst->size + len);
	memcpy(dst->data + dst->size, src->data + start, len);

	dst->size += len;
}

void appendSpan(Buffer *b, char c, int len) {
	b->data = realloc(b->data, b->size + len);
	memset(b->data + b->size, c, len);
	b->size += len;
}

void appendString(Buffer *b, char *str) {
	int len = strlen(str);
	b->data = realloc(b->data, b->size + len);
	memcpy(b->data + b->size, str, len);
	b->size += len;
}

void convertFile(Entry *e, int spt, int spMode, int lnMode) {
	if (!e || e->type != TYPE_FILE || e->size <= 0)
		return;

	FILE *f = fopen(e->fullName, "rb");
	if (!f)
		return;

	Buffer in = {NULL, e->size};
	Buffer out = {NULL, 0};

	in.data = malloc(in.size);
	fread(in.data, 1, in.size, f);
	fclose(f);

	int start = 0;
	int col = 0, line = 0;
	int leadWh = 1, spCon = 0;

	int i;
	for (i = 0; i < in.size; i++) {
		if (in.data[i] != ' ')
			spCon = 0;

		if (leadWh && !(in.data[i] == ' ' || in.data[i] == '\t'))
			leadWh = 0;

		if (spMode == TO_TABS) {
			if (in.data[i] == ' ' && leadWh)
				spCon++;

			if (spCon == spt) {
				appendSection(&out, &in, start, i - spCon);
				appendSpan(&out, '\t', 1);
				start = i + 1;

				spCon = 0;
			}
		}
		else if (spMode == TO_SPACES) {
			if (in.data[i] == '\t') {
				appendSection(&out, &in, start, i - 1);
				appendSpan(&out, ' ', spt);
				start = i + 1;
			}
		}

		if (lnMode == TO_DOS || lnMode == TO_UNIX) {
			int wasCr = i > 0 && in.data[i-1] == '\r';
			if (lnMode == TO_DOS) {
				if (in.data[i] == '\n' && !wasCr) {
					appendSection(&out, &in, start, i - 1);
					appendString(&out, "\r\n");
					start = i + 1;
				}
			}
			else if (lnMode == TO_UNIX) {
				if (in.data[i] == '\n' && wasCr) {
					appendSection(&out, &in, start, i - 2);
					appendString(&out, "\n");
					start = i + 1;
				}
			}
		}

		col++;
		if (in.data[i] == '\n') {
			line++;
			col = 0;
			leadWh = 1;
		}
	}

	appendSection(&out, &in, start, in.size - 1);

	f = fopen(e->fullName, "wb");
	if (f) {
		fwrite(out.data, 1, out.size, f);
		fclose(f);
	}

	free(in.data);
	free(out.data);
}

void iterate(char *directory, int recursive, int spt, int spMode, int lnMode) {
	Entry *list = NULL;
	int n_items = 0;

	DIR *handle = opendir(directory);
	struct dirent *cur;
	struct stat info = {0};

	while ((cur = readdir(handle)) != NULL) {
		if (!strcmp(cur->d_name, ".") || !strcmp(cur->d_name, ".."))
			continue;

		char *full = malloc(strlen(directory) + 1 + strlen(cur->d_name) + 1);
		sprintf(full, "%s/%s", directory, cur->d_name);

		memset(&info, 0, sizeof(struct stat));
		int res = stat(full, &info);
		if (res < 0)
			continue;

		int type = TYPE_OTHER;
		int size = 0;
		if (info.st_mode & S_IFREG) {
			type = TYPE_FILE;
			size = info.st_size;
		}
		if (info.st_mode & S_IFDIR)
			type = TYPE_DIR;

		list = realloc(list, (n_items + 1) * sizeof(Entry));
		list[n_items].name = strdup(cur->d_name);
		list[n_items].fullName = full;
		list[n_items].type = type;
		list[n_items].size = size;
		n_items++;
	}

	int i;
	for (i = 0; i < n_items; i++) {
		if (list[i].type == TYPE_FILE) {
			convertFile(&list[i], spt, spMode, lnMode);
		}
		else if (recursive && list[i].type == TYPE_DIR) {
			int nextLen = strlen(directory) + 1 + strlen(list[i].name);
			char *nextDir = malloc(nextLen + 1);
			sprintf(nextDir, "%s/%s", directory, list[i].name);

			iterate(nextDir, 1, spt, spMode, lnMode);
			free(nextDir);
		}

		free(list[i].name);
		free(list[i].fullName);
	}

	free(list);
}

void printHelp() {
	printf("Space-Tab Converter:\n"
	       "For converting spaces to tabs and vice versa\n"
		   "Can optionally convert line-endings from DOS to Unix and vice versa\n"
	       "Options:\n"
		   "  -h:\n"
		   "    Print this menu\n"
		   "  -R:\n"
		   "    Recursively locates files to convert\n"
		   "  -d <directory>:\n"
		   "    Specifies the directory to start converting files from\n"
		   "  -n <length>:\n"
		   "    Specifies the number of spaces per tab (default: 4)\n"
		   "  -t:\n"
		   "    Converts leading spaces to tabs (you want this ;))\n"
		   "  -s:\n"
		   "    Converts tabs to spaces (believe me, you don't want this :P)\n"
		   "  -w:\n"
		   "    Converts all Unix line-endings to DOS\n"
		   "  -u:\n"
		   "    Converts all DOS line-endings to Unix\n");
}

int main(int argc, char **argv) {
	int recursive = 0;
	char *directory = ".";
	int spacesPerTab = 4;
	int spaceMode = NO_SC;
	int lineMode = NO_LC;

	int i;
	for (i = 1; i < argc; i++) {
		if (argv[i][0] != '-')
			continue;

		switch (argv[i][1]) {
			case 'h':
				printHelp();
				return 0;
			case 'R':
				recursive = 1;
				break;
			case 'd':
				if (i >= argc-1)
					break;

				directory = argv[++i];
				break;
			case 'n':
				if (i >= argc-1)
					break;

				spacesPerTab = atoi(argv[++i]);
				break;
			case 't':
				spaceMode = TO_TABS;
				break;
			case 's':
				spaceMode = TO_SPACES;
				break;
			case 'w':
				lineMode = TO_DOS;
				break;
			case 'u':
				lineMode = TO_UNIX;
				break;
		}
	}

	if (spaceMode == NO_SC && lineMode == NO_LC) {
		printf("No conversion specified\n"
		       "Use -t to convert tabs to spaces, -s for the opposite\n"
			   " OR -w to convert Unix line-endings to DOS, -u for the opposite\n");
		return 0;
	}

	char *path = absolute_path(directory, 1);
	iterate(path, recursive, spacesPerTab, spaceMode, lineMode);

	free(path);
	return 0;
}
