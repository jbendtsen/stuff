#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <unistd.h>
#include <fcntl.h>
#include <dirent.h>

#include <gccore.h>
#include <wiiuse/wpad.h>

#include <sdcard/wiisd_io.h>
#include <ogc/usbstorage.h>
#include <fat.h>

#define DARK_GREY 0x40804080

#define FS_POOL_SIZE (1024 * 1024)

#define FS_NOT_EXIST  -2
#define FS_NOT_LOADED  -1

#define FS_FLAG_DIR       1
#define FS_FLAG_OPENED    2
#define FS_FLAG_SELECTED  4

typedef struct {
	int parent;
	int prev;
	int next;
	int name_len;
	u32 flags;
	int first_dir;
	int first_file;
} Directory;

typedef struct {
	int parent;
	int prev;
	int next;
	int name_len;
	u32 flags;
	u32 size;
} File;

typedef struct {
	char *pool;
	int head;
	int top_idx;
	int hl_idx;
} FS;

static FS sd_fs = {0};
static FS usb_fs = {0};

static FS *fs_hl = NULL;

typedef struct {
	int x, y, w, h;
} Rect;

#define INPUT_ANY(type)    (wpad_##type | pad_##type)
#define INPUT_HOME(type)   ((wpad_##type & WPAD_BUTTON_HOME)  | (pad_##type & PAD_BUTTON_MENU))
#define INPUT_ACCEPT(type) ((wpad_##type & WPAD_BUTTON_A)     | (pad_##type & PAD_BUTTON_A))
#define INPUT_BACK(type)   ((wpad_##type & WPAD_BUTTON_B)     | (pad_##type & PAD_BUTTON_B))
#define INPUT_UP(type)     ((wpad_##type & WPAD_BUTTON_UP)    | (pad_##type & PAD_BUTTON_UP))
#define INPUT_DOWN(type)   ((wpad_##type & WPAD_BUTTON_DOWN)  | (pad_##type & PAD_BUTTON_DOWN))
#define INPUT_LEFT(type)   ((wpad_##type & WPAD_BUTTON_LEFT)  | (pad_##type & PAD_BUTTON_LEFT))
#define INPUT_RIGHT(type)  ((wpad_##type & WPAD_BUTTON_RIGHT) | (pad_##type & PAD_BUTTON_RIGHT))
#define INPUT_NEXT(type)   ((wpad_##type & WPAD_BUTTON_PLUS)  | (pad_##type & PAD_TRIGGER_R))
#define INPUT_PREV(type)   ((wpad_##type & WPAD_BUTTON_MINUS) | (pad_##type & PAD_TRIGGER_L))
#define INPUT_X(type)      ((wpad_##type & WPAD_BUTTON_2)     | (pad_##type & PAD_BUTTON_X))
#define INPUT_Y(type)      ((wpad_##type & WPAD_BUTTON_1)     | (pad_##type & PAD_BUTTON_Y))

#define ALLOW_TIMER_ACTION(timer) ((timer) == 1 || ((timer) > 20 && (timer) % 10 == 1))

static u32 wpad_down = 0;
static u32 wpad_held = 0;
static u32 pad_down = 0;
static u32 pad_held = 0;

static u32 *xfb = NULL;
static GXRModeObj *rmode = NULL;

void video_init() {
	VIDEO_Init();
	rmode = VIDEO_GetPreferredMode(NULL);
	xfb = MEM_K0_TO_K1(SYS_AllocateFramebuffer(rmode));
	console_init(xfb, 20, 20, rmode->fbWidth, rmode->xfbHeight, rmode->fbWidth * VI_DISPLAY_PIX_SZ);
	VIDEO_Configure(rmode);
	VIDEO_SetNextFramebuffer(xfb);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
}

void poll_input(void) {
	WPAD_ScanPads();
	PAD_ScanPads();

	wpad_down = WPAD_ButtonsDown(0);
	wpad_held = WPAD_ButtonsHeld(0);
	pad_down = PAD_ButtonsDown(0);
	pad_held = PAD_ButtonsHeld(0);
}

