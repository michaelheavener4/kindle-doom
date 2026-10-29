/* Kindle Paperwhite e-ink backend for doomgeneric.
 * Targets the MTK hwtcon panel (PW5 and later, including PW6 / Bellatrix4).
 * The update ioctl matches the fbink binary shipped by the jailbreak.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#include <linux/fb.h>
#include <linux/input.h>

enum {
	LNX_EV_SYN = EV_SYN,
	LNX_EV_KEY = EV_KEY,
	LNX_EV_ABS = EV_ABS,
	LNX_SYN_REPORT = SYN_REPORT,
	LNX_ABS_X = ABS_X,
	LNX_ABS_Y = ABS_Y,
	LNX_ABS_MT_X = ABS_MT_POSITION_X,
	LNX_ABS_MT_Y = ABS_MT_POSITION_Y,
	LNX_ABS_MT_ID = ABS_MT_TRACKING_ID,
	LNX_BTN_TOUCH = BTN_TOUCH,
	LNX_KEY_POWER = KEY_POWER
};

#ifdef KEY_TAB
#	undef KEY_TAB
#endif
#ifdef KEY_ENTER
#	undef KEY_ENTER
#endif
#ifdef KEY_BACKSPACE
#	undef KEY_BACKSPACE
#endif
#ifdef KEY_MINUS
#	undef KEY_MINUS
#endif
#ifdef KEY_F1
#	undef KEY_F1
#endif
#ifdef KEY_F2
#	undef KEY_F2
#endif
#ifdef KEY_F3
#	undef KEY_F3
#endif
#ifdef KEY_F4
#	undef KEY_F4
#endif
#ifdef KEY_F5
#	undef KEY_F5
#endif
#ifdef KEY_F6
#	undef KEY_F6
#endif
#ifdef KEY_F7
#	undef KEY_F7
#endif
#ifdef KEY_F8
#	undef KEY_F8
#endif
#ifdef KEY_F9
#	undef KEY_F9
#endif
#ifdef KEY_F10
#	undef KEY_F10
#endif
#ifdef KEY_F11
#	undef KEY_F11
#endif
#ifdef KEY_F12
#	undef KEY_F12
#endif
#ifdef KEY_HOME
#	undef KEY_HOME
#endif
#ifdef KEY_END
#	undef KEY_END
#endif
#ifdef KEY_PAUSE
#	undef KEY_PAUSE
#endif

#include "doomgeneric.h"
#include "doomkeys.h"
#include "font8x8_basic.h"

/* PW6 hwtcon update, same layout fbink uses (ioctl size 96). */
struct kd_rect {
	uint32_t top;
	uint32_t left;
	uint32_t width;
	uint32_t height;
};

struct kd_alt {
	uint32_t phys_addr;
	uint32_t width;
	uint32_t height;
	struct kd_rect region;
};

struct kd_swipe {
	uint32_t direction;
	uint32_t steps;
};

struct kd_update {
	struct kd_rect region;
	uint32_t waveform_mode;
	uint32_t update_mode;
	uint32_t update_marker;
	int temp;
	unsigned int flags;
	int dither_mode;
	int quant_bit;
	struct kd_alt alt;
	struct kd_swipe swipe;
	uint32_t hist_bw;
	uint32_t hist_gray;
	uint32_t ts_pxp;
	uint32_t ts_epdc;
};

_Static_assert(sizeof(struct kd_update) == 96, "MTK update struct must be 96 bytes");

#define KD_IOW(nr, type) _IOC(_IOC_WRITE, 'F', (nr), sizeof(type))
#define KD_SEND_UPDATE KD_IOW(0x2E, struct kd_update)

#define KD_PARTIAL 0
#define KD_FULL 1
#define KD_TEMP_AMBIENT 0x1000
#define KD_WFM_DU 1
#define KD_WFM_GC16 2
#define KD_WFM_A2 6
#define KD_FLAG_INVERT 0x01
#define KD_FLAG_MONO 0x02
#define KD_FLAG_DITHER_Y1 0x2000
#define KD_FLAG_DITHER_Y4 0x4000

#define CFG_PATH "/mnt/us/doom/kindle.cfg"
#define MAX_INPUTS 8
#define KEYQUEUE 64

