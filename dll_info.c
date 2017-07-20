/*
   This tool displays most useful information about most Windows Portable Executables (EXEs, DLLs, etc),
   such as the table of sections, the list of imports and the list of exports.
   It can also be used to search for exports inside the file by name or by function address.
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

typedef struct {
	char name[8];
	int mem_size;
	int mem_offset;
	int file_size;
	int file_offset;
	u8 padding[12];
	u16 type;
	u16 access;
} section_t;

typedef struct {
	char *name;
	u32 *func_addrs;
	char **func_names;
	int len;
} dll_t;

typedef struct {
	char *section;
	int type; // 0 = export, 1 = import
	int mem_offset;
	int file_offset;
	int size;
	dll_t *dlls;
	int n_dlls;
	int len;
} func_table;

typedef struct {
	int list1;
	u8 padding[8];
	int name;
	int list2;
} import_dll_info;

// Extract section table from a pointer to the table inside the file buffer
section_t *get_section_table(u8 *buf, int n_sections) {
	int i = 0, off = 0;
	char name[8] = {0};
	do {
		memcpy(name, buf+off, 7);
		off += sizeof(section_t);
		i++;
	}
	while (i < n_sections && name[0]);

	section_t *sections = malloc(n_sections * sizeof(section_t));
	memcpy(sections, buf, n_sections * sizeof(section_t));

	return sections;
}

// Returns the corresponding file offset of a memory address when the PE would be loaded in memory
int mem_to_file(int mem_addr, section_t *sections, int n_sections, int *section_id) {
	if (mem_addr < 0 || !sections || n_sections < 1) return -1;

	if (mem_addr < sections[0].mem_offset ||
	    mem_addr > sections[n_sections-1].mem_offset + sections[n_sections-1].mem_size)
		 return -2;

	int i, sid = -1;
	for (i = 0; i < n_sections; i++) {
		if (mem_addr > sections[i].mem_offset + sections[i].mem_size) continue;
		sid = i;
		break;
	}

	if (sid < 0) return -3;
	if (section_id) *section_id = sid;

	return mem_addr - sections[sid].mem_offset + sections[sid].file_offset;
}

// Reads a pointer from a file offset, gets the equivalent file offset of that pointer,
// and returns an allocated string copied from that file offset. Quite handy!
char *get_indirect_string(int off, int add, u8 *buf, int sz, section_t *sections, int n_sections) {
	int p = 0;
	memcpy(&p, buf+off, 4);
	p = mem_to_file(p, sections, n_sections, NULL);
	char *str = NULL;
	if (p+add > 0 && p+add < sz-4)
		str = strdup((char*)(buf+p+add));
	else {
		//printf("addr: %#x\n", p+add);
		str = strdup("?");
	}
	return str;
}

// Retrieves the list of exports inside the PE, specifically the name and address of each export
void get_exports(func_table *el, u8 *buf, int sz, section_t *sections, int n_sections) {
	if (!el->dlls || !el->n_dlls || el->dlls[0].func_names || el->dlls[0].func_addrs) return;

	el->dlls[0].func_names = calloc(el->len, sizeof(void*));
	el->dlls[0].func_addrs = calloc(el->len, sizeof(void*));

	int i, off = el->file_offset + 0x28;
	for (i = 0; i < el->len; i++, off += 4) {
		memcpy(&el->dlls[0].func_addrs[i], buf+off, 4);
		el->dlls[0].func_addrs[i] = mem_to_file(el->dlls[0].func_addrs[i], sections, n_sections, NULL);
	}

	for (i = 0; i < el->len; i++, off += 4)
		el->dlls[0].func_names[i] = get_indirect_string(off, 0, buf, sz, sections, n_sections);
}

void close_func_table(func_table *fl) {
	if (!fl->dlls) return;

	int i, j;
	for (i = 0; i < fl->n_dlls; i++) {
		if (fl->dlls[i].name) free(fl->dlls[i].name);
		if (fl->dlls[i].func_addrs) free(fl->dlls[i].func_addrs);

		if (fl->dlls[i].func_names) {
			for (j = 0; j < fl->dlls[i].len; j++) free(fl->dlls[i].func_names[j]);
			free(fl->dlls[i].func_names);
		}
	}

	free(fl->dlls);
	fl->dlls = NULL;
}

void print_func_table(func_table *fl, int mode) {
	if (!fl->dlls) return;

	char name[8] = {0};
	if (fl->type == 0) strcpy(name, "Export");
	else if (fl->type == 1) strcpy(name, "Import");
	else strcpy(name, "??");

	printf("%s Table:\n"
		"\tSection:      %s\n"
		"\tRAM Offset:   %#x\n"
		"\tFile Offset:  %#x\n"
		"\tSize:         %#x\n"
		"\tLength:       %d\n",
		name, fl->section, fl->mem_offset, fl->file_offset, fl->size, fl->len);

	if (fl->type == 1) printf("\tDLL Count:    %d\n", fl->n_dlls);
	printf("\n");

	if (!mode || !fl->n_dlls || !fl->dlls) return;

	printf("%s List:\n\n", name);

	char *func_name = NULL;
	int i, j;
	for (i = 0; i < fl->n_dlls; i++) {
		if (fl->type == 1) printf("%s:\n", fl->dlls[i].name);

		for (j = 0; j < fl->dlls[i].len; j++) {
			if (fl->type == 1) printf("    ");

			if (mode > 1 && fl->dlls[i].func_addrs)
				printf("%#8x - ", fl->dlls[i].func_addrs[j]);

			func_name = fl->dlls[i].func_names[j];
			puts(func_name ? func_name : "(null)");
		}
		printf("\n");
	}
}

int main(int argc, char **argv) {
	if (argc < 2) {
		printf("Invalid arguments\n"
				"Usage: %s <input DLL file> [options]\n"
				"If no option is specified, the list of sections will be printed.\n"
				"Otherwise, the options are:\n"
				" -i\n"
				"   print the import function name list\n"
				" -e\n"
				"   print the export function name list\n"
				" -E\n"
				"   print the export function name + address list\n"
				" -a <name of export function>\n"
				"   find and print the matching address\n"
				" -n <address of export function>\n"
				"   find and print the matching name\n\n", argv[0]);
		return 1;
	}

	// Open the file, read the contents
	FILE *f = fopen(argv[1], "rb");
	if (!f) {
		printf("Could not open \"%s\"\n", argv[1]);
		return 2;
	}
	fseek(f, 0, SEEK_END);
	int sz = ftell(f);
	rewind(f);
	if (sz <= 0x1000) {
		printf("\"%s\" is of an invalid size (%d)\n", argv[1], sz);
		return 3;
	}
	u8 *buf = malloc(sz);
	fread(buf, 1, sz, f);
	fclose(f);

	if (buf[0] != 'M' || buf[1] != 'Z') {
		printf("\"%s\" is not a Windows executable file\n", argv[1]);
		free(buf);
		return 4;
	}

	// Get the section table (list of sections)
	int i = 0, j = 0, off = 0;
	memcpy(&off, buf+0x3c, 4); // Get PE Header Offset

	if (buf[off] != 'P' || buf[off+1] != 'E') {
		printf("\"%s\" is not a Win32 EXE or DLL\n", argv[1]);
		free(buf);
		return 5;
	}

	int n_sections = 0, flags = 0, bit_type = 0;
	func_table el = {0}, il = {0};

	memcpy(&n_sections, buf+off+0x6, 2); // Get the number of sections
	memcpy(&flags, buf+off+0x16, 2); // Get the file type flags
	memcpy(&bit_type, buf+off+0x18, 2); // 0x10b = 32-bit, 0x20b = 64-bit

	if (bit_type == 0x20b) off += 0x10;

	memcpy(&el.mem_offset, buf+off+0x78, 4);
	memcpy(&el.size, buf+off+0x7c, 4);
	memcpy(&il.mem_offset, buf+off+0x80, 4);
	memcpy(&il.size, buf+off+0x84, 4);

	off += 0xf8; // Add 0xf8 to get the Section Header Offset
	section_t *section = get_section_table(buf+off, n_sections);

	int sid = -1;
	el.file_offset = mem_to_file(el.mem_offset, section, n_sections, &sid);
	if (sid >= 0) el.section = (char*)section[sid].name;

	il.file_offset = mem_to_file(il.mem_offset, section, n_sections, &sid);
	if (sid >= 0) il.section = (char*)section[sid].name;

	// Get the number of exports
	if (el.file_offset >= 0 && el.size && el.file_offset < sz-0x18) {
		int n_exports;
		memcpy(&n_exports, buf+el.file_offset+0x14, 4);
		el.dlls = calloc(1, sizeof(dll_t));
		el.n_dlls = 1;
		el.dlls[0].len = el.len = n_exports;
		el.dlls[0].name = get_indirect_string(el.file_offset+0xc, 0, buf, sz, section, n_sections);
	}

	il.type = 1;

	// Get all imports
	if (il.file_offset >= 0 && il.size && il.file_offset < sz-0x18) {
		// Get the number of statically imported DLLs
		import_dll_info dll_info = {0};
		u8 empty[sizeof(import_dll_info)] = {0};

		int off = il.file_offset;
		i = 0;
		do {
			memcpy(&dll_info, buf+off, sizeof(import_dll_info));
			off += sizeof(import_dll_info);
			i++;
		}
		while (memcmp(&dll_info, empty, sizeof(import_dll_info)));

		il.n_dlls = i - 1;
		il.dlls = calloc(il.n_dlls, sizeof(dll_t));

		// Get the list of imports for each DLL
		char ord_str[10];
		off = il.file_offset;
		int fn_tbl_off = 0, name_off = 0;
		for (i = 0; i < il.n_dlls; i++, off += sizeof(import_dll_info)) {
			memcpy(&dll_info, buf+off, sizeof(import_dll_info));
			il.dlls[i].name = get_indirect_string(off+0xc, 0, buf, sz, section, n_sections);

			// Find the list of import name offsets for a particular DLL
			fn_tbl_off = dll_info.list1 ? dll_info.list1 : dll_info.list2;
			fn_tbl_off = mem_to_file(fn_tbl_off, section, n_sections, NULL);
			if (fn_tbl_off < 0) continue;

			// Iterate over the list
			j = 0;
			memcpy(&name_off, buf+fn_tbl_off, 4);
			while (name_off && fn_tbl_off < sz-4) {
				il.dlls[i].func_names = realloc(il.dlls[i].func_names, (j+1) * sizeof(void*));

				// If the current name offset has the highest bit set,
				// it is not an offset to a name but instead a number that indexes an import function
				if (name_off & 0x80000000) {
					memset(ord_str, 0, 10);
					sprintf(ord_str, "0x%x", name_off & 0x7fffffff);
					il.dlls[i].func_names[j++] = strdup(ord_str);
				}
				// Else, the offset should be valid and pointing to the name of the current import function
				else {
					il.dlls[i].func_names[j++] = get_indirect_string(fn_tbl_off, 2, buf, sz, section, n_sections);
				}

				fn_tbl_off += 4;
				memcpy(&name_off, buf+fn_tbl_off, 4);
			}
			il.dlls[i].len = j;
			il.len += il.dlls[i].len;
		}
	}

	printf("\n");

	// If no options were specified, print the section table and leave
	if (argc == 2) {
		printf("Flags: 0x%04x\nNumber of sections: %d\n", flags, n_sections);
		char type[16] = {0};
		for (i = 0; i < n_sections; i++) {
			switch (section[i].type) {
				case 0:
					strcpy(type, "BSS/Other");
					break;
				case 0x20:
					strcpy(type, "Code");
					break;
				case 0x40:
					strcpy(type, "Data");
					break;
				case 0x80:
					strcpy(type, "Memory");
					break;
				default:
					sprintf(type, "Unknown (0x%02x)", section[i].type);
					break;
			}

			printf("\n%s:\n"
					"\tSize in RAM:   %#x\n"
					"\tRAM Offset:    %#x\n"
					"\tSize in File:  %#x\n"
					"\tFile Offset:   %#x\n"
					"\tType:          %s\n"
					"\tAccess:        %c%c%c\n",
					section[i].name, section[i].mem_size, section[i].mem_offset, section[i].file_size, section[i].file_offset,
					type, section[i].access & 0x4000 ? 'R' : ' ', section[i].access & 0x8000 ? 'W' : ' ', section[i].access & 0x2000 ? 'X' : ' ');

			memset(type, 0, 16);
		}
		printf("\n------------------------------------\n\n");

		print_func_table(&el, 0);
		print_func_table(&il, 0);

		close_func_table(&el);
		close_func_table(&il);
		free(section);
		free(buf);
		return 0;
	}

	int a;
	for (a = 2; a < argc; a++) {
		if (!memcmp(argv[a], "-i", 2)) {
			//get_imports(&il, buf, sz, section, n_sections);
			print_func_table(&il, 1);
		}
		else if (!memcmp(argv[a], "-e", 2)) {
			get_exports(&el, buf, sz, section, n_sections);
			print_func_table(&el, 1);
		}
		else if (!memcmp(argv[a], "-E", 2)) {
			get_exports(&el, buf, sz, section, n_sections);
			print_func_table(&el, 2);
		}
		else if (!memcmp(argv[a], "-a", 2)) {
			if (++a >= argc) break;

			get_exports(&el, buf, sz, section, n_sections);
			if (!el.dlls[0].func_names) continue;
			for (i = 0; i < el.dlls[0].len; i++)
				if (strlen(el.dlls[0].func_names[i]) == strlen(argv[a]) && !strcmp(el.dlls[0].func_names[i], argv[a])) break;

			if (i == el.dlls[0].len) printf("Could not find %s in the export list\n\n", argv[a]);
			else if (el.dlls[0].func_addrs) printf("%s: %#x\n\n", argv[a], el.dlls[0].func_addrs[i]);
		}
		else if (!memcmp(argv[a], "-n", 2)) {
			if (a+1 >= argc) break;
			u32 addr = strtoul(argv[++a], NULL, 16);

			get_exports(&el, buf, sz, section, n_sections);
			if (!el.dlls[0].func_addrs) continue;
			for (i = 0; i < el.dlls[0].len; i++)
				if (el.dlls[0].func_addrs[i] == addr) break;

			if (i == el.dlls[0].len) printf("Could not find %#x in the export list\n\n", addr);
			else if (el.dlls[0].func_names) printf("%#x: %s\n\n", addr, el.dlls[0].func_names[i]);
		}
	}

	close_func_table(&el);
	close_func_table(&il);

	free(section);
	free(buf);
	return 0;
}