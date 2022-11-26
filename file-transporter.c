// unix:  gcc file-transporter.c -o file-transporter
// mingw: x86_64-w64-mingw32-gcc file-transporter.c -lws2_32 -o file-transporter.exe

#define _LARGEFILE64_SOURCE
#define _FILE_OFFSET_BITS 64

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define SEND 1
#define RECV 2

int parse_ip_address(unsigned char *addr, char *str, int len) {
	if (len <= 0)
		len = strlen(str);

	typedef struct {
		char buf[16];
		int cur;
	} IP_Info;

	IP_Info ipv4 = {0};
	IP_Info ipv6_1 = {0};
	IP_Info ipv6_2 = {0};
	IP_Info *ipv6 = &ipv6_1;

	int ipv4_n = 0;
	int ipv6_n = 0;

	int invalidated = 0;

	for (int i = 0; i < len && invalidated != 3; i++) {
		char c = str[i];
		if (c == '/')
			break;

		if (ipv4.cur < 4 && (invalidated & 1) == 0) {
			if (c == '.') {
				ipv4.buf[ipv4.cur++] = (unsigned char)ipv4_n;
				ipv4_n = 0;
			}
			else if (c >= '0' && c <= '9') {
				ipv4_n = ipv4_n * 10 + (c - '0');
			}
			else {
				invalidated |= 1;
			}
		}

		if (ipv6->cur < 8 && (invalidated & 2) == 0) {
			if (c == ':') {
				printf("%x\n", ipv6_n);
				if (i > 0 && str[i-1] == ':') {
					if (ipv6 == &ipv6_1)
						ipv6 = &ipv6_2;
					else
						invalidated |= 2;
				}
				else {
					ipv6->buf[2*ipv6->cur] = (unsigned char)(ipv6_n >> 8);
					ipv6->buf[2*ipv6->cur+1] = (unsigned char)(ipv6_n & 0xff);
					ipv6_n = 0;
					ipv6->cur++;
				}
			}
			else if (c >= '0' && c <= '9') {
				ipv6_n = ipv6_n * 16 + (c - '0');
			}
			else if (c >= 'A' && c <= 'F') {
				ipv6_n = ipv6_n * 16 + (c + 10 - 'A');
			}
			else if (c >= 'a' && c <= 'f') {
				ipv6_n = ipv6_n * 16 + (c + 10 - 'a');
			}
			else {
				invalidated |= 2;
			}
		}
	}

	if (ipv4.cur < 4)
		ipv4.buf[ipv4.cur++] = (unsigned char)ipv4_n;

	if (ipv6->cur < 8) {
		ipv6->buf[2*ipv6->cur] = (unsigned char)(ipv6_n >> 8);
		ipv6->buf[2*ipv6->cur+1] = (unsigned char)(ipv6_n & 0xff);
		ipv6->cur++;
	}

	if (ipv6_1.cur + ipv6_2.cur <= 8) {
		unsigned char *src = &ipv6_2.buf[0];
		unsigned char *dst = &ipv6_1.buf[ipv6_1.cur * 2];
		int right = 8 - ipv6_1.cur;
		int j;
		for (j = 0; j < right - ipv6_2.cur; j++) {
			*dst++ = 0;
			*dst++ = 0;
		}
		for ( ; j < right; j++) {
			*dst++ = *src++;
			*dst++ = *src++;
		}
		ipv6_1.cur = 8;
		ipv6 = &ipv6_1;
	}
	else {
		invalidated |= 2;
	}

	if (invalidated == 2) {
		for (int i = 0; i < 4; i++)
			addr[i] = i < ipv4.cur ? ipv4.buf[i] : 0;
		return 4;
	}
	else if (invalidated == 1) {
		for (int i = 0; i < 16; i++)
			addr[i] = i < ipv6->cur * 2 ? ipv6->buf[i] : 0;
		return 16;
	}

	return 0;
}

void *file_open(const char *file_name, int writing, int64_t *file_size); // returns a handle to the new file, or null on error
void file_seek(void *handle, int64_t pos); // pos is relative to the start of the file, not the current position
int64_t file_read(void *handle, char *buf, int64_t size);
int64_t file_write(void *handle, char *buf, int64_t size);
void file_close(void *handle);