enum {
	ACT_FIRE = 1,
	ACT_USE,
	ACT_GUN,
	ACT_MAP,
	ACT_MENU,
	ACT_OK,
	ACT_WAV,
	ACT_FLASH,
	ACT_INV,
	ACT_ROT,
	ACT_TOUCH,
	ACT_QUIT
};

struct button {
	int action;
	int x, y, w, h;
	char label[8];
};

static int fb_fd = -1;
static uint8_t *fb_base;
static uint8_t *fb_ptr;
static size_t fb_map_len;
static int fb_w, fb_h, fb_stride, fb_bpp;
static int red_off, green_off, blue_off;

static int rotate = 90;
static int waveform = KD_WFM_A2;
static int contrast = 160;
static int flash_every = 40;
static int invert = 0;
static int touch_mode = 0;
static int dither = 1;

static int canvas_w, canvas_h, bar_h, bar_y;
static int view_x, view_y, view_w, view_h, scale;
static int deadzone;
static struct button buttons[12];
static int nbuttons;

static int need_full = 1;
static int chrome_dirty = 1;
static int frame_i;
static int quit_requested;
static uint32_t update_marker = 1;
static int refresh_errors;

static struct timeval start_time;

static int input_fds[MAX_INPUTS];
static int ninputs;
static struct input_absinfo abs_x, abs_y;
static int have_abs;

static unsigned short keyq[KEYQUEUE];
static unsigned keyq_r, keyq_w;

static int touch_down;
static int gesture_latched;
static int origin_lx, origin_ly;
static int stick_active, look_active;
static int hold_up, hold_down, hold_sl, hold_sr, hold_tl, hold_tr;
static int weapon = 1;

static const char *wfm_name(int w)
{
	if (w == KD_WFM_DU)
		return "DU";
	if (w == KD_WFM_GC16)
		return "GC";
	return "A2";
}

static void push_key(int pressed, unsigned char key)
{
	unsigned next = (keyq_w + 1) % KEYQUEUE;
	if (next == keyq_r)
		return;
	keyq[keyq_w] = (unsigned short)((pressed ? 0x100 : 0) | key);
	keyq_w = next;
}

static void tap_key(unsigned char key)
{
	push_key(1, key);
	push_key(0, key);
}

static void set_held(int *slot, int want, unsigned char key)
{
	if (want && !*slot) {
		push_key(1, key);
		*slot = 1;
	} else if (!want && *slot) {
		push_key(0, key);
		*slot = 0;
	}
}

static void release_move(void)
{
	set_held(&hold_up, 0, KEY_UPARROW);
	set_held(&hold_down, 0, KEY_DOWNARROW);
	set_held(&hold_sl, 0, KEY_STRAFE_L);
	set_held(&hold_sr, 0, KEY_STRAFE_R);
	set_held(&hold_tl, 0, KEY_LEFTARROW);
	set_held(&hold_tr, 0, KEY_RIGHTARROW);
	stick_active = 0;
	look_active = 0;
}

static void save_cfg(void)
{
	FILE *f = fopen(CFG_PATH, "w");
	if (!f)
		return;
	fprintf(f,
		"# kindle-doom. Edited live by the on-screen buttons too.\n"
		"rotate=%d\n"
		"waveform=%s\n"
		"contrast=%d\n"
		"flash_every=%d\n"
		"invert=%d\n"
		"touch=%d\n"
		"dither=%d\n",
		rotate, wfm_name(waveform), contrast, flash_every, invert,
		touch_mode, dither);
	fclose(f);
}

static void load_cfg(void)
{
	FILE *f = fopen(CFG_PATH, "r");
	char line[128];
	if (!f)
		return;
	while (fgets(line, sizeof line, f)) {
		int v;
		char word[32];
		if (line[0] == '#' || line[0] == '\n')
			continue;
		if (sscanf(line, "rotate=%d", &v) == 1) {
			if (v == 0 || v == 90 || v == 180 || v == 270)
				rotate = v;
		} else if (sscanf(line, "contrast=%d", &v) == 1) {
			if (v >= 40 && v <= 300)
				contrast = v;
		} else if (sscanf(line, "flash_every=%d", &v) == 1) {
			if (v >= 0 && v <= 600)
				flash_every = v;
		} else if (sscanf(line, "invert=%d", &v) == 1) {
			invert = v ? 1 : 0;
		} else if (sscanf(line, "touch=%d", &v) == 1) {
			if (v >= 0 && v <= 7)
				touch_mode = v;
		} else if (sscanf(line, "dither=%d", &v) == 1) {
			dither = v ? 1 : 0;
		} else if (sscanf(line, "waveform=%31s", word) == 1) {
			if (!strcmp(word, "DU"))
				waveform = KD_WFM_DU;
			else if (!strcmp(word, "GC"))
				waveform = KD_WFM_GC16;
			else
				waveform = KD_WFM_A2;
		}
	}
	fclose(f);
}

