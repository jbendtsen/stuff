#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>
#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>

// dd, but better

#define TYPE_UNKNOWN  0
#define TYPE_FD       1
#define TYPE_REG      2
#define TYPE_BLOCK    3
#define TYPE_CHR      4
#define TYPE_FIFO     5
#define TYPE_SOCKET   6

static const char * const type_strings[] = {
    "unknown   ",
    "channel   ",
    "file      ",
    "block-dev ",
    "char-dev  ",
    "pipe      ",
    "socket    "
};
static const char * const rw_strings[] = {"reading", "writing"};

typedef struct {
    int fd;
    int type;
    char *name;
    int64_t size;
} Stream;

typedef struct {
    Stream *streams;
    int n_streams;
    int finished_fd;
    int64_t chunksize;
    int64_t chunkcount;
    int64_t totalsize;
    bool nosync;
    bool noprogress;
    bool noconfirm;
    bool noautochunk;
} Settings;

char *get_string_element(char **args, int count, int idx);
int64_t get_int64_element_or(char **args, int count, int idx, int64_t sentinel);
int64_t get_size_element(char **args, int count, int idx);

void print_stream_info(Stream *s, FILE *output);
void *copy_data(void *args);

static _Atomic int64_t bytes_read = 0;
static _Atomic int64_t bytes_written = 0;
static _Atomic int64_t total_bytes = 0;
static _Atomic int64_t cancelled = 0;

void print_help(FILE *output)
{
    fprintf(
        output,
        "Usage: chunker <options>\n"
        "Options:\n"
        "  -i <input file or device>\n"
        "     Overrides -ifd\n"
        "  -o <output file or device>\n"
        "     Adds an output file\n"
        "  -ifd <input file descriptor integer>\n"
        "     Overrides -i\n"
        "  -ofd <output file descriptor integer>\n"
        "     Adds an output descriptor\n"
        "  -chunksize <size specifier>\n"
        "     Size of each chunk to be copied\n"
        "  -chunkcount <count>\n"
        "     Number of chunks to copy. Requires -chunksize, and not -noautochunk\n"
        "  -totalsize <size specifier>\n"
        "     Overrides -chunkcount\n"
        "  -nosync\n"
        "     Don't sync after each copy\n"
        "  -noprogress\n"
        "     Don't print the current progress\n"
        "  -noconfirm\n"
        "     Don't show input and output metadata and prompt before copying\n"
        "  -noautochunk\n"
        "     When not using -chunksize, don't grow the chunk size after successive chunks\n"
    );
}

