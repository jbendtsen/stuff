#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstring>

#ifdef _WIN32
 #include <windows.h>
#else
 #include <unistd.h>
 #include <sys/types.h>
 #include <sys/stat.h>
 #include <sys/ptrace.h>
 #include <sys/wait.h>
 #include <fcntl.h>
#endif

typedef unsigned char u8;

struct buffer {
	std::string name;
	u8 *buf;
	int size;

	buffer(std::string *n, int sz);
	~buffer();

	void resize(int sz);
	void copy(buffer *src, int dst_off = 0, int src_off = 0, int len = 0);
	void fill(u8 byte = 0, int off = 0, int len = 0);
	void patch(int off, std::vector<u8> *b);
	void print(int off = 0, int len = 0);
	void load(std::string *file);
	void save(std::string *file);
	void memacc(int pid, long addr, int len = 0, int off = 0, int mode = 0);
};

buffer::buffer(std::string *n, int sz) {
	if (n == nullptr || sz < 1) return;
	name = *n;
	size = sz;
	buf = new u8[size];
	memset(buf, 0, size);
}

buffer::~buffer() {
	delete[] buf;
}

void buffer::resize(int sz) {
	if (sz < 1) {
		std::cout << "Error: invalid size " << sz << "\n";
		return;
	}

	u8 *b = new u8[sz];

	if (sz > size) {
		memcpy(b, buf, size);
		memset(b+size, 0, sz-size);
	}
	else memcpy(b, buf, sz);

	delete[] buf;
	buf = b;
	size = sz;
}

void buffer::copy(buffer *src, int dst_off, int src_off, int len) {
	if (src == nullptr || src->buf == nullptr || dst_off >= size || src_off >= src->size) return;

	if (len <= 0) len = src->size;
	if (src_off < 0) src_off = 0;
	if (src_off+len > src->size) len = src->size - src_off;

	if (dst_off < 0) dst_off = 0;
	if (dst_off+len > size) len = size - dst_off;

	memcpy(buf+dst_off, src->buf+src_off, len);
}

void buffer::fill(u8 byte, int off, int len) {
	if (off < 0 || off >= size) return;
	if (len <= 0 || len > size) len = size;
	if (off+len > size) len = size - off;

	memset(buf+off, byte, len);
}

void buffer::patch(int off, std::vector<u8> *b) {
	if (b == nullptr || off < 0 || off >= size) return;

	int i;
	for (i = 0; i < b->size() && i < size-off; i++) buf[off+i] = (*b)[i];
}

void buffer::print(int off, int len) {
	if (off < 0 || off >= size) return;
	if (len <= 0 || len > size) len = size;
	if (off+len > size) len = size - off;

	bool hex = true;
	int sp = 0;
	for (int i = 0; i < len; i++) {
		if (hex) {
			if (i % 16 == 0) std::cout << std::setw(8) << std::right << std::setfill(' ') << std::hex << off+i << " | ";
			sp += printf("%02x ", buf[off+i]);
			if (i % 16 == 15 || i == len-1) {
				if (i == len-1) {
					std::cout << std::setw(56 - sp) << std::setfill(' ');
				}
				std::cout << "| ";
				hex = false;
				sp = 0;
				i = (i & ~0xf) - 1;
			}
			else if (i % 4 == 3) sp += printf("- ");
		}
		else {
			if (buf[off+i] >= 0x20 && buf[off+i] < 0x7f) std::cout << buf[off+i];
			else std::cout << ".";
			if (i % 16 == 15 || i == len-1) {
				std::cout << "\n";
				hex = true;
			}
		}
	}
	std::cout << "\n";
}

void buffer::load(std::string *file) {
	if (file == nullptr) return;

	std::fstream in;
	in.open(*file, std::fstream::in | std::fstream::binary | std::fstream::ate);
	if (!in.is_open()) {
		std::cout << "Error: could not open " << *file << "\n";
		return;
	}

	int sz = in.tellg();
	in.seekg(in.beg);
	if (sz < 1) {
		std::cout << "Error: " << *file << " is empty" << "\n";
		return;
	}

	u8 *b = new u8[sz];
	in.read((char*)b, sz);
	in.close();

	if (sz < size) memcpy(buf, b, sz);
	else memcpy(buf, b, size);

	delete[] b;
}

