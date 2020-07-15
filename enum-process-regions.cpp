// Prints the list of accessible memory regions/pages in a Windows process.
// Requires -lpsapi and -ldbghelp

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <map>
#include <vector>
#include <string>

#include <windows.h>
#include <psapi.h>
#include <dbghelp.h>

#define IMAGE_HEADER_PAGE_SIZE 0x1000

typedef unsigned char u8;
typedef unsigned int u32;
typedef unsigned long long u64;

struct Region {
	u64 base;
	u32 size;
	bool pm_read;
	bool pm_write;
	bool pm_exec;
	std::string name;
};

void get_section_names(HANDLE proc, u64 base, u8 *buf, std::map<u64, std::string>& sections) {
	SIZE_T len = 0;
	ReadProcessMemory(proc, (LPCVOID)base, (LPVOID)buf, IMAGE_HEADER_PAGE_SIZE, &len);
	if (len != IMAGE_HEADER_PAGE_SIZE)
		return;

	IMAGE_NT_HEADERS *headers = ImageNtHeader((LPVOID)buf);
	if (!headers)
		return;

	int n_sections = headers->FileHeader.NumberOfSections;
	IMAGE_SECTION_HEADER *section = IMAGE_FIRST_SECTION(headers);

	for (int i = 0; i < n_sections; i++) {
		sections[base + section->VirtualAddress] = (char*)section->Name;
		section++;
	}
}

int main(int argc, char **argv) {
	if (argc < 2) {
		printf("Insufficient arguments\nUsage: %s <PID>\n", argv[0]);
		return 1;
	}

	int pid = strtol(argv[1], NULL, 0);

	HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, 0, pid);
	if (!proc) {
		printf("Could not open process\n");
		return 2;
	}

	MEMORY_BASIC_INFORMATION info = {0};
	VirtualQueryEx(proc, (LPCVOID)0, &info, sizeof(MEMORY_BASIC_INFORMATION));
	u64 base = info.RegionSize;

	char name[1000];
	u8 *img_hdr = (u8*)malloc(IMAGE_HEADER_PAGE_SIZE);

	std::map<u64, std::string> sections;
	std::vector<Region> regions;

	u64 total = 0;

	while (1) {
		if (!VirtualQueryEx(proc, (LPCVOID)base, &info, sizeof(MEMORY_BASIC_INFORMATION)))
			break;

		if (info.State == 0x1000 && (info.Protect & PAGE_GUARD) == 0) {
			int protect = info.Protect & 0xff;
			if (protect != PAGE_NOACCESS && protect != PAGE_EXECUTE) {
				regions.push_back({});
				Region& reg = regions.back();

				reg.base = base;
				reg.size = info.RegionSize;
				reg.pm_read = true;
				reg.pm_write = 
					protect != PAGE_EXECUTE_READ &&
					protect != PAGE_READONLY;
				reg.pm_exec =
					protect == PAGE_EXECUTE_READ ||
					protect == PAGE_EXECUTE_READWRITE ||
					protect == PAGE_EXECUTE_WRITECOPY;

				int len = GetMappedFileNameA(proc, (LPVOID)base, name, 1000);
				if (len > 0) {
					char *str = strrchr(name, '\\');
					reg.name = str ? str+1 : name;
				}
				else
					reg.name = "";

				if (info.RegionSize == IMAGE_HEADER_PAGE_SIZE)
					get_section_names(proc, base, img_hdr, sections);

				auto it = sections.find(base);
				if (it != sections.end()) {
					reg.name += " ";
					reg.name += it->second;
				}

				total += reg.size;
			}
		}

		base += info.RegionSize;
	}

	CloseHandle(proc);
	free(img_hdr);

	for (auto& reg : regions) {
		printf(
			"%016llx %08x %c%c%c %s\n",
			reg.base, reg.size,
			reg.pm_read ? 'r' : '-',
			reg.pm_write ? 'w' : '-',
			reg.pm_exec ? 'x' : '-',
			reg.name.c_str()
		);
	}

	printf("\nTotal Memory: %#x (%g MiB)\n", total, (double)total / (1024 * 1024));

	return 0;
}