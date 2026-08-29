/*
 * File: tty.c
 * Purpose: Terminal driver implementation.
 *
 * This file is part of the esdumanOS test suite.
 */
#include "tty.h"
#include "io.h"
/*
 * Where the cells go, which this file used to know and no longer does. Six lines
 * here wrote to text-mode video memory and programmed a hardware cursor; they
 * were the only thing tying eight hundred lines of terminal to a machine with a
 * text mode. See include/console.h.
 */
#include "console.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define MAX_SCROLL 100
#define NUM_TERMINALS 3

/**
 * @brief Rows of text the screen actually shows.
 *
 * VGA row 0 is the status bar; update_screen() draws the buffer from row 1 down.
 * Every escape sequence that names a row counts in this space, not in the 25 the
 * hardware has and not in the 100 the scrollback buffer holds - get that wrong
 * and a full-screen program draws a line off, or over the status bar.
 */
#define TERM_ROWS (VGA_HEIGHT - 1)

/** Escape parser states. */
#define ESC_GROUND 0   /**< Ordinary text. */
#define ESC_ESC    1   /**< Saw 0x1B, waiting for '['. */
#define ESC_CSI    2   /**< Inside a control sequence, collecting parameters. */

/** Parameters a single sequence may carry. */
#define ESC_MAX_PARAMS 8

typedef struct {
	size_t cursor_x;
	size_t cursor_y;
	int view_offset;
	uint8_t color;

	/*
	 * Escape parser state, per terminal rather than global: there are three of
	 * them and the user can switch away in the middle of a sequence.
	 */
	int esc_state;
	uint32_t esc_params[ESC_MAX_PARAMS];
	int esc_param_count;

	/* Cursor save/restore, in buffer coordinates. */
	size_t saved_x;
	size_t saved_y;

	/* Scroll region, 1-based and view-relative; the full screen by default. */
	uint32_t scroll_top;
	uint32_t scroll_bottom;

	uint16_t buffer[MAX_SCROLL][VGA_WIDTH];
} terminal_state_t;

terminal_state_t terminals[NUM_TERMINALS];
size_t current_terminal = 0;

/**
 * @brief Combines foreground and background colors into a VGA color byte.
 * @param fg Foreground color.
 * @param bg Background color.
 * @return The combined VGA color byte.
 */
static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
	return fg | bg << 4;
}

/**
 * @brief Creates a VGA text mode entry with a character and color.
 * @param uc The character.
 * @param color The combined VGA color byte.
 * @return The 16-bit VGA entry.
 */
static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
	return (uint16_t) uc | (uint16_t) color << 8;
}

/**
 * @brief Moves the cursor to a screen position, or hides it.
 *
 * The name and the signature are unchanged and eleven callers rely on both. What
 * changed in v1.6.0 is that this no longer knows how a cursor is moved: in text
 * mode it is two CRT controller registers, on a framebuffer it is two rows of
 * pixels painted over the bottom of a cell, and neither of those is the
 * terminal's business.
 *
 * A column of VGA_WIDTH or beyond means "hide", which is how update_screen()
 * asks for it when the cursor has scrolled out of view.
 */
void	update_cursor(size_t x, size_t y) {
	console_set_cursor(x, y);
}

/**
 * @brief Sets the color for the current terminal.
 * @param fg Foreground color.
 * @param bg Background color.
 */
void terminal_setcolor(uint8_t fg, uint8_t bg) {
	terminals[current_terminal].color = vga_entry_color(fg, bg);
}

/**
 * @brief Draws a status bar at the top of the screen.
 * @param left Text to align on the left.
 * @param right Text to align on the right.
 */
void draw_status_bar(const char* left, const char* right) {
	uint8_t color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);

	for (int x = 0; x < VGA_WIDTH; x++)
		console_put_cell((size_t)x, 0, vga_entry(' ', color));

	// left
	for (int i = 0; left[i] != '\0'; i++)
		console_put_cell((size_t)i, 0, vga_entry(left[i], color));

	// right -strlen-
	int len = 0;
	while (right[len])
		len++;

	for (int i = 0; i < len; i++)
		console_put_cell((size_t)(VGA_WIDTH - len + i), 0, vga_entry(right[i], color));
}