void buffer::save(std::string *file) {
	if (file == nullptr) return;

	std::fstream out;
	out.open(*file, std::fstream::out | std::fstream::binary);
	out.write((char*)buf, size);
	out.close();
}

#ifdef _WIN32

void buffer::memacc(int pid, long addr, int len, int off, int mode) {
	if (off < 0 || off >= size) return;
	if (len <= 0 || len > size) len = size;
	if (off+len > size) len = size - off;

	HANDLE proc = OpenProcess(PROCESS_ALL_ACCESS, 0, pid);
	if (proc == nullptr) {
		std::cout << "Error: could not open process " << pid << "\n";
		return;
	}

	unsigned long s = 0;
	bool res;
	if (!mode) {
		res = ReadProcessMemory(proc, (void*)addr, buf+off, len, &s);
		if (res == false || !s)
			std::cout << "Error: could not read memory from process " << pid << "\n";

		if (s != len)
			std::cout << s << "/" << len << " bytes were read" << "\n";
	}
	else {
		res = WriteProcessMemory(proc, (void*)addr, buf+off, len, &s);
		if (res == false || !s)
			std::cout << "Error: could not write memory to process " << pid << "\n";

		if (s != len)
			std::cout << s << "/" << len << " bytes were written" << "\n";

		FlushInstructionCache(proc, (void*)addr, len);
	}

	CloseHandle(proc);
}

#else

void buffer::memacc(int pid, long addr, int len, int off, int mode) {
	if (off < 0 || off >= size) return;
	if (len <= 0 || len > size) len = size;
	if (off+len > size) len = size - off;

	std::string pn = "/proc/" + std::to_string(pid) + "/mem";
	int proc = open(pn.c_str(), mode ? O_RDWR : O_RDONLY);
	if (proc < 0) {
		std::string msg = "Could not open process at " + pn;
		perror(msg.c_str());
		return;
	}

	int err = 0;
	if (ptrace(PTRACE_ATTACH, pid, NULL, NULL) < 0) {
		perror("Could not attach to process");
		err = 1;
	}
	if (!err && waitpid(pid, NULL, 0) != pid) {
		perror("Could not wait for process");
		err = 2;
	}

	if (!err) {
		lseek(proc, addr, SEEK_SET);
		int r = mode ? write(proc, buf+off, len) : read(proc, buf+off, len);
		if (r < 1) {
			std::string rw = (mode ? "write" : "read");
			std::string msg = "Could not " + rw + " memory";
			perror(msg.c_str());
			err = 2;
		}
	}

	if (err != 1) ptrace(PTRACE_DETACH, pid, NULL, NULL);
	close(proc);
}

#endif

std::vector<buffer*> list;

int indexof(std::string *name) {
	if (name == nullptr) return -1;
	for (int i = 0; i < list.size(); i++) {
		if (list[i] == nullptr) continue;
		if (list[i]->name == *name) return i;
	}
	return -1;
}

long read_number(const char *str, int mode = 1) {
	if (str == nullptr) return 0;
	if (mode == 0) return atol(str);
	if (mode == 1) {
		if (str[0] == '0' && str[1] == 'x') return strtol(str, NULL, 16);
		else return atol(str);
	}
	if (mode == 2) return strtol(str, NULL, 16);
	return 0;
}

int to_num(char c) {
	if (c > 0x2f && c < 0x3a) return c-0x30;
	if (c > 0x40 && c < 0x5b) return c-0x37;
	if (c > 0x60 && c < 0x7b) return c-0x57;
	return -1;
}

struct command {
	std::string name;
	int min_args, max_args;
	std::string desc;
	std::string usage;
};