int main(int argc, char **argv)
{
    Settings s = {0};
    s.streams = calloc((argc / 2) + 1, sizeof(Stream));
    bool any_input = false;

    int console_outputs = 0;
    FILE *pf = stdout;

    for (int i = 1; i < argc; i++) {
        bool invalid = false;
        if (!strcmp(argv[i], "-i")) {
            s.streams[0].name = get_string_element(argv, argc, i+1);
            invalid = s.streams[0].name == NULL;
            if (!invalid)
                any_input = true;
        }
        else if (!strcmp(argv[i], "-o")) {
            char *output_file = get_string_element(argv, argc, i+1);
            invalid = output_file == NULL;
            if (!invalid)
                s.streams[1 + s.n_streams++].name = output_file;
        }
        else if (!strcmp(argv[i], "-ifd")) {
            int64_t fd = get_int64_element_or(argv, argc, i+1, -1);
            invalid = fd == -1;
            if (!invalid) {
                any_input = true;
                s.streams[0].name = (char*)(fd + 1LL);
            }
        }
        else if (!strcmp(argv[i], "-ofd")) {
            int64_t fd = get_int64_element_or(argv, argc, i+1, -1);
            invalid = fd == -1;
            if (!invalid) {
                s.streams[1 + s.n_streams++].name = (char*)(fd + 1LL);
                if (fd == STDOUT_FILENO) {
                    pf = stderr;
                    console_outputs |= 1;
                }
                if (fd == STDERR_FILENO) {
                    console_outputs |= 2;
                }
            }
        }
        else if (!strcmp(argv[i], "-chunksize")) {
            s.chunksize = get_size_element(argv, argc, i+1);
            invalid = s.chunksize <= 0;
        }
        else if (!strcmp(argv[i], "-chunkcount")) {
            s.chunkcount = get_int64_element_or(argv, argc, i+1, -1);
            invalid = s.chunkcount <= 0;
        }
        else if (!strcmp(argv[i], "-totalsize")) {
            s.totalsize = get_size_element(argv, argc, i+1);
            invalid = s.totalsize <= 0;
        }
        else if (!strcmp(argv[i], "-nosync")) {
            s.nosync = true;
            i--;
        }
        else if (!strcmp(argv[i], "-noprogress")) {
            s.noprogress = true;
            i--;
        }
        else if (!strcmp(argv[i], "-noconfirm")) {
            s.noconfirm = true;
            i--;
        }
        else if (!strcmp(argv[i], "-noautochunk")) {
            s.noautochunk = true;
            i--;
        }
        else {
            i--;
        }
        if (invalid) {
            print_help(pf);
            return 1;
        }
        i++;
    }

    if (any_input)
        s.n_streams++;

    if (s.n_streams < 2) {
        print_help(pf);
        return 1;
    }

    if (console_outputs == 3 && !s.noprogress) {
        fprintf(pf, "Cannot write to stdout and stderr and show progress simultaneously, pass -noprogress instead\n");
        return 2;
    }

    if (s.totalsize > 0)
        s.chunkcount = 0;

    if (s.chunkcount > 0) {
        if (s.chunksize <= 0) {
            fprintf(pf, "If -chunkcount is used, -chunksize must also be provided\n");
            return 2;
        }
        s.totalsize = s.chunkcount * s.chunksize;
        s.noautochunk = true;
    }

    for (int i = 0; i < s.n_streams; i++) {
        if ((uint64_t)s.streams[i].name <= 3ULL) {
            int64_t fd = (int64_t)s.streams[i].name - 1LL;
            if (i == 0) {
                if (fd == STDOUT_FILENO) {
                    if (!s.noconfirm) {
                        fprintf(pf, "Cannot read from stdin and ask for user input, pass -noconfirm instead\n");
                        return 2;
                    }
                    s.streams[i].name = "(stdin)";
                }
                else if (fd == STDOUT_FILENO) {
                    fprintf(pf, "Cannot read from stdout\n");
                    return 2;
                }
                else if (fd == STDERR_FILENO) {
                    fprintf(pf, "Cannot read from stderr\n");
                    return 2;
                }
            }
            else {
                if (fd == STDIN_FILENO) {
                    fprintf(pf, "Cannot write to stdin\n");
                    return 2;
                }
                else if (fd == STDOUT_FILENO) {
                    s.streams[i].name = "(stdout)";
                }
                else if (fd == STDERR_FILENO) {
                    s.streams[i].name = "(stderr)";
                }
            }
            s.streams[i].fd = (int)fd;
            s.streams[i].type = TYPE_FD;
            s.streams[i].size = -1;
        }
        else {
            bool is_link = false;
            do {
                is_link = false;

                int flags = i == 0 ? O_RDONLY : O_RDWR;
                int fd = open(s.streams[i].name, flags);
                if (fd < 0) {
                    fprintf(pf, "Could not open \"%s\" for %s\n", s.streams[i].name, rw_strings[i > 0]);
                    return 2;
                }
                s.streams[i].fd = fd;

                struct stat st = {0};
                if (fstat(fd, &st) < 0) {
                    fprintf(pf, "Could not stat \"%s\"\n", s.streams[i].name);
                    return 2;
                }

                switch (st.st_mode & S_IFMT) {
                    case S_IFDIR:
                    {
                        fprintf(pf, "Error: \"%s\" is a directory, not a file or data stream\n", s.streams[i].name);
                        return 2;
                    }
                    case S_IFLNK:
                    {
                        is_link = true;
                        s.streams[i].name = realpath(s.streams[i].name, NULL);
                        break;
                    }
                    case S_IFREG:
                    {
                        s.streams[i].type = TYPE_REG;
                        s.streams[i].size = st.st_size;
                        break;
                    }
                    case S_IFBLK:
                    {
                        s.streams[i].type = TYPE_BLOCK;
                        uint64_t block_size;
                        int rc = ioctl(fd, BLKGETSIZE64, &block_size);
                        s.streams[i].size = (int64_t)block_size;
                        break;
                    }
                    case S_IFCHR:
                    {
                        s.streams[i].type = TYPE_CHR;
                        s.streams[i].size = -1;
                        break;
                    }
                    case S_IFIFO:
                    {
                        s.streams[i].type = TYPE_FIFO;
                        s.streams[i].size = -1;
                        break;
                    }
                    case S_IFSOCK:
                    {
                        s.streams[i].type = TYPE_SOCKET;
                        s.streams[i].size = -1;
                        break;
                    }
                    default:
                    {
                        printf("Unrecognised filesystem entry type %d\n", st.st_mode & S_IFMT);
                        break;
                    }
                }
            }
            while (is_link);
        }
    }

    if (!s.noconfirm) {
        fprintf(pf, "Copying from\n");
        print_stream_info(&s.streams[0], pf);
        fprintf(pf, "To\n");

        int n_named_outputs = 0;
        for (int i = 1; i < s.n_streams; i++) {
            print_stream_info(&s.streams[i], pf);
            if (s.streams[i].name && s.streams[i].name[0] && s.streams[i].type != TYPE_FD)
                n_named_outputs++;
        }

        if (n_named_outputs > 0) {
            if (n_named_outputs == 1)
                fprintf(pf, "\nType the name of the file to overwrite\n");
            else
                fprintf(pf, "\nType the name each file to be overwritten\n");

            char answer_buf[512];

            for (int i = 1; i < s.n_streams; i++) {
                if (!s.streams[i].name || !s.streams[i].name[0] || s.streams[i].type == TYPE_FD)
                    continue;

                int idx = strlen(s.streams[i].name) - 1;
                while (idx >= 0 && s.streams[i].name[idx] != '/')
                    idx--;
                if (idx < 0 || s.streams[i].name[idx] == '/')
                    idx++;

                fprintf(pf, "(%s)\n", &s.streams[i].name[idx]);
                fgets(answer_buf, 512, stdin);

                bool matches = false;
                for (int j = 0; j < 512; j++) {
                    char c1 = answer_buf[j];
                    char c2 = s.streams[i].name[idx+j];
                    if (c2 == 0 && (c1 == '\r' || c1 == '\n')) {
                        matches = true;
                        break;
                    }
                    if (c1 != c2)
                        break;
                }
                if (!matches)
                    return 4;
            }
        }
    }

    if (!s.noprogress) {
        int cancel_fds[2] = {0};
        if (pipe(cancel_fds) < 0) {
            fprintf(pf, "Failed to create communication pipe\n");
            return 3;
        }
        s.finished_fd = cancel_fds[1];

        struct timeval tv;
        tv.tv_sec = 1;
        tv.tv_usec = 0;

        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(cancel_fds[0], &fds);

        Settings *settings_copy = malloc(sizeof(Settings));
        memcpy(settings_copy, &s, sizeof(Settings));

        pthread_t tid;
        pthread_create(&tid, NULL, copy_data, settings_copy);

        while (true) {
            select(1, &fds, NULL, NULL, &tv);
            fprintf(pf, "\x1b[G%ld bytes transferred", bytes_written);
            if (cancelled) {
                break;
            }

            FD_ZERO(&fds);
            FD_SET(cancel_fds[0], &fds);
            tv.tv_sec = 1;
            tv.tv_usec = 0;
        }
        putchar('\n');
    }
    else {
        copy_data(&s);
        fprintf(pf, "%ld bytes transferred\n", bytes_written);
    }
}

