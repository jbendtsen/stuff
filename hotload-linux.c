#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <dirent.h>
#include <dlfcn.h>

/* library:
// compile with:
// gcc -shared library.c -o library-`date +%s`.so
// no header file required
#include <stdio.h>

void example(void) {
	printf("Hello\n");
}
*/

#define LIB_OPEN_FLAGS (RTLD_LAZY | RTLD_LOCAL)

int find_latest_library(char *out_name, int max_out_sz, const char *name_template) {
	int templ_sz = strlen(name_template);

	int found_any = 0;
	uint64_t highest = 0;

	DIR *d = opendir(".");
	struct dirent *ent;
	while ((ent = readdir(d))) {
		int sz = strlen(ent->d_name);
		if (sz > 5 && sz > templ_sz &&
			!memcmp(&ent->d_name[sz-3], ".so", 3) &&
			!memcmp(ent->d_name, name_template, templ_sz)
		) {
			uint64_t n = 0;
			for (int i = templ_sz; i < sz - 3; i++) {
				char c = ent->d_name[i];
				if (c >= '0' && c <= '9')
					n = n * 10ULL + (uint64_t)(c - '0');
			}
			if (n >= highest) {
				highest = n;
				int out_sz = sz < (max_out_sz - 1) ? sz : (max_out_sz - 1);
				memcpy(out_name, ent->d_name, out_sz);
				out_name[out_sz] = 0;
				found_any = 1;
			}
		}
	}

	closedir(d);
	return found_any;
}

void maybe_call_example(void *handle) {
	void (*func)() = dlsym(handle, "example");
	if (func) {
		func();
	}
	else {
		printf("Could not locate example()\n");
	}
}

int main(int argc, char **argv) {
	if (argc < 2) {
		printf("Usage: %s <name of library without extension or timestamp>\n", argv[0]);
		return 1;
	}

	char name_buf[1024];
	if (!find_latest_library(name_buf, 1024, argv[1])) {
		printf("Could not find a library starting with the name %s\n", argv[1]);
		return 1;
	}

	printf("program start: loading \"%s\"\n", name_buf);
	void *handle = dlopen(name_buf, LIB_OPEN_FLAGS);
	if (!handle) {
		printf("Could not open %s\n", name_buf);
		return 1;
	}

	while (1) {
		printf("cur: ");
		maybe_call_example(handle);

		printf("press enter to reload the library\n");
		getchar();

		if (!find_latest_library(name_buf, 1024, argv[1])) {
			printf("Could not find a library starting with the name %s\n", argv[1]);
			break;
		}

		void *old_handle = handle;
		handle = dlopen(name_buf, LIB_OPEN_FLAGS);
		if (!handle) {
			printf("Could not open %s\n", name_buf);
			break;
		}
		printf("loaded %s\n", name_buf);

		printf("new: ");
		maybe_call_example(handle);
		printf("old: ");
		maybe_call_example(old_handle);

		dlclose(old_handle);
	}

	return 0;
}
