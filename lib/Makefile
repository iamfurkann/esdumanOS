SRCS := $(wildcard *.c)
OBJS := $(SRCS:.c=.o)

NAME = libc.a

override CFLAGS += -I ../include -I .

AR ?= ar

all: $(NAME)

$(NAME): $(OBJS)
	$(AR) rcs $(NAME) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJS) $(NAME)