static void to_fb(int lx, int ly, int *fx, int *fy)
{
	switch (rotate) {
	case 90:
		*fx = ly;
		*fy = canvas_w - 1 - lx;
		break;
	case 180:
		*fx = canvas_w - 1 - lx;
		*fy = canvas_h - 1 - ly;
		break;
	case 270:
		*fx = canvas_h - 1 - ly;
		*fy = lx;
		break;
	default:
		*fx = lx;
		*fy = ly;
		break;
	}
}

static void fb_to_logical(int fx, int fy, int *lx, int *ly)
{
	switch (rotate) {
	case 90:
		*ly = fx;
		*lx = canvas_w - 1 - fy;
		break;
	case 180:
		*lx = canvas_w - 1 - fx;
		*ly = canvas_h - 1 - fy;
		break;
	case 270:
		*lx = fy;
		*ly = canvas_h - 1 - fx;
		break;
	default:
		*lx = fx;
		*ly = fy;
		break;
	}
}

static void logical_bounds(int x, int y, int w, int h, int *fl, int *ft, int *fw, int *fh)
{
	int xs[4], ys[4], i, minx, maxx, miny, maxy;
	to_fb(x, y, &xs[0], &ys[0]);
	to_fb(x + w - 1, y, &xs[1], &ys[1]);
	to_fb(x, y + h - 1, &xs[2], &ys[2]);
	to_fb(x + w - 1, y + h - 1, &xs[3], &ys[3]);
	minx = maxx = xs[0];
	miny = maxy = ys[0];
	for (i = 1; i < 4; i++) {
		if (xs[i] < minx)
			minx = xs[i];
		if (xs[i] > maxx)
			maxx = xs[i];
		if (ys[i] < miny)
			miny = ys[i];
		if (ys[i] > maxy)
			maxy = ys[i];
	}
	*fl = minx;
	*ft = miny;
	*fw = maxx - minx + 1;
	*fh = maxy - miny + 1;
}

static void put_px(int fx, int fy, uint8_t gray)
{
	uint8_t *p;
	uint16_t pix16;
	uint32_t pix32;
	if (fx < 0 || fy < 0 || fx >= fb_w || fy >= fb_h)
		return;
	p = fb_ptr + (size_t)fy * (size_t)fb_stride + (size_t)fx * (size_t)(fb_bpp / 8);
	if (fb_bpp == 8) {
		*p = gray;
	} else if (fb_bpp == 16) {
		pix16 = (uint16_t)(((gray & 0xF8) << 8) | ((gray & 0xFC) << 3) | (gray >> 3));
		memcpy(p, &pix16, 2);
	} else if (fb_bpp == 24) {
		p[0] = p[1] = p[2] = gray;
	} else if (fb_bpp == 32) {
		pix32 = ((uint32_t)gray << red_off) | ((uint32_t)gray << green_off) |
			((uint32_t)gray << blue_off);
		memcpy(p, &pix32, 4);
	}
}

static void fill_logical(int x, int y, int w, int h, uint8_t gray)
{
	int lx, ly;
	for (ly = y; ly < y + h; ly++) {
		for (lx = x; lx < x + w; lx++) {
			int fx, fy;
			to_fb(lx, ly, &fx, &fy);
			put_px(fx, fy, gray);
		}
	}
}

static void draw_text(int x, int y, const char *s, int sc, uint8_t color)
{
	for (; *s; s++) {
		unsigned char ch = (unsigned char)*s;
		int row, col;
		if (ch > 127)
			ch = '?';
		for (row = 0; row < 8; row++) {
			unsigned char bits = (unsigned char)font8x8_basic[ch][row];
			for (col = 0; col < 8; col++) {
				if (bits & (1u << col))
					fill_logical(x + col * sc, y + row * sc, sc, sc, color);
			}
		}
		x += 8 * sc;
	}
}