char *get_string_element(char **args, int count, int idx)
{
    return idx >= 0 && idx < count ? args[idx] : NULL;
}

int64_t get_int64_element_or(char **args, int count, int idx, int64_t sentinel)
{
    if (idx < 0 || idx >= count || args[idx][0] == 0)
        return sentinel;

    int64_t n = 0;
    for (int i = 0; args[idx][i]; i++) {
        char c = args[idx][i];
        if (c < '0' || c > '9')
            return sentinel;
        n = n * 10LL + (int64_t)(c - '0');
    }

    return n;
}

int64_t get_size_element(char **args, int count, int idx)
{
    if (idx < 0 || idx >= count || args[idx][0] == 0)
        return -1;

    int64_t n = 0;
    char spec = 0;
    for (int i = 0; args[idx][i]; i++) {
        char c = args[idx][i];
        if (c < '0' || c > '9') {
            if (spec)
                return -1;
            spec = c;
        }
        n = n * 10LL + (int64_t)(c - '0');
    }

    switch (spec) {
        case 'k':
            n *= 1000LL;
            break;
        case 'K':
            n *= 1024LL;
            break;
        case 'm':
            n *= 1000LL * 1000LL;
            break;
        case 'M':
            n *= 1024LL * 1024LL;
            break;
        case 'g':
            n *= 1000LL * 1000LL * 1000LL;
            break;
        case 'G':
            n *= 1024LL * 1024LL * 1024LL;
            break;
        case 't':
            n *= 1000LL * 1000LL * 1000LL * 1000LL;
            break;
        case 'T':
            n *= 1024LL * 1024LL * 1024LL * 1024LL;
            break;
    }

    return n;
}