Rect make_rect_from_text_grid(int row, int col, int n_rows, int n_cols) {
	if (row < 0) {
		n_rows += row;
		row = 0;
	}
	if (col < 0) {
		n_cols += col;
		col = 0;
	}
	if (n_rows <= 0 || row >= 30 || n_cols <= 0 || col >= 80)
		return (Rect){0};

	if (row + n_rows > 30)
		n_rows = 30 - row;
	if (col + n_cols > 80)
		n_cols = 80 - col;

	Rect r = (Rect){
		.x = ((col * 8) - 2) / 2,
		.y = (row * 16) - 2,
		.w = (n_cols * 4) + 2,
		.h = (n_rows * 16) + 4
	};

	if (r.x < 0) r.x = 0;
	if (r.y < 0) r.y = 0;
	if (r.x+r.w >= 320) r.w = 320-r.x;
	if (r.y+r.h >= 480) r.h = 480-r.y;

	return r;
}

void fill_text_area(int row, int col, int n_rows, int n_cols, u32 color) {
	Rect box = make_rect_from_text_grid(row, col, n_rows, n_cols);
	if (!box.w || !box.h)
		return;

	for (int i = 0; i < box.h; i++) {
		int off = (box.y + i) * 320 + box.x;
		for (int j = 0; j < box.w; j++)
			xfb[off+j] = color;
	}
}

void tint_text_color(int row, int col, int n_rows, int n_cols, u32 new_color, u32 inv_mask) {
	Rect box = make_rect_from_text_grid(row, col, n_rows, n_cols);
	if (!box.w || !box.h)
		return;

	inv_mask = inv_mask ? 0xff : 0;

	u32 new_y = new_color >> 24;
	u32 new_u = (new_color >> 16) & 0xff;
	u32 new_v = new_color & 0xff;

	for (int i = 0; i < box.h; i++) {
		int off = (box.y + i) * 320 + box.x;
		for (int j = 0; j < box.w; j++) {
			u32 color = xfb[off+j];
			u32 lum = (color >> 24) ^ inv_mask;
			u32 y = new_y * lum;
			u32 u = new_u * lum;
			u32 v = new_v * lum;
			lum ^= 0xff;
			y += (color >> 24) * lum;
			u += ((color >> 16) & 0xff) * lum;
			v += (color & 0xff) * lum;
			color = y & 0xff00;
			color |= color << 16;
			color |= (u & 0xff00) << 8;
			color |= (v & 0xff00) >> 8;
			xfb[off+j] = color;
		}
	}
}

int write_full_path(char *path, char *pool, int offset, int struct_size) {
	int len = 0;
	while (offset >= 0) {
		if (len > 0)
			path[len++] = '/';

		// This works for both files and directories since the offsets of
		//  'parent', 'next' and 'name_len' in the File struct
		//  are the same as in the Directory struct
		Directory *dir = (Directory*)&pool[offset];
		char *name = &pool[offset + struct_size];
		char *p = &name[dir->name_len];

		while (p > name) {
			p--;
			path[len++] = *p;
		}

		struct_size = sizeof(Directory);
		offset = dir->parent;
	}

	for (int i = 0; i < len/2; i++) {
		char c = path[i];
		path[i] = path[len-i-1];
		path[len-i-1] = c;
	}

	path[len] = 0;
	return len;
}