/*
 * Deferred refresh.
 *
 * A full repaint is 80 by 24 cells copied out of the scrollback buffer, and
 * until this existed one happened for every completed escape sequence and the
 * hardware cursor was reprogrammed for every printable character. That is
 * affordable for a shell writing a prompt and ruinous for a program drawing a
 * screen: one frame of /bin/edit is around fifty sequences and eighteen hundred
 * characters, so it cost fifty repaints - about a hundred thousand cell writes -
 * and thirty-six hundred port writes, per keystroke.
 *
 * None of that work is wanted in the middle. What the screen looks like halfway
 * through a write is something nobody can see; only the state it is left in
 * matters. So a write marks the refresh as owed and pays it once, at the end.
 *
 * Nested writes save and restore rather than counting, so that a keystroke
 * arriving on IRQ1 in the middle of one - the driver echoes "^C" from interrupt
 * context - cannot lose an update or leave the flag set.
 */
static int refresh_deferred = 0;
static int refresh_owed = 0;

void update_screen(void);

/**
 * @brief Repaints now, or notes that a repaint is owed.
 */
static void term_refresh(void) {
	if (refresh_deferred) { refresh_owed = 1; return; }
	update_screen();
}

/**
 * @brief Moves the hardware cursor, or notes that the screen is owed a refresh.
 *
 * @param x Column.
 * @param y Row.
 */
static void term_refresh_cursor(size_t x, size_t y) {
	if (refresh_deferred) { refresh_owed = 1; return; }
	update_cursor(x, y);
}

/**
 * @brief Updates the screen based on the current terminal's state.
 */
void update_screen(void) {
	terminal_state_t *term = &terminals[current_terminal];

	for (size_t y = 1; y < VGA_HEIGHT; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			console_put_cell(x, y, term->buffer[term->view_offset + (y - 1)][x]);
		}
	}

	if (term->cursor_y >= (size_t)term->view_offset && term->cursor_y < (size_t)(term->view_offset + (VGA_HEIGHT - 1))) {
		update_cursor(term->cursor_x, term->cursor_y - term->view_offset + 1);
	}
	else {
		update_cursor(80, 25);
	}
}

/**
 * @brief Switches to a different virtual terminal.
 * @param term_no The terminal number to switch to (0 to NUM_TERMINALS-1).
 */
void terminal_switch(size_t term_no) {
	if (term_no >= NUM_TERMINALS) return;
	current_terminal = term_no;
	update_screen();
}

/**
 * @brief Initializes all virtual terminals.
 */
/*
 * Deliberately does not choose a console backend.
 *
 * This is the obvious place to put that and it is the wrong one, because
 * SYSCALL_CLEAR_SCREEN calls this function: a backend selected here would be
 * selected again every time a program ran `clear`, and on a machine booted with
 * a framebuffer that would drop the console back into text mode mid-session and
 * freeze the screen. Which backend is right depends on how the machine booted,
 * which is the boot path's knowledge and not the terminal's. See
 * drivers/console.c.
 */
void	terminal_initialize(void) {
	for (size_t i = 0; i < NUM_TERMINALS; i++) {
		terminals[i].cursor_x = 0;
		terminals[i].cursor_y = 0;
		terminals[i].view_offset = 0;
		terminals[i].color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

		terminals[i].esc_state = ESC_GROUND;
		terminals[i].esc_param_count = 0;
		terminals[i].saved_x = 0;
		terminals[i].saved_y = 0;
		terminals[i].scroll_top = 1;
		terminals[i].scroll_bottom = TERM_ROWS;

		for (size_t y = 0; y < MAX_SCROLL; y++) {
			for (size_t x = 0; x < VGA_WIDTH; x++) {
				terminals[i].buffer[y][x] = vga_entry(' ', terminals[i].color);
			}
		}
	}
	terminal_switch(0);
}

/*
 * ── ANSI escape sequences ───────────────────────────────────────────────
 *
 * Enough of the repertoire for a full-screen program to draw: absolute and
 * relative cursor motion, erasing, colour, a saved cursor, a scroll region and
 * line insertion. Everything else is swallowed rather than printed - a sequence
 * this does not implement should leave no trace, not spray its parameters across
 * the screen.
 *
 * Every row an escape names is counted in the 24 rows the screen shows, and
 * translated to the scrollback buffer through view_offset. The three coordinate
 * spaces in this file - hardware rows including the status bar, visible text
 * rows, and buffer rows - are the easiest thing here to get wrong.
 */

