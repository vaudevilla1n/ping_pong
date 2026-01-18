#include <fcntl.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <signal.h>
#include <unistd.h>
#include <termios.h>
#include <sys/ioctl.h>

#define unreachable(func) \
	do { fprintf(stderr, "unreachable (%s)\n", (func)); abort(); } while(0)

#define die(fmt, ...) \
	do { fprintf(stderr, fmt __VA_OPT__(,) __VA_ARGS__); fputc('\n', stderr); exit(1); } while(0)

#define assert_ok(ret, fmt, ...) \
	do { if (ret) { die(fmt __VA_OPT__(,) __VA_ARGS__); } } while(0)

typedef long double f64;

typedef struct {
	f64 x, y;
} vec2;

static inline vec2 vec2_new(const int x, const int y) {
	return (vec2){ x, y };
}

static inline f64 vec2_len(const vec2 a) {
	const f64 x = a.x;
	const f64 y = a.y;

	return sqrtl((x * x) + (y * y));
}

static inline vec2 vec2_add(const vec2 a, const vec2 b) {
	return (vec2){ a.x + b.x, a.y + b.y };
}

static inline vec2 vec2_sub(const vec2 a, const vec2 b) {
	return (vec2){ a.x - b.x, a.y - b.y };
}

static inline vec2 vec2_negate(const vec2 a) {
	return (vec2){ -a.x, -a.y };
}

static inline vec2 vec2_rotate(const vec2 a, const f64 degrees) {
	const f64 radians = degrees * M_PI/180;
	return (vec2){
		roundl(cosl(radians) * a.x - sinl(radians) * a.y),
		roundl(sinl(radians) * a.x + cosl(radians) * a.y),
	};
}

typedef struct {
	uint8_t r, g, b;
} rgb;

static inline rgb rgb_new(const uint8_t r, const uint8_t g, const uint8_t b) {
	return (rgb){ r, g, b };
}

#define RGB(col)		(col).r, (col).g, (col).b
#define WHITE			(rgb){ 0xFF, 0xFF, 0xFF }
#define BLACK			(rgb){ 0, 0, 0 }


void clear(void) {
	printf("\033[H\033[2J");
}

void clear_line(void) {
	printf("\033[0K");
}

void move(const vec2 pos) {
	printf("\033[%d;%dH", (int)pos.y, (int)pos.x);
}

void cursor_visible(const bool visible) {
	printf("\033[?25%c", visible ? 'h' : 'l');
}

void reset_graphics(void) {
	printf("\033[0m");
}

void start_graphics(void) {
	clear();
	cursor_visible(false);
}

void end_graphics(void) {
	reset_graphics();
	cursor_visible(true);
	clear();
}

void color_cell(const rgb col) {
	printf("\033[38;2;%u;%u;%um", RGB(col));
	printf("\033[48;2;%u;%u;%um", RGB(col));
}

typedef struct {
	int fd;

	struct termios attrs;

	int rows;
	int cols;
} tty_ctx_t;

tty_ctx_t tty_context;

#define DISPLAY_WIDTH	(tty_context.cols - 1)
#define DISPLAY_HEIGHT	(tty_context.rows - 1)

static inline void draw_cell(const vec2 pos, const rgb col, const char pixel) {
	move(pos);
	color_cell(col);
	putchar(pixel);
	reset_graphics();
}

void set_dimensions(tty_ctx_t *ctx) {
	struct winsize ws;
	assert_ok(ioctl(ctx->fd, TIOCGWINSZ, &ws), "unable to get window size");

	ctx->rows = ws.ws_row;
	ctx->cols = ws.ws_col;
}

void assert_is_tty(const int fd, const char *fd_name) {
	if (isatty(fd))
		return;
	
	die("%s is not a tty. exiting...", fd_name);
}

tty_ctx_t init_tty(void) {
	assert_is_tty(STDIN_FILENO, "stdin");
	assert_is_tty(STDOUT_FILENO, "stdout");

	tty_ctx_t ctx = {
		.fd = STDOUT_FILENO,
	};
	assert_ok(tcgetattr(ctx.fd, &ctx.attrs), "couldn't get terminal attributes");

	assert_ok(fcntl(STDIN_FILENO, F_SETFL, O_NONBLOCK), "couldn't set stdin to non-blocking");

	struct termios attr;
	cfmakeraw(&attr);
	assert_ok(tcsetattr(ctx.fd, TCSANOW, &attr), "couldn't set terminal attributes");

	set_dimensions(&ctx);

	return ctx;
}

void deinit_tty(tty_ctx_t *ctx) {
	assert_ok(tcsetattr(ctx->fd, TCSANOW, &ctx->attrs), "couldn't set terminal attributes");
}


typedef struct {
	vec2 pos;
	vec2 delta;
	vec2 size;
} entity_t;

#define MAX_ENTITY_WIDTH	(DISPLAY_WIDTH / 2)
#define MAX_ENTITY_HEIGHT	(DISPLAY_HEIGHT / 2)

#define MIN_ENTITY_WIDTH	1
#define MIN_ENTITY_HEIGHT	1

#define MAX_ENTITY_DELTA_X	DISPLAY_WIDTH
#define MAX_ENTITY_DELTA_Y	DISPLAY_HEIGHT

#define MIN_ENTITY_DELTA_X	0
#define MIN_ENTITY_DELTA_Y	0

#define DEFAULT_ENTITY_PROPERTIES \
	(entity_t){ .pos = (vec2){ 1, 60 }, .size = (vec2){ 2, 1 }, .delta = (vec2){ 0.005, 0.005 } }

