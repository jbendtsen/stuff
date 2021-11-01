#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <gccore.h>
#include <wiiuse/wpad.h>
#include <ogc/wiilaunch.h>

#define VISIBLE_ROWS 8
#define VISIBLE_COLS 12

#define INPUT_ANY()    (wpad_down | pad_down)
#define INPUT_HOME()   ((wpad_down & WPAD_BUTTON_HOME)  | (pad_down & PAD_BUTTON_MENU))
#define INPUT_ACCEPT() ((wpad_down & WPAD_BUTTON_A)     | (pad_down & PAD_BUTTON_A))
#define INPUT_BACK()   ((wpad_down & WPAD_BUTTON_B)     | (pad_down & PAD_BUTTON_B))
#define INPUT_UP()     ((wpad_down & WPAD_BUTTON_UP)    | (pad_down & PAD_BUTTON_UP))
#define INPUT_DOWN()   ((wpad_down & WPAD_BUTTON_DOWN)  | (pad_down & PAD_BUTTON_DOWN))
#define INPUT_LEFT()   ((wpad_down & WPAD_BUTTON_LEFT)  | (pad_down & PAD_BUTTON_LEFT))
#define INPUT_RIGHT()  ((wpad_down & WPAD_BUTTON_RIGHT) | (pad_down & PAD_BUTTON_RIGHT))

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

void render_titles(int hl_idx, u64 *title_ids, int n_titles) {
	for (int i = 0; i < n_titles && i < VISIBLE_ROWS * VISIBLE_COLS; i++) {
		int row = 4 + (i / VISIBLE_COLS) * 2;
		int col = 1 + (i % VISIBLE_COLS) * 6;

		char *t = (char*)&title_ids[i] + 4;
		printf("\x1b[%d;%dH%c%c%c%c", row, col, t[0], t[1], t[2], t[3]);

		if (i == hl_idx) {
			int x = ((col * 8) - 2) / 2;
			int y = (row * 16) - 2;

			for (int j = 0; j < 20; j++) {
				int off = (y+j) * 320 + x;
				for (int k = 0; k < 18; k++) {
					u32 *color = &xfb[off+k];
					if (*color == COLOR_BLACK)
						*color = COLOR_BLUE;
				}
			}
		}
	}
}

int main() {
	video_init();
	VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);

	printf("\x1b[1;1HChannel Launcher");

	WII_Initialize();

	int n_titles = 0;
	ES_GetNumTitles((u32*)&n_titles);

	if (n_titles <= 0) {
		printf("\x1b[3;1HFailed to list titles. Exiting...");
		return 1;
	}

	int size = (2 * n_titles * sizeof(u64)) + 0x20;
	void *mem = calloc(size, 1);

	u64 *all_title_ids = (u64*)(((u32)mem + 0x1f) & ~0x1f);
	u64 *title_ids = &all_title_ids[n_titles];

	ES_GetTitles(all_title_ids, n_titles);
	int n_visible_ids = 0;

	for (int i = 0; i < n_titles; i++) {
		char *t = (char*)&all_title_ids[i] + 4;

		if (t[0] < ' ' || t[1] < ' ' || t[2] < ' ' || t[3] < ' ' ||
			t[0] > '~' || t[1] > '~' || t[2] > '~' || t[3] > '~')
		{
			continue;
		}

		title_ids[n_visible_ids++] = all_title_ids[i];
	}

	int hl = 0;
	render_titles(hl, title_ids, n_visible_ids);

	WPAD_Init();
	PAD_Init();

	while (1) {
		VIDEO_WaitVSync();
		poll_input();

		if (INPUT_HOME())
			break;

		if (INPUT_ACCEPT()) {
			if (hl >= 0 && hl < n_visible_ids) {
				char *t = (char*)&title_ids[hl] + 4;
				printf("\x1b[2;1HLaunching %c%c%c%c...", t[0], t[1], t[2], t[3]);
				WII_LaunchTitle(title_ids[hl]);
			}
		}

		int delta = 0;
		if (INPUT_UP())
			delta = -VISIBLE_COLS;
		else if (INPUT_DOWN())
			delta = VISIBLE_COLS;
		else if (INPUT_LEFT())
			delta = -1;
		else if (INPUT_RIGHT())
			delta = 1;

		int next_hl = hl + delta;
		if (next_hl >= 0 && next_hl < n_visible_ids)
			hl = next_hl;

		if (INPUT_ANY()) {
			VIDEO_ClearFrameBuffer(rmode, xfb, COLOR_BLACK);
			printf("\x1b[1;1HChannel Launcher");
			render_titles(hl, title_ids, n_visible_ids);
		}
	}

	printf("\x1b[2;1HExiting...");
	return 0;
}