command commands[] = {
	{"help",   0, 1,  "Prints this help message", "help [command]"},
	{"quit",   0, 0,  "Exits the program", "quit"},
	{"list",   0, 0,  "Prints the current list of buffers", "list"},
	{"create", 2, 2,  "Creates a new buffer of a certain size", "create <name of new buffer> <size>"},
	{"delete", 1, 1,  "Deletes an existing buffer", "delete <name of buffer>"},
	{"resize", 2, 2,  "Resizes a buffer", "resize <buffer> <new size>"},
	{"copy",   2, 5,  "Copies data from one buffer to another", "copy <dest buffer> <src buffer> [dst offset] [src offset] [length]"},
	{"fill",   1, 4,  "Fills a range of data in a buffer with a value", "fill <buffer> [byte value] [offset] [length]"},
	{"patch",  3, -1, "Patches a byte array to a buffer at an offset", "patch <buffer> <offset> <byte array>"},
	{"print",  1, 3,  "Displays data from a buffer", "print <buffer> [offset] [length]"},
	{"load",   2, 2,  "Loads data from a file into a buffer", "load <buffer> <file name>"},
	{"save",   2, 2,  "Saves data from a buffer to a file", "save <buffer> <file name>"},
	{"read",   3, 5,  "Reads data from a running process into a buffer", "read <buffer> <PID> <address> [length] [offset]"},
	{"write",  3, 5,  "Writes data from a buffer to a running process", "write <buffer> <PID> <address> [length] [offset]"}
};
int n_cmds = sizeof(commands) / sizeof(command);