static void button_label(struct button *b)
{
	switch (b->action) {
	case ACT_FIRE:
		snprintf(b->label, sizeof b->label, "FIRE");
		break;
	case ACT_USE:
		snprintf(b->label, sizeof b->label, "USE");
		break;
	case ACT_GUN:
		snprintf(b->label, sizeof b->label, "GUN");
		break;
	case ACT_MAP:
		snprintf(b->label, sizeof b->label, "MAP");
		break;
	case ACT_MENU:
		snprintf(b->label, sizeof b->label, "MENU");
		break;
	case ACT_OK:
		snprintf(b->label, sizeof b->label, "OK");
		break;
	case ACT_WAV:
		snprintf(b->label, sizeof b->label, "%s", wfm_name(waveform));
		break;
	case ACT_FLASH:
		snprintf(b->label, sizeof b->label, "FLASH");
		break;
	case ACT_INV:
		snprintf(b->label, sizeof b->label, "%s", invert ? "INV" : "DAY");
		break;
	case ACT_ROT:
		snprintf(b->label, sizeof b->label, "R%d", rotate);
		break;
	case ACT_TOUCH:
		snprintf(b->label, sizeof b->label, "T%d", touch_mode);
		break;
	case ACT_QUIT:
		snprintf(b->label, sizeof b->label, "QUIT");
		break;
	default:
		b->label[0] = 0;
		break;
	}
}

static void layout(void)
{
	static const int actions[] = {
		ACT_FIRE, ACT_USE, ACT_GUN, ACT_MAP, ACT_MENU, ACT_OK,
		ACT_WAV, ACT_FLASH, ACT_INV, ACT_ROT, ACT_TOUCH, ACT_QUIT
	};
	int area_h, sx, sy, i, bw;

	if (rotate == 90 || rotate == 270) {
		canvas_w = fb_h;
		canvas_h = fb_w;
	} else {
		canvas_w = fb_w;
		canvas_h = fb_h;
	}
	bar_h = canvas_h / 7;
	if (bar_h < 96)
		bar_h = 96;
	if (bar_h > canvas_h / 3)
		bar_h = canvas_h / 3;
	area_h = canvas_h - bar_h;
	sx = canvas_w / DOOMGENERIC_RESX;
	sy = area_h / DOOMGENERIC_RESY;
	scale = sx < sy ? sx : sy;
	if (scale < 1)
		scale = 1;
	view_w = DOOMGENERIC_RESX * scale;
	view_h = DOOMGENERIC_RESY * scale;
	view_x = (canvas_w - view_w) / 2;
	view_y = (area_h - view_h) / 2;
	bar_y = area_h;
	nbuttons = (int)(sizeof actions / sizeof actions[0]);
	bw = canvas_w / nbuttons;
	for (i = 0; i < nbuttons; i++) {
		buttons[i].action = actions[i];
		buttons[i].x = i * bw + 2;
		buttons[i].y = bar_y + 4;
		buttons[i].w = bw - 4;
		buttons[i].h = bar_h - 8;
		button_label(&buttons[i]);
	}
	deadzone = canvas_w / 26;
	if (deadzone < 28)
		deadzone = 28;
	printf("layout rotate=%d canvas=%dx%d scale=%d view=%d,%d %dx%d bar=%d\n",
	       rotate, canvas_w, canvas_h, scale, view_x, view_y, view_w, view_h, bar_h);
}

static void draw_chrome(void)
{
	int i;
	fill_logical(0, bar_y, canvas_w, bar_h, 210);
	for (i = 0; i < nbuttons; i++) {
		struct button *b = &buttons[i];
		int sc = (b->w >= 88 && b->h >= 28) ? 2 : 1;
		int tw = (int)strlen(b->label) * 8 * sc;
		int th = 8 * sc;
		int tx, ty;
		button_label(b);
		fill_logical(b->x, b->y, b->w, b->h, 255);
		fill_logical(b->x, b->y, b->w, 2, 0);
		fill_logical(b->x, b->y + b->h - 2, b->w, 2, 0);
		fill_logical(b->x, b->y, 2, b->h, 0);
		fill_logical(b->x + b->w - 2, b->y, 2, b->h, 0);
		tx = b->x + (b->w - tw) / 2;
		ty = b->y + (b->h - th) / 2;
		if (tx < b->x + 4)
			tx = b->x + 4;
		if (ty < b->y + 4)
			ty = b->y + 4;
		draw_text(tx, ty, b->label, sc, 0);
	}
}