void format_size(char *size_buf, int max_len, int64_t n)
{
    if (n < 0) {
        strncpy(size_buf, "N/A    ", max_len);
        return;
    }

    int cuts = 0;
    int64_t rem = 0;
    while (cuts < 4 && n >= 1024L) {
        rem = n % 1024L;
        n /= 1024L;
        cuts++;
    }

    int off = snprintf(size_buf, max_len, "%ld", n);
    if (rem > 0)
        off += snprintf(&size_buf[off], max_len - off, ".%02d", (int)((float)rem / 10.24f));
    if (cuts > 0)
        off += snprintf(&size_buf[off], max_len - off, "%c", "KMGT"[cuts-1]);
}

void print_stream_info(Stream *s, FILE *output)
{
    char size_buf[64];
    format_size(size_buf, 64, s->size);
    fprintf(output, "%s %s\t%s\n", type_strings[s->type], size_buf, s->name);
}

void *copy_data(void *args)
{
    Settings *s = (Settings*)args;

    const int max_chunk = 16 * 1024 * 1024;
    int chunk_size = s->chunksize > 0 ? s->chunksize : 4096;
    if (chunk_size > max_chunk)
        chunk_size = max_chunk;

    char *buf = malloc(max_chunk);

    struct timespec t1 = {0}, t2 = {0};
    if (!s->noautochunk)
        clock_gettime(CLOCK_MONOTONIC, &t1);

    double current_speed = 0.0;

    int64_t total_size = s->totalsize;
    if (total_size <= 0)
        total_size = s->streams[0].size;

    int64_t offset = 0;
    while (total_size <= 0 || offset < total_size) {
        int64_t to_read = total_size > 0 ? (total_size - offset) : chunk_size;
        to_read = to_read < chunk_size ? to_read : chunk_size;

        int rsz = read(s->streams[0].fd, buf, (int)to_read);
        if (rsz <= 0)
            break;

        int n_closed_outputs = 0;
        for (int i = 1; i < s->n_streams; i++) {
            if (s->streams[i].fd == -1) {
                n_closed_outputs++;
                continue;
            }
            int w = 0;
            while (w < rsz) {
                int res = write(s->streams[i].fd, &buf[w], rsz - w);
                if (res <= 0)
                    break;
                w += res;
            }
            if (w == 0) {
                if (!s->nosync)
                    fsync(s->streams[i].fd);
                if (s->streams[i].fd > 2)
                    close(s->streams[i].fd);
                s->streams[i].fd = -1;
            }
            bytes_written += w;
        }

        if (n_closed_outputs >= s->n_streams - 1)
            break;

        if (!s->nosync) {
            for (int i = 1; i < s->n_streams; i++) {
                if (s->streams[i].fd != -1)
                    fdatasync(s->streams[i].fd);
            }
        }

        if (!s->noautochunk && chunk_size < max_chunk) {
            clock_gettime(CLOCK_MONOTONIC, &t2);
            int64_t delta = t2.tv_nsec - t1.tv_nsec + 1000000L * (t2.tv_sec - t1.tv_sec);
            double speed = (double)rsz / (double)delta;
            if (speed > current_speed) {
                int new_cs = chunk_size << 1;
                chunk_size = new_cs <= max_chunk ? new_cs : max_chunk;
                current_speed = speed;
            }
            t1 = t2;
        }
    }

    for (int i = 0; i < s->n_streams; i++) {
        if (!s->nosync && i > 0 && s->streams[i].fd != -1)
            fsync(s->streams[i].fd);
        if (s->streams[i].fd > 2)
            close(s->streams[i].fd);
    }

    cancelled = 1;
    if (s->finished_fd > 0) {
        char c = 1;
        write(s->finished_fd, &c, 1);
    }

    return NULL;
}