int main(int argc, char **argv) {
	std::cout << "Buffer Tool\n"
	     << "Type \"help\" for the list of available commands or\n"
	     << "\"help <command>\" for how to use a particular command\n\n";

	std::string cmd_str;
	std::vector<std::string> cmd;
	while (1) {
		cmd.clear();
		std::cout << "> ";
		getline(std::cin, cmd_str);
		std::cout << "\n";

		int cur = 0, next = -1;
		do {
			next = cmd_str.find(" ", cur);
			cmd.push_back(cmd_str.substr(cur, next-cur));
			cur = next + 1;
		} while (next != std::string::npos);

		int c_idx;
		for (c_idx = 0; c_idx < n_cmds; c_idx++) {
			if (commands[c_idx].name == cmd[0]) break;
		}
		if (c_idx == n_cmds) {
			std::cout << "Unknown command \"" << cmd[0] << "\"\n";
			continue;
		}

		if (cmd.size() < commands[c_idx].min_args + 1 && commands[c_idx].min_args > 0) {
			std::cout << "Not enough arguments to power \"" << commands[c_idx].name << "\"\n"
			     << "Type \"help " << commands[c_idx].name << "\" for valid usage\n\n";
			continue;
		}
		if (cmd.size() > commands[c_idx].max_args + 1 && commands[c_idx].max_args > 0) {
			std::cout << "Too many arguments to power \"" << commands[c_idx].name << "\"\n"
			     << "Type \"help " << commands[c_idx].name << "\" for valid usage\n\n";
			continue;
		}

		if (c_idx == 0) { // help
			if (cmd.size() == 1) {
				for (int i = 0; i < n_cmds; i++) {
					std::cout << std::setw(9) << std::left << std::setfill(' ') << commands[i].name << commands[i].desc << "\n";
				}
			}
			else {
				for (c_idx = 0; c_idx < n_cmds; c_idx++)
					if (commands[c_idx].name == cmd[1]) break;

				if (c_idx < n_cmds)
					std::cout << std::setw(9) << std::left << std::setfill(' ') << commands[c_idx].name << "\n    "
					          << commands[c_idx].desc << "\n    Usage: " << commands[c_idx].usage << "\n";
				else
					std::cout << "Unknown command \"" << cmd[1] << "\"\n";
			}
			c_idx = 0;
			std::cout << "\n";
		}

		if (c_idx == 1) { // quit
			break;
		}

		if (c_idx == 2) { // list
			for (int i = 0; i < list.size(); i++) {
				if (list[i] == nullptr) continue;
				std::cout << "\t" << list[i]->name << " - 0x" << std::hex << list[i]->size << " (" << std::dec << list[i]->size << ")\n";
			}
			std::cout << "\n";
		}

		if (c_idx == 3) { // create
			if (indexof(&cmd[1]) >= 0) {
				std::cout << "Error: the buffer \"" << cmd[1] << "\" already exists\n";
				continue;
			}
			int sz = read_number(cmd[2].c_str());
			if (sz < 1) {
				std::cout << "Error: invalid size " << sz << "\n";
				continue;
			}
			buffer *buf = new buffer(&cmd[1], sz);
			list.push_back(buf);
		}

		int idx = -1;
		if (c_idx >= 4) {
			idx = indexof(&cmd[1]);
			if (idx < 0) {
				std::cout << "Error: could not find the buffer \"" << cmd[1] << "\"\n";
				continue;
			}
		}

		if (c_idx == 4) { // delete
			delete list[idx];
			list[idx] = nullptr;
		}

		if (c_idx == 5) { // resize
			int sz = read_number(cmd[2].c_str());
			list[idx]->resize(sz);
		}

		if (c_idx == 6) { // copy
			int src_idx = indexof(&cmd[2]);
			if (src_idx < 0) {
				std::cout << "Error: could not find the buffer \"" << cmd[2] << "\"\n";
				continue;
			}
			int dst_off = 0, src_off = 0, len = 0;
			if (cmd.size() > 3) dst_off = read_number(cmd[3].c_str());
			if (cmd.size() > 4) src_off = read_number(cmd[4].c_str());
			if (cmd.size() > 5) len = read_number(cmd[5].c_str());
			list[idx]->copy(list[src_idx], dst_off, src_off, len);
		}

		if (c_idx == 7) { // fill
			int byte = 0, off = 0, len = 0;
			if (cmd.size() > 2) byte = read_number(cmd[2].c_str());
			if (cmd.size() > 3) off = read_number(cmd[3].c_str());
			if (cmd.size() > 4) len = read_number(cmd[4].c_str());
			list[idx]->fill((u8)byte, off, len);
		}

		if (c_idx == 8) { // patch
			int off = read_number(cmd[2].c_str());
			int n = -1;
			std::vector<u8> array;
			for (int i = 3; i < cmd.size(); i++) {
				for (int j = 0; j < cmd[i].size(); j++) {
					if (n < 0) n = to_num(cmd[i].at(j));
					else {
						int t = to_num(cmd[i].at(j));
						if (t >= 0) {
							n = (n << 4) | t;
							array.push_back((u8)n);
							n = -1;
						}
					}
				}
			}
			if (n >= 0) array.push_back((u8)n);
			list[idx]->patch(off, &array);
		}

		if (c_idx == 9) { // print
			int off = 0, len = 0;
			if (cmd.size() > 2) off = read_number(cmd[2].c_str());
			if (cmd.size() > 3) len = read_number(cmd[3].c_str());
			list[idx]->print(off, len);
		}

		if (c_idx == 10) { // load
			list[idx]->load(&cmd[2]);
		}

		if (c_idx == 11) { // save
			list[idx]->save(&cmd[2]);
		}

		long addr = 0;
		int pid = 0, len = 0, off = 0;
		if (c_idx >= 12) {
			pid = read_number(cmd[2].c_str());
			addr = read_number(cmd[3].c_str(), 2);
			if (cmd.size() > 4) len = read_number(cmd[4].c_str());
			if (cmd.size() > 5) off = read_number(cmd[5].c_str());
		}
			
		if (c_idx == 12) { // read
			list[idx]->memacc(pid, addr, len, off, 0);
		}

		if (c_idx == 13) { // write
			list[idx]->memacc(pid, addr, len, off, 1);
		}
	}

	for (int i = 0; i < list.size(); i++) {
		if (list[i] == nullptr) continue;
		delete list[i];
	}
	return 0;
}