static void paint_background(void)
{
	fill_logical(0, 0, canvas_w, canvas_h, 255);
}

static uint8_t shade(uint32_t p)
{
	int r = (int)((p >> 16) & 255);
	int g = (int)((p >> 8) & 255);
	int b = (int)(p & 255);
	int y = (r * 77 + g * 150 + b * 29) >> 8;
	y = 128 + ((y - 128) * contrast) / 100;
	if (y < 0)
		y = 0;
	if (y > 255)
		y = 255;
	if (invert)
		y = 255 - y;
	return (uint8_t)y;
}

static void blit_view(void)
{
	int dy, dx;
	for (dy = 0; dy < view_h; dy++) {
		int sy = dy / scale;
		const pixel_t *row = DG_ScreenBuffer + sy * DOOMGENERIC_RESX;
		for (dx = 0; dx < view_w; dx++) {
			int fx, fy;
			uint8_t g = shade(row[dx / scale]);
			to_fb(view_x + dx, view_y + dy, &fx, &fy);
			put_px(fx, fy, g);
		}
	}
}

static void refresh_region(int left, int top, int width, int height, uint32_t wfm, int full)
{
	struct kd_update up;
	unsigned int flags = 0;
	if (width < 1 || height < 1)
		return;
	if (left < 0)
		left = 0;
	if (top < 0)
		top = 0;
	if (left >= fb_w || top >= fb_h)
		return;
	if (left + width > fb_w)
		width = fb_w - left;
	if (top + height > fb_h)
		height = fb_h - top;

	if (dither) {
		if (wfm == KD_WFM_A2 || wfm == KD_WFM_DU)
			flags |= KD_FLAG_DITHER_Y1;
		else
			flags |= KD_FLAG_DITHER_Y4;
	} else if (wfm == KD_WFM_A2) {
		flags |= KD_FLAG_MONO;
	}

	memset(&up, 0, sizeof up);
	up.region.top = (uint32_t)top;
	up.region.left = (uint32_t)left;
	up.region.width = (uint32_t)width;
	up.region.height = (uint32_t)height;
	up.waveform_mode = wfm;
	up.update_mode = full ? KD_FULL : KD_PARTIAL;
	up.update_marker = update_marker++;
	up.temp = KD_TEMP_AMBIENT;
	up.flags = flags;
	up.hist_bw = KD_WFM_DU;
	up.hist_gray = KD_WFM_GC16;

	if (ioctl(fb_fd, KD_SEND_UPDATE, &up) < 0) {
		if (refresh_errors < 6) {
			fprintf(stderr, "eink refresh %s %s %dx%d @ %d,%d failed: %s\n",
				wfm_name((int)wfm), full ? "full" : "partial", width, height,
				left, top, strerror(errno));
		}
		refresh_errors++;
		/* Bellatrix4 honors Y1 dither. If the kernel rejects it, retry once bare. */
		if (flags && refresh_errors < 4) {
			up.flags = (wfm == KD_WFM_A2) ? KD_FLAG_MONO : 0;
			up.update_marker = update_marker++;
			if (ioctl(fb_fd, KD_SEND_UPDATE, &up) == 0) {
				dither = 0;
				fprintf(stderr, "eink dither rejected, continuing without it\n");
			}
		}
	}
}

static void refresh_logical(int x, int y, int w, int h, uint32_t wfm, int full)
{
	int fl, ft, fw, fh;
	logical_bounds(x, y, w, h, &fl, &ft, &fw, &fh);
	refresh_region(fl, ft, fw, fh, wfm, full);
}

static void refresh_screen(uint32_t wfm, int full)
{
	refresh_region(0, 0, fb_w, fb_h, wfm, full);
}