void sock_api_init();
int sock_new(int is_ipv6);
int sock_client_connect(int handle, int is_ipv6, int port, unsigned char *ip_addr, int ip_len);
int sock_server_obtain_client(int handle, int is_ipv6, int port, unsigned char *ip_addr, int ip_len);
int sock_read(int handle, void *buf, int size);
int sock_write(int handle, void *buf, int size);
void sock_close(int handle);
int sock_last_error();
void sock_api_close();

#ifdef _WIN32

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <winsock2.h>
#include <ws2tcpip.h>

void *file_open(const char *file_name, int writing, int64_t *file_size) {
	DWORD access = writing ? GENERIC_WRITE : GENERIC_READ;
	DWORD creation = writing ? CREATE_ALWAYS : OPEN_EXISTING;
	HANDLE fh = CreateFileA(file_name, access, FILE_SHARE_READ, NULL, creation, FILE_ATTRIBUTE_NORMAL, NULL);

	if (fh == INVALID_HANDLE_VALUE)
		return NULL;

	if (file_size) {
		LARGE_INTEGER size = {0};
		GetFileSizeEx(fh, &size);
		*file_size = size.QuadPart;
	}

	return (void*)fh;
}

void file_seek(void *handle, int64_t pos) {
	LARGE_INTEGER new_pos;
	new_pos.QuadPart = pos;
	SetFilePointerEx((HANDLE)handle, new_pos, NULL, FILE_BEGIN);
}

int64_t file_read(void *handle, char *buf, int64_t size) {
	int64_t total = 0;

	while (size > 0) {
		DWORD chunk = size <= 0x7fffffff ? (DWORD)size : 0x7fffffff;
		int res = 0;
		ReadFile((HANDLE)handle, buf, chunk, (DWORD*)&res, NULL); 
		if (res <= 0)
			break;

		size -= res;
		total += res;
		buf += res;
	}

	return total;
}

int64_t file_write(void *handle, char *buf, int64_t size) {
	int64_t total = 0;

	while (size > 0) {
		DWORD chunk = size <= 0x7fffffff ? (DWORD)size : 0x7fffffff;
		int res = 0;
		WriteFile((HANDLE)handle, buf, chunk, (DWORD*)&res, NULL); 
		if (res <= 0)
			break;

		size -= res;
		total += res;
		buf += res;
	}

	return total;
}

void file_close(void *handle) {
	CloseHandle((HANDLE)handle);
}

void sock_api_init() {
	WSADATA wsa;
	WSAStartup(MAKEWORD(2,2), &wsa);
}