/**
 * @brief ANSI colour number to VGA colour.
 *
 * The two orders are not the same: ANSI counts black, red, green, yellow, blue,
 * magenta, cyan, white, while VGA puts blue at 1 and red at 4. ANSI's yellow is
 * VGA's brown, which becomes yellow once the bright bit is on.
 */
static const uint8_t ansi_to_vga[8] = {
	VGA_COLOR_BLACK, VGA_COLOR_RED,     VGA_COLOR_GREEN, VGA_COLOR_BROWN,
	VGA_COLOR_BLUE,  VGA_COLOR_MAGENTA, VGA_COLOR_CYAN,  VGA_COLOR_LIGHT_GREY
};

/**
 * @brief Buffer row for a 1-based screen row.
 */
static size_t term_row_to_buffer(terminal_state_t *term, uint32_t row) {
	if (row < 1) row = 1;
	if (row > TERM_ROWS) row = TERM_ROWS;
	return (size_t)term->view_offset + (size_t)(row - 1);
}

/** @brief Topmost buffer row of the scroll region. */
static size_t term_region_top(terminal_state_t *term) {
	return term_row_to_buffer(term, term->scroll_top);
}

/** @brief Bottommost buffer row of the scroll region. */
static size_t term_region_bottom(terminal_state_t *term) {
	return term_row_to_buffer(term, term->scroll_bottom);
}

/**
 * @brief Blanks a span of cells, inclusive at both ends.
 */
static void term_clear_span(terminal_state_t *term, size_t from_y, size_t from_x,
                            size_t to_y, size_t to_x) {
	for (size_t y = from_y; y <= to_y && y < MAX_SCROLL; y++) {
		size_t x0 = (y == from_y) ? from_x : 0;
		size_t x1 = (y == to_y) ? to_x : (VGA_WIDTH - 1);

		for (size_t x = x0; x <= x1 && x < VGA_WIDTH; x++) {
			term->buffer[y][x] = vga_entry(' ', term->color);
		}
	}
}

/**
 * @brief Opens n blank lines at the cursor, pushing the rest of the region down.
 *
 * Lines pushed past the bottom of the scroll region are lost, which is what a
 * scroll region is for.
 */
static void term_insert_lines(terminal_state_t *term, uint32_t n) {
	size_t top = term->cursor_y;
	size_t bot = term_region_bottom(term);

	if (n == 0) n = 1;
	/* Outside the scroll region there is nothing to open or close, which is what
	 * a real terminal does and what keeps a stale region from letting a program
	 * shift rows it no longer owns. */
	if (top < term_region_top(term) || top > bot) return;
	if (n > (uint32_t)(bot - top + 1)) n = (uint32_t)(bot - top + 1);

	/* From the bottom up, so a row is copied before it is overwritten. */
	for (size_t y = bot + 1; y-- > top + n; ) {
		for (size_t x = 0; x < VGA_WIDTH; x++) term->buffer[y][x] = term->buffer[y - n][x];
	}
	term_clear_span(term, top, 0, top + n - 1, VGA_WIDTH - 1);
}

/**
 * @brief Removes n lines at the cursor, pulling the rest of the region up.
 */
static void term_delete_lines(terminal_state_t *term, uint32_t n) {
	size_t top = term->cursor_y;
	size_t bot = term_region_bottom(term);

	if (n == 0) n = 1;
	/* Same bound as insertion, for the same reason. */
	if (top < term_region_top(term) || top > bot) return;
	if (n > (uint32_t)(bot - top + 1)) n = (uint32_t)(bot - top + 1);

	for (size_t y = top; y + n <= bot; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) term->buffer[y][x] = term->buffer[y + n][x];
	}
	term_clear_span(term, bot - n + 1, 0, bot, VGA_WIDTH - 1);
}

/**
 * @brief Applies a colour and attribute sequence.
 */