static int open_input(const char *path)
{
	int fd = open(path, O_RDONLY | O_NONBLOCK);
	struct input_absinfo ax, ay;
	unsigned char keys[(KEY_MAX + 7) / 8];
	int touch = 0, power = 0;
	if (fd < 0)
		return -1;
	memset(&ax, 0, sizeof ax);
	memset(&ay, 0, sizeof ay);
	if (ioctl(fd, EVIOCGABS(LNX_ABS_MT_X), &ax) == 0 && ax.maximum > ax.minimum &&
	    ioctl(fd, EVIOCGABS(LNX_ABS_MT_Y), &ay) == 0 && ay.maximum > ay.minimum)
		touch = 1;
	else if (ioctl(fd, EVIOCGABS(LNX_ABS_X), &ax) == 0 && ax.maximum > ax.minimum &&
		 ioctl(fd, EVIOCGABS(LNX_ABS_Y), &ay) == 0 && ay.maximum > ay.minimum)
		touch = 1;
	memset(keys, 0, sizeof keys);
	if (ioctl(fd, EVIOCGBIT(LNX_EV_KEY, sizeof keys), keys) == 0) {
		if (keys[LNX_KEY_POWER / 8] & (1u << (LNX_KEY_POWER % 8)))
			power = 1;
	}
	if (!touch && !power) {
		close(fd);
		return -1;
	}
	if (touch && !have_abs) {
		abs_x = ax;
		abs_y = ay;
		have_abs = 1;
		printf("touch %s x=%d..%d y=%d..%d\n", path, ax.minimum, ax.maximum, ay.minimum,
		       ay.maximum);
	} else if (power) {
		printf("keys %s\n", path);
	}
	return fd;
}

static void scan_inputs(void)
{
	char path[64];
	int i;
	for (i = 0; i < 32 && ninputs < MAX_INPUTS; i++) {
		int fd;
		snprintf(path, sizeof path, "/dev/input/event%d", i);
		fd = open_input(path);
		if (fd >= 0)
			input_fds[ninputs++] = fd;
	}
	printf("input fds: %d\n", ninputs);
}

static void norm_touch(int raw_x, int raw_y, int *fx, int *fy)
{
	double nx, ny;
	int swap = touch_mode & 1;
	int mx = touch_mode & 2;
	int my = touch_mode & 4;
	if (!have_abs || abs_x.maximum == abs_x.minimum || abs_y.maximum == abs_y.minimum) {
		*fx = raw_x;
		*fy = raw_y;
		return;
	}
	nx = (double)(raw_x - abs_x.minimum) / (double)(abs_x.maximum - abs_x.minimum);
	ny = (double)(raw_y - abs_y.minimum) / (double)(abs_y.maximum - abs_y.minimum);
	if (swap) {
		double t = nx;
		nx = ny;
		ny = t;
	}
	if (mx)
		nx = 1.0 - nx;
	if (my)
		ny = 1.0 - ny;
	if (nx < 0)
		nx = 0;
	if (ny < 0)
		ny = 0;
	if (nx > 1)
		nx = 1;
	if (ny > 1)
		ny = 1;
	*fx = (int)(nx * (fb_w - 1));
	*fy = (int)(ny * (fb_h - 1));
}

static void do_action(int action)
{
	switch (action) {
	case ACT_FIRE:
		tap_key(KEY_FIRE);
		break;
	case ACT_USE:
		tap_key(KEY_USE);
		break;
	case ACT_GUN:
		weapon++;
		if (weapon > 7)
			weapon = 1;
		tap_key((unsigned char)('0' + weapon));
		break;
	case ACT_MAP:
		tap_key(KEY_TAB);
		break;
	case ACT_MENU:
		tap_key(KEY_ESCAPE);
		break;
	case ACT_OK:
		tap_key(KEY_ENTER);
		break;
	case ACT_WAV:
		if (waveform == KD_WFM_A2)
			waveform = KD_WFM_DU;
		else if (waveform == KD_WFM_DU)
			waveform = KD_WFM_GC16;
		else
			waveform = KD_WFM_A2;
		chrome_dirty = 1;
		need_full = 1;
		save_cfg();
		printf("waveform %s\n", wfm_name(waveform));
		break;
	case ACT_FLASH:
		need_full = 1;
		break;
	case ACT_INV:
		invert = !invert;
		chrome_dirty = 1;
		need_full = 1;
		save_cfg();
		break;
	case ACT_ROT:
		rotate = (rotate + 90) % 360;
		layout();
		chrome_dirty = 1;
		need_full = 1;
		save_cfg();
		break;
	case ACT_TOUCH:
		touch_mode = (touch_mode + 1) & 7;
		chrome_dirty = 1;
		save_cfg();
		printf("touch mode %d (bit0 swap, bit1 mirror x, bit2 mirror y)\n", touch_mode);
		break;
	case ACT_QUIT:
		quit_requested = 1;
		break;
	default:
		break;
	}
}