int sock_new(int is_ipv6) {
	return (int)socket(is_ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

int sock_client_connect(int handle, int is_ipv6, int port, unsigned char *ip_addr, int ip_len) {
	struct sockaddr_in addr_ipv4 = {0};
	struct sockaddr_in6 addr_ipv6 = {0};
	struct sockaddr *addr;
	int addr_len;

	if (is_ipv6) {
		addr = (struct sockaddr *)&addr_ipv6;
		addr_len = sizeof(addr_ipv6);
		addr_ipv6.sin6_family = AF_INET6;
		addr_ipv6.sin6_port = htons(port);
		for (int i = 0; i < 16; i++)
			addr_ipv6.sin6_addr.s6_addr[i] = i < ip_len ? ip_addr[i] : 0;
	}
	else {
		addr = (struct sockaddr *)&addr_ipv4;
		addr_len = sizeof(addr_ipv4);
		addr_ipv4.sin_family = AF_INET;
		addr_ipv4.sin_port = htons(port);
		addr_ipv4.sin_addr.s_addr = *(int*)ip_addr;
	}

	return connect((SOCKET)handle, addr, sizeof(*addr));
}

int sock_server_obtain_client(int handle, int is_ipv6, int port, unsigned char *ip_addr, int ip_len) {
	struct sockaddr_in server_addr_ipv4 = {0};
	struct sockaddr_in6 server_addr_ipv6 = {0};
	struct sockaddr *server_addr;
	int addr_len;

	if (is_ipv6) {
		server_addr = (struct sockaddr *)&server_addr_ipv6;
		addr_len = sizeof(server_addr_ipv6);
		server_addr_ipv6.sin6_family = AF_INET6;
		server_addr_ipv6.sin6_port = htons(port);
		for (int i = 0; i < 16; i++)
			server_addr_ipv6.sin6_addr.s6_addr[i] = i < ip_len ? ip_addr[i] : 0;
	}
	else {
		server_addr = (struct sockaddr *)&server_addr_ipv4;
		addr_len = sizeof(server_addr_ipv4);
		server_addr_ipv4.sin_family = AF_INET;
		server_addr_ipv4.sin_port = htons(port);
		server_addr_ipv4.sin_addr.s_addr = *(int*)ip_addr;
	}

	int res = bind((SOCKET)handle, server_addr, addr_len);
	if (res < 0) {
		printf("bind() failed\n");
		return res;
	}

	res = listen((SOCKET)handle, 1);
	if (res < 0) {
		printf("listen() failed\n");
		return res;
	}

	struct sockaddr_in6 dummy = {0}; // sizeof(sockaddr_in6) > sizeof(sockaddr_in)
	int dummy_len = addr_len;
	SOCKET client = accept((SOCKET)handle, (struct sockaddr *)&dummy, &dummy_len);
	if (client == INVALID_SOCKET) {
		printf("accept() failed\n");
		return -1;
	}

	return (int)client;
}

int sock_read(int handle, void *buf, int size) {
	return recv((SOCKET)handle, buf, size, 0);
}

int sock_write(int handle, void *buf, int size) {
	return send((SOCKET)handle, buf, size, 0);
}

void sock_close(int handle) {
	shutdown(handle, SD_BOTH);
	closesocket(handle);
}

int sock_last_error() {
	return WSAGetLastError();
}

void sock_api_close() {
	WSACleanup();
}

#else

#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>

void *file_open(const char *file_name, int writing, int64_t *file_size) {
	int fd;
	if (writing)
		fd = open(file_name, O_CREAT | O_TRUNC | O_WRONLY, 0777);
	else
		fd = open(file_name, O_RDONLY);

	if (fd > 0) {
		if (file_size) {
			int64_t offset = lseek(fd, 0, SEEK_END);
			lseek(fd, 0, SEEK_SET);
			*file_size = offset;
		}
		return (void*)(int64_t)fd;
	}

	return NULL;
}

void file_seek(void *handle, int64_t pos) {
	lseek((int64_t)handle, pos, SEEK_SET);
}

int64_t file_read(void *handle, char *buf, int64_t size) {
	int fd = (int64_t)handle;
	int64_t total = 0;

	while (size > 0) {
		int chunk = size <= 0x7fffffff ? size : 0x7fffffff;
		int res = read(fd, buf, chunk);
		if (res <= 0)
			break;

		size -= res;
		total += res;
		buf += res;
	}

	return total;
}

int64_t file_write(void *handle, char *buf, int64_t size) {
	int fd = (int64_t)handle;
	int64_t total = 0;

	while (size > 0) {
		int chunk = size <= 0x7fffffff ? size : 0x7fffffff;
		int res = write(fd, buf, chunk);
		if (res <= 0)
			break;

		size -= res;
		total += res;
		buf += res;
	}

	return total;
}

void file_close(void *handle) {
	close((int64_t)handle);
}

void sock_api_init() {}

int sock_new(int is_ipv6) {
	return socket(is_ipv6 ? AF_INET6 : AF_INET, SOCK_STREAM, IPPROTO_TCP);
}

int sock_client_connect(int handle, int is_ipv6, int port, unsigned char *ip_addr, int ip_len) {
	struct sockaddr_in addr_ipv4 = {0};
	struct sockaddr_in6 addr_ipv6 = {0};
	struct sockaddr *addr;
	int addr_len;

	if (is_ipv6) {
		addr = (struct sockaddr *)&addr_ipv6;
		addr_len = sizeof(addr_ipv6);
		addr_ipv6.sin6_family = AF_INET6;
		addr_ipv6.sin6_port = htons(port);
		for (int i = 0; i < 16; i++)
			addr_ipv6.sin6_addr.s6_addr[i] = i < ip_len ? ip_addr[i] : 0;
	}
	else {
		addr = (struct sockaddr *)&addr_ipv4;
		addr_len = sizeof(addr_ipv4);
		addr_ipv4.sin_family = AF_INET;
		addr_ipv4.sin_port = htons(port);
		addr_ipv4.sin_addr.s_addr = *(int*)ip_addr;
	}

	return connect(handle, addr, sizeof(*addr));
}

int sock_server_obtain_client(int handle, int is_ipv6, int port, unsigned char *ip_addr, int ip_len) {
	struct sockaddr_in server_addr_ipv4 = {0};
	struct sockaddr_in6 server_addr_ipv6 = {0};
	struct sockaddr *server_addr;
	int addr_len;

	if (is_ipv6) {
		server_addr = (struct sockaddr *)&server_addr_ipv6;
		addr_len = sizeof(server_addr_ipv6);
		server_addr_ipv6.sin6_family = AF_INET6;
		server_addr_ipv6.sin6_port = htons(port);
		for (int i = 0; i < 16; i++)
			server_addr_ipv6.sin6_addr.s6_addr[i] = i < ip_len ? ip_addr[i] : 0;
	}
	else {
		server_addr = (struct sockaddr *)&server_addr_ipv4;
		addr_len = sizeof(server_addr_ipv4);
		server_addr_ipv4.sin_family = AF_INET;
		server_addr_ipv4.sin_port = htons(port);
		server_addr_ipv4.sin_addr.s_addr = *(int*)ip_addr;
	}

	int res = bind(handle, server_addr, addr_len);
	if (res < 0) {
		printf("bind() failed\n");
		return res;
	}

	res = listen(handle, 1);
	if (res < 0) {
		printf("listen() failed\n");
		return res;
	}

	struct sockaddr_in6 dummy = {0}; // sizeof(sockaddr_in6) > sizeof(sockaddr_in)
	int dummy_len = addr_len;
	res = accept(handle, (struct sockaddr *)&dummy, &dummy_len);
	if (res < 0) {
		printf("accept() failed\n");
	}

	return res;
}

int sock_read(int handle, void *buf, int size) {
	return read(handle, buf, size);
}

int sock_write(int handle, void *buf, int size) {
	return write(handle, buf, size);
}

void sock_close(int handle) {
	shutdown(handle, SHUT_RDWR);
	close(handle);
}

int sock_last_error() {
	return errno;
}

void sock_api_close() {}

#endif

#define CHUNK_SIZE (32 * 1024)

typedef struct {
	int port;
	int ip_len;
	char *ip_addr;
	const char *file_name;
	char *temp_buf;
	void *file_handle;
	int client;
	int server;
} Context;

void send_file(Context *ctx) {
	int64_t size = 0;
	ctx->file_handle = file_open(ctx->file_name, 0, &size);
	if (!ctx->file_handle || size < 1) {
		printf("Could not open file\n");
		return;
	}

	int is_ipv6 = ctx->ip_len > 4;
	ctx->server = sock_new(is_ipv6);

	ctx->client = sock_server_obtain_client(ctx->server, is_ipv6, ctx->port, ctx->ip_addr, ctx->ip_len);
	if (ctx->client <= 0) {
		printf("Failed to obtain a client (last error: %d)\n", sock_last_error());
		return;
	}

	ctx->temp_buf = malloc(CHUNK_SIZE);
	int64_t left = size;
	int count = 0;

	while (left > 0) {
		int chunk = left < CHUNK_SIZE ? left : CHUNK_SIZE;
		int retrieved = file_read(ctx->file_handle, ctx->temp_buf, chunk);
		if (retrieved <= 0) {
			printf("send_file() retrieved=%d, chunk=%d\n", retrieved, chunk);
			break;
		}

		int info = retrieved < left ? retrieved : -retrieved;
		sock_write(ctx->client, &info, sizeof(int));

		unsigned char ack;
		sock_read(ctx->client, &ack, 1);
		if (ack != 0xaa)
			break;

		int written = 0;
		while (written < retrieved) {
			int res = sock_write(ctx->client, &ctx->temp_buf[written], retrieved - written);
			if (res <= 0) {
				printf("sock_write res=%d\n", res);
				break;
			}
			written += res;
		}
		if (written < retrieved)
			break;

		sock_read(ctx->client, &ack, 1);
		if (ack != 0xaa)
			break;

		left -= retrieved;
	}

	printf("Wrote %ld/%ld bytes\n", size - left, size);
}

void recv_file(Context *ctx) {
	int is_ipv6 = ctx->ip_len > 4;

	ctx->client = sock_new(is_ipv6);
	if (sock_client_connect(ctx->client, is_ipv6, ctx->port, ctx->ip_addr, ctx->ip_len) < 0) {
		printf("Failed to connect to server (last error: %d)\n", sock_last_error());
		return;
	}

	ctx->file_handle = file_open(ctx->file_name, 1, NULL);
	if (!ctx->file_handle) {
		unsigned char fail = 0xcc;
		sock_write(ctx->client, &fail, 1);
		printf("Could not create file\n");
		return;
	}

	ctx->temp_buf = malloc(CHUNK_SIZE);

	int64_t total = 0;
	int count = 0;

	while (1) {
		int info = 0;
		sock_read(ctx->client, &info, sizeof(int));
		if (!info) {
			char fail = 0xdd;
			sock_write(ctx->client, &fail, 1);
			break;
		}

		char ack = 0xaa;
		sock_write(ctx->client, &ack, 1);

		int chunk = info < 0 ? -info : info;
		int retrieved = 0;
		while (retrieved < chunk) {
			int res = sock_read(ctx->client, &ctx->temp_buf[retrieved], chunk - retrieved);
			if (res <= 0) {
				printf("recv_file() retrieved=%d, error=%d\n", retrieved, sock_last_error());
				char fail = 0xcc;
				sock_write(ctx->client, &fail, 1);
				break;
			}
			retrieved += res;
		}

		file_write(ctx->file_handle, ctx->temp_buf, retrieved);

		ack = 0xaa;
		sock_write(ctx->client, &ack, 1);

		total += retrieved;

		if (info < 0)
			break;
	}

	printf("Read %ld bytes\n", total);
}

void cleanup(Context *ctx) {
	if (ctx->temp_buf) {
		free(ctx->temp_buf);
		ctx->temp_buf = NULL;
	}
	if (ctx->file_handle) {
		file_close(ctx->file_handle);
		ctx->file_handle = NULL;
	}
	if (ctx->client > 0) {
		sock_close(ctx->client);
		ctx->client = 0;
	}
	if (ctx->server > 0) {
		sock_close(ctx->server);
		ctx->server = 0;
	}
}

int main(int argc, char **argv) {
	if (argc < 4) {
		printf("File sender/receiver\n"
			"Usage: %s <send | recv> <file name> <port> [ip address]\n"
			"First run the program on the sending side, then start the receiving side.\n", argv[0]);
		return 1;
	}

	unsigned int cmd = 0;
	int cmd_len = strlen(argv[1]);
	for (int i = 0; i < 4 && i < cmd_len; i++) {
		char c = argv[1][i];
		c += (c >= 'A' && c <= 'Z') * 0x20;
		cmd = (cmd << 8) | (c & 0xffU);
	}

	int mode = 0;
	if (cmd == 0x73656e64)
		mode = SEND;
	else if (cmd == 0x72656376)
		mode = RECV;

	if (!mode) {
		printf("Unrecognised mode \"%s\"\n", argv[1]);
		return 2;
	}

	int port = atoi(argv[3]);

	unsigned char ip[16];
	int ip_len = 0;

	if (argc > 4) {
		ip_len = parse_ip_address(ip, argv[4], 0);
	}
	else {
		*(int*)ip = 0;
		ip_len = 4;
	}

	if (ip_len != 4 && ip_len != 16) {
		printf("\"%s\" does not appear to be a valid IPv4 or IPv6 address\n", argv[4]);
		return 3;
	}

	Context ctx = {0};
	ctx.file_name = argv[2];
	ctx.port = port;
	ctx.ip_addr = &ip[0];
	ctx.ip_len = ip_len;

	sock_api_init();

	if (mode == SEND)
		send_file(&ctx);
	else
		recv_file(&ctx);

	cleanup(&ctx);
	sock_api_close();

	return 0;
}