static void term_apply_sgr(terminal_state_t *term) {
	uint8_t fg = term->color & 0x0F;
	uint8_t bg = (term->color >> 4) & 0x0F;
	int count = term->esc_param_count ? term->esc_param_count : 1;

	for (int i = 0; i < count; i++) {
		uint32_t p = term->esc_params[i];

		if (p == 0) {
			fg = VGA_COLOR_LIGHT_GREY;
			bg = VGA_COLOR_BLACK;
		} else if (p == 1) {
			fg |= 0x08;                       /* bright */
		} else if (p == 22) {
			fg &= (uint8_t)~0x08;
		} else if (p >= 30 && p <= 37) {
			fg = (uint8_t)((fg & 0x08) | ansi_to_vga[p - 30]);
		} else if (p >= 40 && p <= 47) {
			bg = ansi_to_vga[p - 40];
		} else if (p >= 90 && p <= 97) {
			fg = (uint8_t)(ansi_to_vga[p - 90] | 0x08);
		}
	}

	term->color = vga_entry_color(fg, bg);
}

/**
 * @brief Carries out a completed control sequence.
 *
 * @param term  Terminal the sequence arrived on.
 * @param final The byte that ended it, which is what selects the operation.
 */
static void terminal_csi_dispatch(terminal_state_t *term, unsigned char final) {
	uint32_t p0 = (term->esc_param_count > 0) ? term->esc_params[0] : 0;
	uint32_t p1 = (term->esc_param_count > 1) ? term->esc_params[1] : 0;
	size_t top = (size_t)term->view_offset;
	size_t bottom = top + TERM_ROWS - 1;

	switch (final) {
		case 'H':                                /* CUP */
		case 'f':
			term->cursor_y = term_row_to_buffer(term, p0 ? p0 : 1);
			term->cursor_x = (p1 ? p1 : 1) - 1;
			if (term->cursor_x >= VGA_WIDTH) term->cursor_x = VGA_WIDTH - 1;
			break;

		case 'A': {                              /* CUU */
			size_t n = p0 ? p0 : 1;
			term->cursor_y = (term->cursor_y > top + n) ? term->cursor_y - n : top;
			break;
		}
		case 'B': {                              /* CUD */
			size_t n = p0 ? p0 : 1;
			term->cursor_y = (term->cursor_y + n < bottom) ? term->cursor_y + n : bottom;
			break;
		}
		case 'C': {                              /* CUF */
			size_t n = p0 ? p0 : 1;
			term->cursor_x = (term->cursor_x + n < VGA_WIDTH) ? term->cursor_x + n : VGA_WIDTH - 1;
			break;
		}
		case 'D': {                              /* CUB */
			size_t n = p0 ? p0 : 1;
			term->cursor_x = (term->cursor_x > n) ? term->cursor_x - n : 0;
			break;
		}

		case 'J':                                /* ED */
			/* The cursor deliberately does not move; a program that wants it at
			 * the origin sends ESC[H itself, as xterm requires. */
			if (p0 == 1)      term_clear_span(term, top, 0, term->cursor_y, term->cursor_x);
			else if (p0 == 2) term_clear_span(term, top, 0, bottom, VGA_WIDTH - 1);
			else              term_clear_span(term, term->cursor_y, term->cursor_x, bottom, VGA_WIDTH - 1);
			break;

		case 'K':                                /* EL */
			if (p0 == 1)      term_clear_span(term, term->cursor_y, 0, term->cursor_y, term->cursor_x);
			else if (p0 == 2) term_clear_span(term, term->cursor_y, 0, term->cursor_y, VGA_WIDTH - 1);
			else              term_clear_span(term, term->cursor_y, term->cursor_x, term->cursor_y, VGA_WIDTH - 1);
			break;

		case 'm':                                /* SGR */
			term_apply_sgr(term);
			break;

		case 's':                                /* Save cursor */
			term->saved_x = term->cursor_x;
			term->saved_y = term->cursor_y;
			break;

		case 'u':                                /* Restore cursor */
			term->cursor_x = term->saved_x;
			term->cursor_y = term->saved_y;
			break;

		case 'r': {                              /* DECSTBM */
			uint32_t t = p0 ? p0 : 1;
			uint32_t b = p1 ? p1 : TERM_ROWS;

			if (t < 1) t = 1;
			if (b > TERM_ROWS) b = TERM_ROWS;
			if (t >= b) { t = 1; b = TERM_ROWS; }

			term->scroll_top = t;
			term->scroll_bottom = b;
			/* Setting the region homes the cursor, which is what DECSTBM does. */
			term->cursor_y = term_row_to_buffer(term, t);
			term->cursor_x = 0;
			break;
		}

		case 'L':                                /* IL */
			term_insert_lines(term, p0);
			break;

		case 'M':                                /* DL */
			term_delete_lines(term, p0);
			break;

		default:
			/* Not implemented: swallowed, deliberately. */
			break;
	}
}