static int button_at(int lx, int ly)
{
	int i;
	if (ly < bar_y)
		return -1;
	for (i = 0; i < nbuttons; i++) {
		struct button *b = &buttons[i];
		if (lx >= b->x && lx < b->x + b->w && ly >= b->y && ly < b->y + b->h)
			return b->action;
	}
	return -1;
}

static void gesture_move(int lx, int ly)
{
	int dx = lx - origin_lx;
	int dy = ly - origin_ly;
	if (stick_active) {
		set_held(&hold_up, dy < -deadzone, KEY_UPARROW);
		set_held(&hold_down, dy > deadzone, KEY_DOWNARROW);
		set_held(&hold_sl, dx < -deadzone, KEY_STRAFE_L);
		set_held(&hold_sr, dx > deadzone, KEY_STRAFE_R);
	}
	if (look_active) {
		set_held(&hold_tl, dx < -deadzone, KEY_LEFTARROW);
		set_held(&hold_tr, dx > deadzone, KEY_RIGHTARROW);
	}
}

static void touch_up(int lx, int ly)
{
	int dx = lx - origin_lx;
	int dy = ly - origin_ly;
	int action;
	if (stick_active || look_active) {
		if (look_active && dx > -deadzone && dx < deadzone && dy > -deadzone && dy < deadzone)
			tap_key(KEY_FIRE);
		release_move();
		return;
	}
	action = button_at(origin_lx, origin_ly);
	if (action > 0 && action == button_at(lx, ly))
		do_action(action);
}

static void touch_down_at(int lx, int ly)
{
	origin_lx = lx;
	origin_ly = ly;
	if (ly >= bar_y)
		return;
	if (lx < canvas_w / 2)
		stick_active = 1;
	else
		look_active = 1;
}

static int raw_x = -1, raw_y = -1;
static int saw_abs;

static void handle_event(const struct input_event *ev)
{
	if (ev->type == LNX_EV_KEY && ev->code == LNX_KEY_POWER && ev->value == 1) {
		printf("power key, quitting\n");
		quit_requested = 1;
		return;
	}
	if (ev->type == LNX_EV_ABS) {
		if (ev->code == LNX_ABS_MT_X || ev->code == LNX_ABS_X) {
			raw_x = ev->value;
			saw_abs = 1;
		} else if (ev->code == LNX_ABS_MT_Y || ev->code == LNX_ABS_Y) {
			raw_y = ev->value;
			saw_abs = 1;
		} else if (ev->code == LNX_ABS_MT_ID) {
			touch_down = ev->value >= 0;
		}
	} else if (ev->type == LNX_EV_KEY && ev->code == LNX_BTN_TOUCH) {
		touch_down = ev->value != 0;
	} else if (ev->type == LNX_EV_SYN && ev->code == LNX_SYN_REPORT && saw_abs) {
		int fx, fy, lx, ly;
		norm_touch(raw_x, raw_y, &fx, &fy);
		fb_to_logical(fx, fy, &lx, &ly);
		if (lx < 0)
			lx = 0;
		if (ly < 0)
			ly = 0;
		if (lx >= canvas_w)
			lx = canvas_w - 1;
		if (ly >= canvas_h)
			ly = canvas_h - 1;
		if (touch_down) {
			if (!gesture_latched) {
				touch_down_at(lx, ly);
				gesture_latched = 1;
			} else {
				gesture_move(lx, ly);
			}
		} else if (gesture_latched) {
			touch_up(lx, ly);
			gesture_latched = 0;
		}
	}
}

static void pump_input(void)
{
	int i;
	for (i = 0; i < ninputs; i++) {
		struct input_event ev;
		while (read(input_fds[i], &ev, sizeof ev) == (ssize_t)sizeof ev)
			handle_event(&ev);
	}
}

static void on_signal(int sig)
{
	(void)sig;
	quit_requested = 1;
}