void enumerate_directory(FS *fs, int offset) {
	char path[512];
	write_full_path(path, fs->pool, offset, sizeof(Directory));

	Directory *parent = (Directory*)&fs->pool[offset];
	parent->first_dir = FS_NOT_EXIST;
	parent->first_file = FS_NOT_EXIST;

	DIR *d = opendir(path);
	if (!d) {
		printf("\x1b[3;1HCould not open directory \"%s\"", path);
		return;
	}

	struct dirent *e = NULL;
	while ((e = readdir(d))) {
		char *name = e->d_name;
		if (name[0] == 0 ||
			(name[0] == '.' && name[1] == 0) ||
			(name[0] == '.' && name[1] == '.' && name[2] == 0)
		) {
			continue;
		}

		int name_len = strlen(name);
		if (fs->head + sizeof(Directory) + name_len + 3 > FS_POOL_SIZE) {
			printf("\x1b[3;1HExceeded filesystem buffer limit");
			return;
		}

		u32 flags = 0;
		int struct_size = 0;
		int *pos = NULL;
		int prev = FS_NOT_EXIST;

		if (e->d_type == DT_DIR) {
			flags = FS_FLAG_DIR;
			struct_size = sizeof(Directory);
			pos = &parent->first_dir;
		}
		else {
			struct_size = sizeof(File);
			pos = &parent->first_file;
		}

		while (*pos > 0) {
			prev = *pos;
			pos = &((Directory*)&fs->pool[*pos])->next;
		}

		*pos = fs->head;
		Directory *entry = (Directory*)&fs->pool[*pos];
		entry->parent = offset;
		entry->prev = prev;
		entry->next = FS_NOT_EXIST;
		entry->name_len = name_len;
		entry->flags = flags;

		if (flags & FS_FLAG_DIR) {
			entry->first_dir = FS_NOT_LOADED;
			entry->first_file = FS_NOT_LOADED;
		}
		else {
			((File*)entry)->size = 0;
		}

		memcpy(&fs->pool[*pos + struct_size], name, name_len);
		fs->head = (*pos + struct_size + name_len + 3) & ~3;
	}

	closedir(d);
}

void mask_all_entries(FS *fs, u32 mask) {
	mask |= FS_FLAG_DIR;
	int idx = 0;
	while (idx < fs->head) {
		Directory *entry = (Directory*)&fs->pool[idx];
		entry->flags &= mask;
		int struct_size = (entry->flags & FS_FLAG_DIR) ? sizeof(Directory) : sizeof(File);
		idx += (entry->name_len + struct_size + 3) & ~3;
	}
}

void fs_find_previous(FS *fs, int *pos) {
	if (*pos <= 0) {
		*pos = 0;
		return;
	}

	Directory *entry = (Directory*)&fs->pool[*pos];
	int idx = entry->prev;

	if (idx < 0) {
		int prev = FS_NOT_EXIST;
		if ((entry->flags & FS_FLAG_DIR) == 0) {
			idx = ((Directory*)&fs->pool[entry->parent])->first_dir;
			while (idx >= 0) {
				prev = idx;
				idx = ((Directory*)&fs->pool[idx])->next;
			}
		}

		if (prev < 0) {
			*pos = entry->parent < 0 ? 0 : entry->parent;
			return;
		}

		idx = prev;
	}

	if (idx >= 0) {
		int prev = idx;
		while (prev >= 0) {
			idx = prev;
			entry = (Directory*)&fs->pool[idx];

			u32 traverse_flags = FS_FLAG_DIR | FS_FLAG_OPENED;
			if ((entry->flags & traverse_flags) != traverse_flags)
				break;

			prev = entry->first_file;
			if (prev < 0) prev = entry->first_dir;

			int next = prev;
			while (next >= 0) {
				prev = next;
				next = ((Directory*)&fs->pool[next])->next;
			}
		}
	}

	*pos = idx < 0 ? 0 : idx;
}