/**
 * @brief Feeds one byte to the escape parser.
 *
 * @param term Terminal the byte arrived on.
 * @param c    The byte.
 * @return 0 when the caller should handle the byte as ordinary output, 1 when it
 *         was consumed mid-sequence, and 2 when it completed one - which is the
 *         only point at which the screen can have changed.
 */
static int terminal_escape_feed(terminal_state_t *term, unsigned char c) {
	/*
	 * A C0 control aborts whatever was in progress and is then handled normally.
	 * Without this a program that emits half a sequence and stops - or crashes
	 * between the '[' and the final byte - would swallow everything printed
	 * afterwards, and the console would go silent with nothing to show why.
	 */
	if (c < 0x20) {
		term->esc_state = ESC_GROUND;
		return 0;
	}

	if (term->esc_state == ESC_ESC) {
		if (c == '[') {
			term->esc_state = ESC_CSI;
			term->esc_param_count = 0;
			/*
			 * The first slot is cleared here and not where the first digit
			 * arrives. Clearing only the count left the value behind, so the
			 * opening parameter of every sequence accumulated on top of whatever
			 * the last one had put there - "\033[7m" followed by "\033[5;10H"
			 * asked for row 75. Sequences with no parameters at all were the only
			 * ones unaffected, which is exactly which of them worked.
			 */
			term->esc_params[0] = 0;
			return 1;
		}
		/* Two-character sequences are not implemented; drop it and carry on. */
		term->esc_state = ESC_GROUND;
		return 1;
	}

	/* ESC_CSI */
	if (c >= '0' && c <= '9') {
		if (term->esc_param_count == 0) term->esc_param_count = 1;
		if (term->esc_param_count <= ESC_MAX_PARAMS) {
			uint32_t *slot = &term->esc_params[term->esc_param_count - 1];
			*slot = (*slot * 10u) + (uint32_t)(c - '0');
		}
		return 1;
	}

	if (c == ';') {
		if (term->esc_param_count == 0) term->esc_param_count = 1;
		if (term->esc_param_count < ESC_MAX_PARAMS) {
			term->esc_param_count++;
			term->esc_params[term->esc_param_count - 1] = 0;
		}
		return 1;
	}

	/* Anything else ends the sequence, implemented or not. */
	terminal_csi_dispatch(term, c);
	term->esc_state = ESC_GROUND;
	return 2;
}

/**
 * @brief Outputs a single character to the current terminal.
 * @param c The character to output.
 */
/**
 * @brief Writes one character to a named terminal.
 *
 * The body of terminal_putchar(), with the terminal passed in rather than taken
 * from current_terminal, and every write to the hardware guarded on that
 * terminal being the one on screen.
 *
 * Split out so the escape parser can be driven against a terminal nobody is
 * looking at. Testing it on the visible one is not possible in any useful sense:
 * the suite reports its own results through this same function, so the assertions
 * would scroll and move the very cursor they were about to check.
 *
 * @param term Terminal to write to.
 * @param c The character.
 */