void DG_Init(void)
{
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;

	load_cfg();
	fb_fd = open("/dev/fb0", O_RDWR);
	if (fb_fd < 0) {
		perror("/dev/fb0");
		exit(1);
	}
	if (ioctl(fb_fd, FBIOGET_VSCREENINFO, &var) < 0 ||
	    ioctl(fb_fd, FBIOGET_FSCREENINFO, &fix) < 0) {
		perror("FBIOGET");
		exit(1);
	}
	fb_w = (int)var.xres;
	fb_h = (int)var.yres;
	fb_bpp = (int)var.bits_per_pixel;
	fb_stride = (int)fix.line_length;
	red_off = (int)var.red.offset;
	green_off = (int)var.green.offset;
	blue_off = (int)var.blue.offset;
	fb_map_len = fix.smem_len ? fix.smem_len : (size_t)fb_stride * var.yres_virtual;
	fb_base = mmap(NULL, fb_map_len, PROT_READ | PROT_WRITE, MAP_SHARED, fb_fd, 0);
	if (fb_base == MAP_FAILED) {
		perror("mmap fb");
		exit(1);
	}
	fb_ptr = fb_base + (size_t)var.yoffset * (size_t)fb_stride +
		 (size_t)var.xoffset * (size_t)(fb_bpp / 8);
	printf("fb %dx%d bpp=%d stride=%d red=%d green=%d blue=%d rotate_fb=%u\n", fb_w, fb_h,
	       fb_bpp, fb_stride, red_off, green_off, blue_off, var.rotate);
	if (fb_bpp != 8 && fb_bpp != 16 && fb_bpp != 24 && fb_bpp != 32) {
		fprintf(stderr, "unsupported bpp %d\n", fb_bpp);
		exit(1);
	}
	layout();
	scan_inputs();
	gettimeofday(&start_time, NULL);
	signal(SIGTERM, on_signal);
	signal(SIGINT, on_signal);
	printf("kindle-doom ready\n");
}

void DG_DrawFrame(void)
{
	uint32_t game_wfm = (uint32_t)waveform;
	pump_input();
	if (quit_requested)
		return;
	frame_i++;
	if (need_full) {
		paint_background();
		blit_view();
		draw_chrome();
		chrome_dirty = 0;
		need_full = 0;
		refresh_screen(KD_WFM_GC16, 1);
		return;
	}
	blit_view();
	refresh_logical(view_x, view_y, view_w, view_h, game_wfm, game_wfm == KD_WFM_GC16);
	if (chrome_dirty) {
		draw_chrome();
		chrome_dirty = 0;
		refresh_logical(0, bar_y, canvas_w, bar_h, KD_WFM_DU, 0);
	}
	if (flash_every > 0 && waveform == KD_WFM_A2 && (frame_i % flash_every) == 0)
		refresh_logical(view_x, view_y, view_w, view_h, KD_WFM_GC16, 1);
}

void DG_SleepMs(uint32_t ms)
{
	struct timespec ts;
	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (long)(ms % 1000) * 1000000L;
	nanosleep(&ts, NULL);
}

uint32_t DG_GetTicksMs(void)
{
	struct timeval now;
	gettimeofday(&now, NULL);
	return (uint32_t)((now.tv_sec - start_time.tv_sec) * 1000 +
			  (now.tv_usec - start_time.tv_usec) / 1000);
}

int DG_GetKey(int *pressed, unsigned char *doomKey)
{
	unsigned short packed;
	pump_input();
	if (keyq_r == keyq_w)
		return 0;
	packed = keyq[keyq_r];
	keyq_r = (keyq_r + 1) % KEYQUEUE;
	*pressed = packed >> 8;
	*doomKey = (unsigned char)(packed & 0xFF);
	return 1;
}

void DG_SetWindowTitle(const char *title)
{
	(void)title;
}

int main(int argc, char **argv)
{
	int i;
	doomgeneric_Create(argc, argv);
	while (!quit_requested)
		doomgeneric_Tick();
	release_move();
	printf("leaving the framebuffer\n");
	refresh_screen(KD_WFM_GC16, 1);
	for (i = 0; i < ninputs; i++)
		close(input_fds[i]);
	if (fb_base && fb_base != MAP_FAILED)
		munmap(fb_base, fb_map_len);
	if (fb_fd >= 0)
		close(fb_fd);
	return 0;
}
