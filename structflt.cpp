#include <iostream>
#include <iomanip>
#include <fstream>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <cmath>

typedef unsigned char u8;
typedef unsigned long long u64;
typedef signed long long s64;

const int nTypes = 5;

const char *typeNames[] = {
	"binary", "dec", "hex", "float", "string"
};

enum Field_Type {
	BIN,
	DEC,
	HEX,
	FLOAT,
	STRING,
	INVALID
};

struct Field {
	int bytes_per_elem;      // multiply by n_elems to get the full size of this field
	Field_Type type;         // variable type
	bool use_caps = false;   // determines whether to capitalise digits, eg. 0x4A vs 0x4a
	int dgs_int = -1;        // number of digits for the integral part. -1 means fewest digits possible without truncation
	int dgs_frac = -1;       // number of digits for the fractional part
	char name[64];           // variable name
	int n_elems = 1;         // array size
	bool is_signed = false;  // controls signedness if the variable is an integer type
	bool big_endian = false; // controls endianness if the variable is not a string
};

bool parse_field(Field& fld, std::vector<std::string>& args) {
	if (args.size() < 3) {
		std::cout << "Not enough information to create a field";
		return false;
	}

	fld.bytes_per_elem = std::stoi(args[0]);
	if (fld.bytes_per_elem < 1) {
		std::cout << "Invalid element size " << fld.bytes_per_elem;
		return false;
	}

	const char *type_str = args[1].c_str();
	const char *p = type_str;
	int i;
	for (i = 0; *p; i++, p++) {
		if (*p >= 'A' && *p <= 'Z') {
			args[1][i] += 0x20;
			fld.use_caps = true;
		}
	}

	int tok_len = i;
	fld.type = INVALID;

	for (i = 0; i < nTypes; i++) {
		const char *a = type_str;
		const char *b = typeNames[i];
		bool match = true;

		for (int j = 0; *a && *b; a++, b++, j++) {
			if (*a != *b) {
				match = false;
				break;
			}
		}
		if (match) {
			fld.type = (Field_Type)i;
			break;
		}
	}

	if (fld.type == INVALID) {
		std::cout << "Invalid field type \"" << args[1] << "\"";
		return false;
	}

	if (fld.type == FLOAT && fld.bytes_per_elem != 4 && fld.bytes_per_elem != 8) {
		std::cout << "Unsupported floating-point size " << fld.bytes_per_elem;
		return false;
	}
	if (fld.type != STRING && fld.bytes_per_elem > 8) {
		std::cout << "Unsupported size " << fld.bytes_per_elem << " for non-string type";
		return false;
	}

	int cur = 2;

	// Check to see if this argument is a format specifier
	if ((args[2][0] >= '0' && args[2][0] <= '9') || args[2][0] == '.') {
		int dot_idx = args[2].find('.');
		if (dot_idx >= 0 && dot_idx < args[2].size() - 1)
			fld.dgs_frac = std::stoi(args[2].substr(dot_idx + 1));

		if (dot_idx != 0)
			fld.dgs_int = std::stoi(args[2]);

		cur++;
	}

	if (fld.type == STRING && (fld.dgs_int >= 0 || fld.dgs_frac >= 0)) {
		std::cout << "Format specifier for string type is not supported";
		return false;
	}

	if (fld.type != FLOAT && fld.dgs_frac >= 0) {
		std::cout << "Fractional digits for non-float types is not supported";
		return false;
	}

	strncpy(fld.name, args[cur++].c_str(), 63);
	fld.name[63] = 0;

	// Optional args at the end
	for (int i = cur; i < args.size(); i++) {
		// if it's a number, treat it as the number of elements
		if (args[i][0] >= '0' && args[i][0] <= '9') {
			fld.n_elems = std::stoi(args[i]);
			if (fld.n_elems < 1) {
				std::cout << "Invalid array size " << fld.n_elems;
				return false;
			}
		}
		switch (args[i][0]) {
			case 'b':
			case 'B':
				fld.big_endian = true;
				break;
			case 'l':
			case 'L':
				fld.big_endian = false;
				break;
			case 's':
			case 'S':
				fld.is_signed = true;
				break;
			case 'u':
			case 'U':
				fld.is_signed = false;
				break;
		}
	}

	if (fld.big_endian && fld.type == STRING) {
		std::cout << "Big-endian format for string type is not supported";
	}

	return true;
}