static void terminal_putchar_on(terminal_state_t *term, char c) {
	int visible = (term == &terminals[current_terminal]);
	int old_view_offset = term->view_offset;
	int needs_full_redraw = 0;
	unsigned char uc = (unsigned char)c;

	if (uc == 0x1B) {
		term->esc_state = ESC_ESC;
		return;
	}

	if (term->esc_state != ESC_GROUND) {
		int taken = terminal_escape_feed(term, uc);

		/* Redrawn once the sequence is complete, not once per parameter digit:
		 * a full repaint is 80 by 24 cells and a cursor move is three bytes.
		 * Deferred to the end of the write on top of that - a frame is dozens of
		 * sequences and only its last state is ever seen. */
		if (taken == 2 && visible) term_refresh();
		if (taken) return;
	}

	if (c == '\b') {
		if (term->cursor_x > 0)
			term->cursor_x--;
		else if (term->cursor_y> 0) {
			term->cursor_y--;
			term->cursor_x = VGA_WIDTH - 1;
		}
		term->buffer[term->cursor_y][term->cursor_x] = vga_entry(' ', term->color);

		if (visible && term->cursor_y >= (size_t)term->view_offset && term->cursor_y < (size_t)(term->view_offset + (VGA_HEIGHT - 1))) {
			size_t screen_y = term->cursor_y - term->view_offset + 1;
			console_put_cell(term->cursor_x, screen_y, term->buffer[term->cursor_y][term->cursor_x]);
			update_cursor(term->cursor_x, screen_y);
		}
		return;
	}

	if (c == '\n') {
		term->cursor_x = 0;
		term->cursor_y++;
	}
	else {
		term->buffer[term->cursor_y][term->cursor_x] = vga_entry(c, term->color);
		
		if (visible && term->cursor_y >= (size_t)term->view_offset && term->cursor_y < (size_t)(term->view_offset + (VGA_HEIGHT - 1))) {
            size_t screen_y = term->cursor_y - term->view_offset + 1;
            console_put_cell(term->cursor_x, screen_y, term->buffer[term->cursor_y][term->cursor_x]);
        }
		
		term->cursor_x++;
		if (term->cursor_x >= VGA_WIDTH) {
			term->cursor_x = 0;
			term->cursor_y++;
		}
	}

	if (term->cursor_y >= MAX_SCROLL) {
		for (size_t y = 1; y < MAX_SCROLL; y++) {
			for (size_t x = 0; x < VGA_WIDTH; x++) {
				term->buffer[y - 1][x] = term->buffer[y][x];
			}
		}
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			term->buffer[MAX_SCROLL - 1][x] = vga_entry(' ', term->color);
		}
		term->cursor_y = MAX_SCROLL - 1;
		needs_full_redraw = 1;
	}

	/*
	 * The view follows the cursor only when the cursor leaves it.
	 *
	 * This used to recompute view_offset from cursor_y on every character, which
	 * works while the cursor can only move forward and becomes impossible once an
	 * escape sequence can put it anywhere: positioning to the top of the screen
	 * and printing a single character would have snapped the view down by
	 * twenty-three rows, so absolute addressing could never have worked at all.
	 *
	 * Sequential output is unchanged. The cursor steps past the bottom and the
	 * view follows by exactly what it followed by before, and scrolling back by
	 * hand still snaps to the cursor the moment anything is printed.
	 */
	if (term->cursor_y > (size_t)term->view_offset + (TERM_ROWS - 1))
		term->view_offset = (int)term->cursor_y - (TERM_ROWS - 1);
	else if (term->cursor_y < (size_t)term->view_offset)
		term->view_offset = (int)term->cursor_y;

	if (term->view_offset != old_view_offset)
		needs_full_redraw = 1;

	if (!visible)
		return;

	if (needs_full_redraw)
		term_refresh();
	else {
		if (term->cursor_y >= (size_t)term->view_offset && term->cursor_y < (size_t)(term->view_offset + (VGA_HEIGHT - 1))) {
			term_refresh_cursor(term->cursor_x, term->cursor_y - term->view_offset + 1);
		}
		else
			term_refresh_cursor(80, 25);
	}
}

/**
 * @brief Outputs a single character to the current terminal.
 * @param c The character to output.
 */
void	terminal_putchar(char c) {
	terminal_putchar_on(&terminals[current_terminal], c);
}

/**
 * @brief Writes a string to a named terminal, which may not be the visible one.
 *
 * Exists for the escape-sequence tests; see terminal_putchar_on().
 *
 * @param term_no Terminal index.
 * @param data Null-terminated string.
 */
void terminal_write_to(size_t term_no, const char *data) {
	if (term_no >= NUM_TERMINALS || data == 0) return;
	for (size_t i = 0; data[i] != '\0'; i++) terminal_putchar_on(&terminals[term_no], data[i]);
}

/**
 * @brief Reads a terminal's cursor position, in screen coordinates.
 *
 * Screen rather than buffer coordinates - 1-based row, 1-based column - because
 * that is the space escape sequences name, and the point of asking is to check
 * that a sequence put the cursor where it said.
 *
 * @param term_no Terminal index.
 * @param row Receives the row; may be null.
 * @param col Receives the column; may be null.
 */