bool fs_find_next(FS *fs, int *pos, int *levels) {
	if (*pos < 0) {
		*pos = 0;
		if (levels) *levels = 0;
		return false;
	}

	int idx = FS_NOT_EXIST;

	Directory *entry = (Directory*)&fs->pool[*pos];
	u32 traverse_flags = FS_FLAG_DIR | FS_FLAG_OPENED;

	if ((entry->flags & traverse_flags) == traverse_flags) {
		idx = entry->first_dir;
		if (idx < 0) idx = entry->first_file;

		if (levels && idx >= 0)
			*levels += 1;
	}

	if (idx < 0)
		idx = entry->next;

	while (idx < 0 && entry->parent >= 0) {
		Directory *parent = (Directory*)&fs->pool[entry->parent];

		if (entry->flags & FS_FLAG_DIR)
			idx = parent->first_file;

		if (idx < 0) {
			idx = parent->next;
			if (levels)
				*levels = *levels > 0 ? *levels - 1 : 0;
		}

		entry = parent;
	}

	*pos = idx < 0 ? 0 : idx;
	if (levels && *pos == 0) *levels = 0;

	return idx >= 0;
}

void copy_allowed_characters(char *dst, char *src, int len, int max_len) {
	char *p = dst;
	char *end = &dst[max_len-1];

	for (int i = 0; i < len && p < end; i++) {
		char c = src[i];
		if (c == ' ' || c == '(' || c == ')' || c == '+' ||
			c == ',' || c == '-' || c == '.' ||
			(c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
			c == '[' || c == ']' || c == '_' ||
			(c >= 'a' && c <= 'z')
		) {
			*p++ = c;
		}
	}

	*p = 0;
}

int check_for_conflicting_name(FS *dst_fs, char *src_name_buf, char *dst_name_buf, int conflict_idx)
{
	while (conflict_idx >= 0) {
		Directory *dst_entry = (Directory*)&dst_fs->pool[conflict_idx];
		int dst_struct_size = (dst_entry->flags & FS_FLAG_DIR) ? sizeof(Directory) : sizeof(File);
		copy_allowed_characters(dst_name_buf, ((char*)dst_entry) + dst_struct_size, dst_entry->name_len, 0x100);

		if (!strcmp(src_name_buf, dst_name_buf))
			break;

		conflict_idx = dst_entry->next;
	}

	return conflict_idx;
}

void transfer_selected_item(FS *src_fs, FS *dst_fs, u8 *copy_buf, char *path_buf, int path_end, Directory *src_entry, int dst_dir_idx)
{
	char *src_name_buf = &path_buf[0x200];
	char *dst_name_buf = &path_buf[0x300];

	int src_struct_size = (src_entry->flags & FS_FLAG_DIR) ? sizeof(Directory) : sizeof(File);
	copy_allowed_characters(src_name_buf, ((char*)src_entry) + src_struct_size, src_entry->name_len, 0x100);

	Directory *dst_dir = (Directory*)&dst_fs->pool[dst_dir_idx];
	if (dst_dir->first_dir == FS_NOT_LOADED || dst_dir->first_file == FS_NOT_LOADED)
		enumerate_directory(dst_fs, dst_dir_idx);

	int conflict_idx = (src_entry->flags & FS_FLAG_DIR) ? dst_dir->first_file : dst_dir->first_dir;
	conflict_idx = check_for_conflicting_name(dst_fs, src_name_buf, dst_name_buf, conflict_idx);

	if (conflict_idx >= 0)
		return;

	printf("\x1b[2;1HTransferring to %s...", dst_fs == &sd_fs ? "SD Card" : "USB Storage");

	int dst_idx = (src_entry->flags & FS_FLAG_DIR) ? dst_dir->first_dir : dst_dir->first_file;
	dst_idx = check_for_conflicting_name(dst_fs, src_name_buf, dst_name_buf, dst_idx);

	char *p = path_buf + path_end;
	{
		char *in = src_name_buf;
		while (*in) *p++ = *in++;
		*p = 0;
	}

	if (dst_idx < 0) {
		int prev = FS_NOT_EXIST;
		int *origin = (src_entry->flags & FS_FLAG_DIR) ? &dst_dir->first_dir : &dst_dir->first_file;
		int *pos = origin;

		while (*pos >= 0) {
			prev = *pos;
			pos = &((Directory*)&dst_fs->pool[prev])->next;
		}

		int name_len = strlen(src_name_buf);
		int next_head = (dst_fs->head + src_struct_size + name_len + 3) & ~3;

		if (next_head > FS_POOL_SIZE) {
			fill_text_area(3, 1, 78, 1, COLOR_BLACK);
			printf("\x1b[3;1HExceeded filesystem buffer limit");
			return;
		}

		if (prev >= 0)
			((Directory*)&dst_fs->pool[prev])->next = dst_fs->head;
		else
			*origin = dst_fs->head;

		Directory *dst_entry = (Directory*)&dst_fs->pool[dst_fs->head];
		dst_entry->parent = dst_dir_idx;
		dst_entry->prev = prev;
		dst_entry->next = FS_NOT_EXIST;
		dst_entry->name_len = name_len;
		dst_entry->flags = src_entry->flags & FS_FLAG_DIR;

		if (src_entry->flags & FS_FLAG_DIR) {
			dst_entry->first_dir = FS_NOT_EXIST;
			dst_entry->first_file = FS_NOT_EXIST;
		}
		else {
			((File*)dst_entry)->size = 0;
		}

		memcpy(((char*)dst_entry) + src_struct_size, src_name_buf, name_len);

		dst_idx = dst_fs->head;
		dst_fs->head = next_head;
	}

	if (src_entry->flags & FS_FLAG_DIR) {
		// stat(path_buf) to determine if mkdir(path_buf) is required
		struct stat st;
		if (stat(path_buf, &st) == -1) {
			mkdir(path_buf, 0777);
		}

		*p++ = '/';
		*p = 0;
		path_end = (int)(p - path_buf);

		if (src_entry->first_dir == FS_NOT_LOADED || src_entry->first_file == FS_NOT_LOADED)
			enumerate_directory(src_fs, (int)((char*)src_entry - src_fs->pool));

		int idx = src_entry->first_dir;
		while (idx >= 0) {
			Directory *dir = (Directory*)&src_fs->pool[idx];
			transfer_selected_item(src_fs, dst_fs, copy_buf, path_buf, path_end, dir, dst_idx);
			idx = dir->next;
		}
		idx = src_entry->first_file;
		while (idx >= 0) {
			Directory *dir = (Directory*)&src_fs->pool[idx];
			transfer_selected_item(src_fs, dst_fs, copy_buf, path_buf, path_end, dir, dst_idx);
			idx = dir->next;
		}
	}
	else {
		fill_text_area(22, 1, 78, 3, COLOR_BLACK);
		printf("\x1b[23;1H%s", path_buf);

		char *src_path = src_name_buf; // safe to overwrite now
		write_full_path(src_path, src_fs->pool, (int)((char*)src_entry - src_fs->pool), sizeof(File));

		int read_fd = open(src_path, O_RDONLY);
		if (read_fd < 0) {
			fill_text_area(3, 1, 78, 1, COLOR_BLACK);
			printf("\x1b[3;1HCould not open file from source");
			return;
		}

		int write_fd = open(path_buf, O_CREAT | O_RDWR);
		if (write_fd < 0) {
			close(read_fd);

			fill_text_area(3, 1, 78, 1, COLOR_BLACK);
			printf("\x1b[3;1HCould not open file in destination");
			return;
		}

		while (true) {
			int sz = read(read_fd, copy_buf, 0x200);
			if (sz <= 0)
				break;

			write(write_fd, copy_buf, sz);
		}

		close(read_fd);
		close(write_fd);
	}
}

void transfer_selected(FS *src_fs, FS *dst_fs) {
	char path_buf[0x400];
	u8 copy_buf[0x200];

	int dst_dir_idx = dst_fs->hl_idx;
	if (dst_dir_idx >= 0) {
		Directory *dst_dir = (Directory*)&dst_fs->pool[dst_dir_idx];
		if ((dst_dir->flags & FS_FLAG_DIR) == 0)
			dst_dir_idx = dst_dir->parent < 0 ? 0 : dst_dir->parent;
	}
	else {
		dst_dir_idx = 0;
	}

	int path_len = write_full_path(path_buf, dst_fs->pool, dst_dir_idx, sizeof(Directory));
	path_buf[path_len++] = '/';

	int src_idx = 0;
	while (src_idx < src_fs->head) {
		Directory *src_entry = (Directory*)&src_fs->pool[src_idx];

		if (src_entry->flags & FS_FLAG_SELECTED) {
			transfer_selected_item(src_fs, dst_fs, copy_buf, path_buf, path_len, src_entry, dst_dir_idx);
			src_entry->flags &= ~FS_FLAG_SELECTED;
		}

		int src_struct_size = (src_entry->flags & FS_FLAG_DIR) ? sizeof(Directory) : sizeof(File);
		src_idx += (src_struct_size + src_entry->name_len + 3) & ~3;
	}
}

bool render_device_window(FS *fs, int column) {
	int hl_row = -1;

	char line[48];
	int idx = fs->top_idx;

	int levels = 0;
	{
		Directory *entry = (Directory*)&fs->pool[idx];
		while (entry->parent >= 0) {
			levels++;
			entry = (Directory*)&fs->pool[entry->parent];
		}
	}

	for (int i = 0; i < 16; i++) {
		int row = i + 5;
		if (idx == fs->hl_idx)
			hl_row = row;

		memset(line, 0, 48);
		int start = sprintf(line, "\x1b[%d;%dH", row, column);
		char *p = &line[start];

		for (int j = 0; j < levels && j < 8; j++) {
			*p++ = ' ';
			*p++ = ' ';
		}

		Directory *entry = (Directory*)&fs->pool[idx];
		int struct_size = 0;
		if (entry->flags & FS_FLAG_DIR) {
			*p++ = (entry->flags & FS_FLAG_OPENED) ? '-' : '+';
			struct_size = sizeof(Directory);
		}
		else {
			*p++ = ' ';
			struct_size = sizeof(File);
		}

		*p++ = ' ';
		int off = p - &line[start];
		int len = 39 - off;
		if (len > entry->name_len)
			len = entry->name_len;

		char *name = ((char*)entry) + struct_size;
		memcpy(p, name, len);

		puts(line);

		if (entry->flags & FS_FLAG_SELECTED)
			tint_text_color(row, column, 1, 38, COLOR_LIME, false);

		if (!fs_find_next(fs, &idx, &levels))
			break;

		if (levels < 0)
			levels = 0;

		if (idx < 0)
			break;
	}

	if (hl_row >= 0) {
		u32 colour = fs == fs_hl ? COLOR_BLUE : DARK_GREY;
		tint_text_color(hl_row, column, 1, 38, colour, true);
		return true;
	}

	return false;
}

bool render_windows() {
	bool res1 = render_device_window(&sd_fs, 1);
	bool res2 = render_device_window(&usb_fs, 40);
	return (res1 && fs_hl == &sd_fs) || (res2 && fs_hl == &usb_fs);
}

int fs_init() {
	if (!fatMountSimple("sd", &__io_wiisd)) {
		printf("\x1b[3;1HFailed to load SD card. Exiting...");
		return -1;
	}
	if (!fatMountSimple("usb", &__io_usbstorage)) {
		printf("\x1b[3;1HFailed to load USB storage. Exiting...");
		return -2;
	}

	sd_fs.pool = malloc(FS_POOL_SIZE);
	usb_fs.pool = malloc(FS_POOL_SIZE);

	if (!sd_fs.pool || !usb_fs.pool) {
		printf("\x1b[3;1HFailed to allocate space for filesystems. Exiting...");
		return -3;
	}

	*(Directory*)sd_fs.pool = (Directory){
		.parent = FS_NOT_EXIST,
		.prev = FS_NOT_EXIST,
		.next = FS_NOT_EXIST,
		.name_len = 3,
		.flags = FS_FLAG_DIR | FS_FLAG_OPENED,
		.first_dir = FS_NOT_LOADED,
		.first_file = FS_NOT_LOADED
	};
	memcpy(sd_fs.pool + sizeof(Directory), "sd:", 3);

	*(Directory*)usb_fs.pool = (Directory){
		.parent = FS_NOT_EXIST,
		.prev = FS_NOT_EXIST,
		.next = FS_NOT_EXIST,
		.name_len = 4,
		.flags = FS_FLAG_DIR | FS_FLAG_OPENED,
		.first_dir = FS_NOT_LOADED,
		.first_file = FS_NOT_LOADED
	};
	memcpy(usb_fs.pool + sizeof(Directory), "usb:", 4);

	sd_fs.head = sizeof(Directory) + 4;
	usb_fs.head = sizeof(Directory) + 4;

	enumerate_directory(&sd_fs, 0);
	enumerate_directory(&usb_fs, 0);

	return 0;
}

int main() {
	video_init();
	VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);

	printf("\x1b[1;1HFile Copier");
	if (fs_init() < 0)
		return 1;

	fs_hl = &sd_fs;

	render_windows();

	WPAD_Init();
	PAD_Init();

	bool was_move_down = false;
	bool was_move_up = false;
	bool cursor_onscreen = true;
	u32 held_timer = 0;

	while (1) {
		VIDEO_WaitVSync();
		poll_input();

		if (INPUT_HOME(down))
			break;

		if (INPUT_ANY(held))
			held_timer++;
		else
			held_timer = 0;

		if (INPUT_ACCEPT(down)) {
			Directory *entry = (Directory*)&fs_hl->pool[fs_hl->hl_idx];
			if (entry->flags & FS_FLAG_DIR) {
				entry->flags ^= FS_FLAG_OPENED;
				if (entry->flags & FS_FLAG_OPENED) {
					if (entry->first_dir == FS_NOT_LOADED || entry->first_file == FS_NOT_LOADED)
						enumerate_directory(fs_hl, fs_hl->hl_idx);
				}
			}
		}

		if (INPUT_BACK(down)) {
			mask_all_entries(&sd_fs, ~FS_FLAG_SELECTED);
			mask_all_entries(&usb_fs, ~FS_FLAG_SELECTED);
		}

		if (INPUT_Y(down)) {
			Directory *entry = (Directory*)&fs_hl->pool[fs_hl->hl_idx];
			entry->flags ^= FS_FLAG_SELECTED;
		}
		if (INPUT_X(down)) {
			FS *fs_src = fs_hl == &sd_fs ? &usb_fs : &sd_fs;
			transfer_selected(fs_src, fs_hl);
		}

		if (INPUT_NEXT(down)) {
			fs_hl = &usb_fs;
		}
		else if (INPUT_PREV(down)) {
			fs_hl = &sd_fs;
		}

		if (was_move_up && !cursor_onscreen)
			fs_find_previous(fs_hl, &fs_hl->top_idx);

		if (INPUT_UP(held) && ALLOW_TIMER_ACTION(held_timer)) {
			fs_find_previous(fs_hl, &fs_hl->hl_idx);
			was_move_up = true;
		}
		else {
			was_move_up = false;
		}

		if (was_move_down && !cursor_onscreen)
			fs_find_next(fs_hl, &fs_hl->top_idx, NULL);

		if (INPUT_DOWN(held) && ALLOW_TIMER_ACTION(held_timer)) {
			int hl = fs_hl->hl_idx;
			if (!fs_find_next(fs_hl, &fs_hl->hl_idx, NULL))
				fs_hl->hl_idx = hl;

			was_move_down = true;
		}
		else {
			was_move_down = false;
		}

		if (INPUT_ANY(down) || was_move_down || was_move_up || !cursor_onscreen) {
			VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
			printf("\x1b[1;1HFile Copier");
			cursor_onscreen = render_windows();
		}
	}

	printf("\x1b[2;1HExiting...");
	return 0;
}