void print_field(Field& fld, u8 *elem) {
	if (fld.type == STRING) {
		char *str = (char*)elem;
		std::cout << std::string(str, fld.bytes_per_elem);
		return;
	}

	int n = 1;
	const bool sys_be = *((char*)&n) != 1;

	u8 temp[8] = {0};
	if (fld.big_endian != sys_be) {
		int n = fld.bytes_per_elem;
		for (int j = 0; j < n; j++)
			temp[n-1-j] = elem[j];
	}
	else
		memcpy(&temp[0], elem, fld.bytes_per_elem);

	// TODO: support digit counts, integral and fractional
	if (fld.type == FLOAT) {
		char format[16] = {0};
		if (fld.dgs_int <= 0 && fld.dgs_frac <= 0)
			strcpy(format, "%g");
		else if (fld.dgs_int > 0 && fld.dgs_frac > 0)
			sprintf(format, "%%%d.%df", fld.dgs_int, fld.dgs_frac);
		else if (fld.dgs_int > 0)
			sprintf(format, "%%%d.f", fld.dgs_int);
		else
			sprintf(format, "%%.%df", fld.dgs_frac);

		if (fld.bytes_per_elem == 8)
			printf(format, *(double*)(&temp[0]));
		else if (fld.bytes_per_elem == 4)
			printf(format, *(float*)(&temp[0]));

		return;
	}

	u64 uvar = *(u64*)(&temp[0]);
	if (!uvar && fld.dgs_int <= 0) {
		std::cout << "0";
		return;
	}

	int base = 0;
	switch (fld.type) {
		case BIN:
			base = 2;
			std::cout << "0b";
			break;
		case DEC:
			base = 10;
			break;
		case HEX:
			base = 16;
			std::cout << "0x";
			break;
	}

	bool neg = (uvar & (1 << 63));
	s64 var = *(s64*)(&temp[0]);
	char num[68] = {0};
	char *p = &num[0];

	if (neg && fld.is_signed) {
		*p++ = '-';
		var = -var;
	}

	int i = 0;
	while (true) {
		if (fld.dgs_int < 0) {
			if (!var) break;
		}
		else if (i >= fld.dgs_int)
			break;

		int d = (int)(var % (s64)base);
		var /= (s64)base;

		if (d >= 10) {
			if (fld.use_caps)
				*p++ = d - 10 + 'A';
			else
				*p++ = d - 10 + 'a';
		}
		else
			*p++ = d + '0';

		i++;
	}

	std::string var_str(num);
	std::reverse(var_str.begin(), var_str.end());
	std::cout << var_str;
}

void read_data(FILE *f, std::vector<u8>& buf) {
	u8 temp[256];
	while (1) {
		int len = fread(temp, 1, 256, f);
		if (len <= 0)
			break;

		buf.insert(buf.end(), temp, temp+len);
	}
}

int main(int argc, char **argv) {
	if (argc < 2) {
		std::cout << "Invalid arguments\n"
			"Usage: " << argv[0] << " <struct file> [input data file]\n"
			"The default source for input data is stdin\n"
			"The format for each line in the struct file goes as such:\n"
			" <bytes> <type> [print format] <name> [array length] [signedness] [endianness]\n"
			" where <type> can be float, string, dec, hex or binary.\n"
			"Properties marked by square brackets are optional.\n"
		;
		return 1;
	}

	std::ifstream in(argv[1]);
	if (!in) {
		std::cout << "Could not open struct \"" << argv[1] << "\"";
		return 2;
	}

	std::vector<Field> model;
	std::string str;

	int line_no = 0;
	bool failed = false;
	while (std::getline(in, str)) {
		line_no++;
		if (str.size() <= 0)
			continue;

		std::vector<std::string> tokens;
		int span = 0;
		const char *p = str.c_str();

		while (*p) {
			if (*p == ' ') {
				if (span)
					tokens.push_back(std::string(p-span, p));

				span = 0;
			}
			else
				span++;

			p++;
		}
		if (span)
			tokens.push_back(std::string(p-span, p));

		Field fld;
		if (!parse_field(fld, tokens)) {
			std::cout << " (line " << line_no << ")\n";
			failed = true;
			break;
		}

		model.push_back(fld);
	}
	in.close();

	if (failed)
		return 3;

	FILE *f = NULL;
	if (argc > 2) {
		f = fopen(argv[2], "rb");
		if (!f) {
			std::cout << "Could not open data file \"" << argv[2] << "\"";
			return 4;
		}
	}
	if (!f)
		f = stdin;

	std::vector<u8> data;
	read_data(f, data);
	if (f != stdin)
		fclose(f);

	int struct_size = 0;
	int name_width = 0;
	for (Field& fld : model) {
		struct_size += fld.bytes_per_elem * fld.n_elems;

		int len = strlen(fld.name);
		if (len > name_width)
			name_width = len;
	}

	if (data.size() < struct_size) {
		std::cout << "Not enough data to fill the struct\n"
			"   required = " << struct_size << " bytes, given = " << data.size() << " bytes\n"
		;
		return 5;
	}

	u8 *elem = &data[0];
	for (Field& fld : model) {
		std::cout << std::setw(name_width) << fld.name << " = ";

		for (int i = 0; i < fld.n_elems; i++) {
			if (i > 0)
				std::cout << ", ";

			print_field(fld, elem);
			elem += fld.bytes_per_elem;
		}

		std::cout << "\n";
	}

	return 0;
}
