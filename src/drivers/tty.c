#include "tty.h"
#include "io.h"
#include "libft.h"

#define VGA_WIDTH 80
#define VGA_HEIGHT 25
#define MAX_SCROLL 100
#define NUM_TERMINALS 3

typedef struct {
	size_t cursor_x;
	size_t cursor_y;
	int view_offset;
	uint8_t color;
	uint16_t buffer[MAX_SCROLL][VGA_WIDTH];
} terminal_state_t;

terminal_state_t terminals[NUM_TERMINALS];
size_t current_terminal = 0;
volatile uint16_t* vga_memory = (uint16_t*) 0xB8000;

static inline uint8_t vga_entry_color(enum vga_color fg, enum vga_color bg) {
	return fg | bg << 4;
}

static inline uint16_t vga_entry(unsigned char uc, uint8_t color) {
	return (uint16_t) uc | (uint16_t) color << 8;
}

void	update_cursor(size_t x, size_t y) {
	uint16_t pos = y * VGA_WIDTH + x;

	outb(0x3D4, 0x0F);
	outb(0x3D5, (uint8_t) (pos & 0xFF));

	outb(0x3D4, 0x0E);
	outb(0x3D5, (uint8_t) ((pos >> 8) & 0xFF));
}

void terminal_setcolor(uint8_t fg, uint8_t bg) {
	terminals[current_terminal].color = vga_entry_color(fg, bg);
}

void draw_status_bar(const char* left, const char* right) {
	uint8_t color = vga_entry_color(VGA_COLOR_WHITE, VGA_COLOR_BLUE);

	for (int x = 0; x < VGA_WIDTH; x++) 
		vga_memory[x] = vga_entry(' ', color);

	//left
	for (int i = 0; left[i] != '\0'; i++)
		vga_memory[i] = vga_entry(left[i], color);
	
	//right -strlen-
	int len = 0;
	while (right[len])
		len++;

	for (int i = 0; i < len; i++)
		vga_memory[VGA_WIDTH - len + i] = vga_entry(right[i], color);
}

void update_screen(void) {
	terminal_state_t *term = &terminals[current_terminal];

	for (size_t y = 1; y < VGA_HEIGHT; y++) {
		for (size_t x = 0; x < VGA_WIDTH; x++) {
			vga_memory[y * VGA_WIDTH + x] = term->buffer[term->view_offset + (y - 1)][x];
		}
	}

	if (term->cursor_y >= (size_t)term->view_offset && term->cursor_y < (size_t)(term->view_offset + 24)) {
		update_cursor(term->cursor_x, term->cursor_y - term->view_offset + 1);
	}
	else {
		update_cursor(80, 25);
	}
}

void terminal_switch(size_t term_no) {
	if (term_no >= NUM_TERMINALS) return;
	current_terminal = term_no;
	update_screen();
}

void	terminal_initialize(void) {
	for (size_t i = 0; i < NUM_TERMINALS; i++) {
		terminals[i].cursor_x = 0;
		terminals[i].cursor_y = 0;
		terminals[i].view_offset = 0;
		terminals[i].color = vga_entry_color(VGA_COLOR_LIGHT_GREY, VGA_COLOR_BLACK);

		for (size_t y = 0; y < MAX_SCROLL; y++) {
			for (size_t x = 0; x < VGA_WIDTH; x++) {
				terminals[i].buffer[y][x] = vga_entry(' ', terminals[i].color);
			}
		}
	}
	terminal_switch(0);
}

void	terminal_putchar(char c) {
	terminal_state_t *term = &terminals[current_terminal];
	int old_view_offset = term->view_offset;
	int needs_full_redraw = 0;

	if (c == '\b') {
		if (term->cursor_x > 0)
			term->cursor_x--;
		else if (term->cursor_y> 0) {
			term->cursor_y--;
			term->cursor_x = VGA_WIDTH - 1;
		}
		term->buffer[term->cursor_y][term->cursor_x] = vga_entry(' ', term->color);

		if (term->cursor_y >= (size_t)term->view_offset && term->cursor_y < (size_t)(term->view_offset + 24)) {
			size_t screen_y = term->cursor_y - term->view_offset + 1;
			vga_memory[screen_y * VGA_WIDTH + term->cursor_x] = term->buffer[term->cursor_y][term->cursor_x];
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
		
		if (term->cursor_y >= (size_t)term->view_offset && term->cursor_y < (size_t)(term->view_offset + 24)) {
            size_t screen_y = term->cursor_y - term->view_offset + 1;
            vga_memory[screen_y * VGA_WIDTH + term->cursor_x] = term->buffer[term->cursor_y][term->cursor_x];
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

	if (term->cursor_y >= 24)
		term->view_offset = term->cursor_y - 23;
	else
		term->view_offset = 0;

	if (term->view_offset != old_view_offset)
		needs_full_redraw = 1;

	if (needs_full_redraw)
		update_screen();
	else {
		if (term->cursor_y >= (size_t)term->view_offset && term->cursor_y < (size_t)(term->view_offset + 24)) {
			update_cursor(term->cursor_x, term->cursor_y - term->view_offset + 1);
		}
		else
			update_cursor(80, 25);
	}
}

void terminal_writestring(const char* data) {
	for (size_t i = 0; data[i] != '\0'; i++)
		terminal_putchar(data[i]);
}

void terminal_scroll_up(void) {
	terminal_state_t *term = &terminals[current_terminal];

	if (term->view_offset > 0) {
		term->view_offset--;
		update_screen();
	}
}

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