void terminal_cursor_at(size_t term_no, size_t *row, size_t *col) {
	if (term_no >= NUM_TERMINALS) return;

	terminal_state_t *term = &terminals[term_no];
	if (row) *row = (term->cursor_y >= (size_t)term->view_offset)
	              ? (term->cursor_y - (size_t)term->view_offset + 1) : 0;
	if (col) *col = term->cursor_x + 1;
}

/**
 * @brief Reads one character off a terminal, in screen coordinates.
 *
 * @param term_no Terminal index.
 * @param row 1-based screen row.
 * @param col 1-based screen column.
 * @return The character, or 0 when the position is off the screen.
 */
char terminal_char_at(size_t term_no, size_t row, size_t col) {
	if (term_no >= NUM_TERMINALS) return 0;
	if (row < 1 || row > TERM_ROWS || col < 1 || col > VGA_WIDTH) return 0;

	terminal_state_t *term = &terminals[term_no];
	size_t y = (size_t)term->view_offset + (row - 1);
	if (y >= MAX_SCROLL) return 0;

	return (char)(term->buffer[y][col - 1] & 0xFF);
}

/**
 * @brief Reads the colour byte at a screen position.
 *
 * @param term_no Terminal index.
 * @param row 1-based screen row.
 * @param col 1-based screen column.
 * @return The VGA colour byte, or 0 when the position is off the screen.
 */
uint8_t terminal_color_at(size_t term_no, size_t row, size_t col) {
	if (term_no >= NUM_TERMINALS) return 0;
	if (row < 1 || row > TERM_ROWS || col < 1 || col > VGA_WIDTH) return 0;

	terminal_state_t *term = &terminals[term_no];
	size_t y = (size_t)term->view_offset + (row - 1);
	if (y >= MAX_SCROLL) return 0;

	return (uint8_t)((term->buffer[y][col - 1] >> 8) & 0xFF);
}

/**
 * @brief Resets a terminal to a known state, for tests.
 *
 * @param term_no Terminal index.
 */
void terminal_reset(size_t term_no) {
	if (term_no >= NUM_TERMINALS) return;

	terminal_state_t *term = &terminals[term_no];
	term->cursor_x = 0;
	term->cursor_y = 0;
	term->view_offset = 0;
	term->color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);
	term->esc_state = ESC_GROUND;
	term->esc_param_count = 0;
	term->saved_x = 0;
	term->saved_y = 0;
	term->scroll_top = 1;
	term->scroll_bottom = TERM_ROWS;

	for (size_t y = 0; y < MAX_SCROLL; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) term->buffer[y][x] = vga_entry(' ', term->color);
	}
}

/**
 * @brief Prints a null-terminated string to the current terminal.
 * @param data The string to print.
 */
void terminal_writestring(const char* data) {
	size_t n = 0;

	while (data[n] != '\0') n++;
	terminal_write_batch(data, n);
}

/**
 * @brief Writes a run of bytes and refreshes the screen once, at the end.
 *
 * The entry point every bulk write should use. What the screen looks like
 * halfway through one is something nobody can see, and paying for it - a full
 * repaint per escape sequence, a cursor reprogram per character - is what made a
 * full-screen program crawl.
 *
 * Nesting saves and restores rather than counting: the keyboard driver echoes
 * from interrupt context and can land in the middle of one of these.
 *
 * @param data Bytes to write.
 * @param n How many.
 */
void terminal_write_batch(const char *data, size_t n) {
	int saved = refresh_deferred;

	refresh_deferred = 1;
	for (size_t i = 0; i < n; i++) terminal_putchar(data[i]);
	refresh_deferred = saved;

	if (!refresh_deferred && refresh_owed) {
		refresh_owed = 0;
		update_screen();
	}
}

/**
 * @brief Scrolls the current terminal view up.
 */
void terminal_scroll_up(void) {
	terminal_state_t *term = &terminals[current_terminal];

	if (term->view_offset > 0) {
		term->view_offset--;
		update_screen();
	}
}

/**
 * @brief Scrolls the current terminal view down.
 */
void terminal_scroll_down(void) {
	terminal_state_t *term = &terminals[current_terminal];
	int max_offset = term->cursor_y - 23;
	
	if (max_offset < 0)
		max_offset = 0;
	
	if (term->view_offset < max_offset) {
		term->view_offset++;
		update_screen();
	}
}