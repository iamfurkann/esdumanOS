SRCS := $(wildcard *.c)

# Output tree, chosen by the caller. Defaults to building in place so that
# `make -C lib` on its own still works; the root Makefile passes a per-flavour
# directory so a test build and a production build never share an archive.
BUILD_DIR ?= .

OBJS := $(SRCS:%.c=$(BUILD_DIR)/%.o)

# Header dependencies, from the -MMD -MP the parent puts in CFLAGS.
#
# These were generated all along and never read, so a change to a header under
# include/ rebuilt every kernel object that used it and left libft's alone. The
# archive then carried objects compiled against a header that no longer existed
# in that form - which is the class of bug that shows up as a struct with the
# wrong layout, long after the change that caused it.
-include $(OBJS:.o=.d)

NAME = $(BUILD_DIR)/libc.a

override CFLAGS += -I ../include -I .

AR ?= ar

all: $(NAME)

$(NAME): $(OBJS)
	$(AR) rcs $(NAME) $(OBJS)

$(BUILD_DIR)/%.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJS) $(NAME)