static inline bool out_of_bounds_x(const f64 x) {
	return 1 > x || x >= DISPLAY_WIDTH;
}
static inline bool out_of_bounds_y(const f64 y) {
	return 1 > y || y >= DISPLAY_HEIGHT;
}
static inline bool out_of_bounds(const vec2 pos) {
	return out_of_bounds_x(pos.x) || out_of_bounds_y(pos.y);
}

static inline f64 constrain_x(const f64 x) {
	return x < 1 ? 1 : x >= DISPLAY_WIDTH ? DISPLAY_WIDTH - 1 : x;
}
static inline f64 constrain_y(const f64 y) {
	return y < 1 ? 1 : y >= DISPLAY_HEIGHT ? DISPLAY_HEIGHT - 1 : y;
}
static inline vec2 constrain(const vec2 v) {
	return (vec2) {
		constrain_x(v.x),
		constrain_y(v.y),
	};
}

static inline bool collision_x(const f64 x) {
	return 1 >= x || x >= DISPLAY_WIDTH;
}
static inline bool collision_y(const f64 y) {
	return 1 >= y || y >= DISPLAY_HEIGHT;
}
static inline bool collision(const vec2 pos) {
	return collision_x(pos.x) || collision_y(pos.y);
}

void entity_move(entity_t *e) {
	const vec2 end = vec2_add(e->pos, e->size);

	const vec2 new_pos = vec2_add(e->pos, e->delta);
	const vec2 new_end = vec2_add(end, e->delta);

	if (collision(new_pos)) {
		if (collision_x(new_pos.x)) {
			e->delta.x = -e->delta.x;
		} else {
			e->delta.y = -e->delta.y;
		}

		e->pos = constrain(new_pos);
	} else if (collision(new_end)) {
		if (collision_x(new_end.x)) {
			e->delta.x = -e->delta.x;
		} else {
			e->delta.y = -e->delta.y;
		}

		e->pos = vec2_sub(constrain(new_end), e->size);
	} else {
		e->pos = new_pos;
	}
}

void entity_draw(const entity_t *e) {
	for (int y = e->pos.y; y < e->pos.y + e->size.y; y++) {
		for (int x = e->pos.x; x < e->pos.x + e->size.x; x++) {
			// silly ass mistake
			// const vec2 pos = { x, e->pos.y + y };
			const vec2 pos = { x, y };

			if (out_of_bounds(pos))
				continue;

			draw_cell(pos, WHITE, ' ');
		}
	}
}

void entity_update(entity_t *e) {
	entity_move(e);
	entity_draw(e);
}


void resize_window(int sig) {
	if (sig != SIGWINCH)
		return;
	
	set_dimensions(&tty_context);
	clear();
}


typedef enum {
	NORMAL,
	RESIZE,
	SPEED,
	QUIT,
} command_state_t;

const char *command_state_string(const command_state_t s) {
	switch (s) {
	case NORMAL: return "normal";
	case RESIZE: return "resize";
	case SPEED: return "speed";
	case QUIT: return "quit";
	default: unreachable("command_state_string");
	}
}

void draw_info_line(const entity_t *e, const command_state_t command) {
	move(vec2_new(0, DISPLAY_HEIGHT));

	const vec2 entity_end = vec2_add(e->pos, e->size);
	printf("entity((%Lf, %Lf), (%Lf, %Lf)) delta(%Lf, %Lf) display: %d x %d (%s)\n",
			e->pos.x, e->pos.y,
			entity_end.x, entity_end.y,
			e->delta.x, e->delta.y,
			DISPLAY_WIDTH, DISPLAY_HEIGHT,
			command_state_string(command));
}

command_state_t handle_command(const command_state_t command, entity_t *e) {
	const int c = getchar();
	switch (c) {
	case 'q': return QUIT;
	case 'n': return NORMAL;
	}

	switch (command) {
	case QUIT: return QUIT;

	case NORMAL: {
		switch (c) {
		case 'r':
			return RESIZE;
		case 's':
			return SPEED;
		}
	} break;

	case RESIZE: {
		switch (c) {
		case 'w': {
			if (e->size.x < MAX_ENTITY_WIDTH && e->size.y < MAX_ENTITY_HEIGHT) {
				e->size.x++;
				e->size.y++;
			}
		} break;

		case 's': {
			if (e->size.x > MIN_ENTITY_WIDTH && e->size.y > MIN_ENTITY_HEIGHT) {
				e->size.x--;
				e->size.y--;
			}
		} break;
		}
	} break;

	case SPEED: {
		switch (c) {
		case 'w': {
			if (e->delta.x < MAX_ENTITY_DELTA_X && e->delta.y < MAX_ENTITY_DELTA_Y) {
				e->delta.x = (!e->delta.x + e->delta.x) * 2;
				e->delta.y = (!e->delta.y + e->delta.y) * 2;
			}
		} break;

		case 's': {
			if (e->delta.x > MIN_ENTITY_DELTA_X && e->delta.y > MIN_ENTITY_DELTA_Y) {
				e->delta.x /= 2;
				e->delta.y /= 2;
			}
		} break;
		}
	} break;

	default: unreachable("handle_command");
	}

	return command;
}

int main(void) {
	tty_context = init_tty();
	start_graphics();

	assert_ok(signal(SIGWINCH, resize_window), "couldn't set handler for resize signal");

	entity_t ball = DEFAULT_ENTITY_PROPERTIES;

	command_state_t command = NORMAL;

	for (;;) {
		clear();

		entity_update(&ball);
		draw_info_line(&ball, command);

		command = handle_command(command, &ball);

		if (command == QUIT)
			break;
	}

	end_graphics();
	deinit_tty(&tty_context);
}
