#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <stdbool.h>

typedef unsigned char u8;

#define TITLE "Image Viewer"
#define WIDTH 1500
#define HEIGHT 900

#define ZOOM_COEFF 0.1

int dpi_w, dpi_h;
SDL_Window *window;
SDL_Renderer *renderer;

bool init(const char *title, int width, int height) {
	int res = SDL_Init(SDL_INIT_VIDEO);
	if (res != 0) {
		SDL_Log("SDL_Init() failed (%d)", res);
		return false;
	}

	window = SDL_CreateWindow(title, SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED, width, height, SDL_WINDOW_RESIZABLE);
	if (!window) {
		SDL_Log("SDL_CreateWindow() failed");
		return false;
	}

	float hdpi, vdpi;
	SDL_GetDisplayDPI(SDL_GetWindowDisplayIndex(window), NULL, &hdpi, &vdpi);

	dpi_w = (int)hdpi;
	dpi_h = (int)vdpi;

	renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
	if (!renderer) {
		SDL_Log("SDL_CreateRenderer() failed");
		return false;
	}

	SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);

	return true;
}

int clamp(int n, int min, int max) {
	if (n < min)
		return min;
	if (n > max && max >= min)
		return max;
	return n;
}

int main(int argc, char **argv) {
	if (!init(TITLE, WIDTH, HEIGHT))
		return 1;

	SDL_Surface *map_sf = IMG_Load(argv[1]);
	if (!map_sf)
		return 2;

	int map_w = map_sf->w;
	int map_h = map_sf->h;

	SDL_Texture *map = SDL_CreateTextureFromSurface(renderer, map_sf);
	SDL_FreeSurface(map_sf);

	float x = 0, y = 0;
	float scale = 1.0;
	float cursor_x = 0, cursor_y = 0;

	int click_x = -1, click_y = -1;
	int prev_mouse_x = -1, prev_mouse_y = -1;
	bool lmouse = false;
	bool ctrl = false;

	while (true) {
		bool quit = false;
		bool lclick = false;
		bool lrelease = false;
		int scroll_y = 0;

		int mouse_x = -1, mouse_y = -1;
		SDL_GetMouseState(&mouse_x, &mouse_y);

		SDL_Event event;
		while (!quit && SDL_PollEvent(&event)) {
			switch (event.type) {
				case SDL_QUIT:
					quit = true;
					break;
				case SDL_MOUSEBUTTONDOWN:
					if (event.button.button == SDL_BUTTON_LEFT) {
						lclick = true;
						lmouse = true;
						click_x = mouse_x;
						click_y = mouse_y;
					}
					SDL_CaptureMouse((SDL_bool)true);
					break;
				case SDL_MOUSEBUTTONUP:
					lmouse = false;
					lrelease = false;
					SDL_CaptureMouse((SDL_bool)false);
					break;
				case SDL_MOUSEWHEEL:
					scroll_y = event.wheel.y;
					break;
				case SDL_KEYDOWN:
				{
					SDL_Keycode key = event.key.keysym.sym;
					if (key == SDLK_LCTRL) ctrl = true;
					break;
				}
				case SDL_KEYUP:
				{
					SDL_Keycode key = event.key.keysym.sym;
					if (key == SDLK_LCTRL) ctrl = false;
					break;
				}
			}
		}
		if (quit)
			break;

		int sc_w, sc_h;
		SDL_GetRendererOutputSize(renderer, &sc_w, &sc_h);
		int half_sc_w = sc_w / 2;
		int half_sc_h = sc_h / 2;

		if (ctrl) {
			if (scroll_y != 0) {
				float coeff = 1.0 + ZOOM_COEFF;
				if (scroll_y < 0)
					coeff = 1.0 / coeff;

				int cur_x = (mouse_x - half_sc_w) / scale;
				int cur_y = (mouse_y - half_sc_h) / scale;

				scale *= coeff;

				x += cur_x - (mouse_x - half_sc_w) / scale;
				y += cur_y - (mouse_y - half_sc_h) / scale;
			}
			if (lmouse && prev_mouse_x >= 0 && prev_mouse_y >= 0) {
				x += (prev_mouse_x - mouse_x) / scale;
				y += (prev_mouse_y - mouse_y) / scale;
			}
		}

		x = clamp(x, 0, map_w);
		y = clamp(y, 0, map_h);

		int scaled_half_w = half_sc_w / scale;
		int scaled_half_h = half_sc_h / scale;
		int scaled_w = sc_w / scale;
		int scaled_h = sc_h / scale;
		int end_x = map_w - scaled_w;
		int end_y = map_h - scaled_h;

		SDL_Rect src = {
			x - scaled_half_w,
			y - scaled_half_h,
			sc_w / scale,
			sc_h / scale
		};
		SDL_Rect dst = {
			0,
			0,
			sc_w,
			sc_h
		};

		if (src.x < 0) {
			dst.x = -src.x * scale;
			src.x = 0;
		}
		if (src.x > end_x) {
			dst.w = sc_w - (src.x - end_x) * scale;
			src.w = dst.w / scale;
		}
		if (src.y < 0) {
			dst.y = -src.y * scale;
			src.y = 0;
		}
		if (src.y > end_y) {
			dst.h = sc_h - (src.y - end_y) * scale;
			src.h = dst.h / scale;
		}

		SDL_RenderClear(renderer);
		SDL_RenderCopy(renderer, map, &src, &dst);
		SDL_RenderPresent(renderer);

		prev_mouse_x = mouse_x;
		prev_mouse_y = mouse_y;
	}

	SDL_DestroyTexture(map);
	SDL_DestroyRenderer(renderer);
	SDL_DestroyWindow(window);
	SDL_Quit();
	return 0;
}
