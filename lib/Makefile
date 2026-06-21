SRCS := $(wildcard *.c)
OBJS := $(SRCS:.c=.o)

NAME = libc.a

override CFLAGS += -I ../include -I .

all: $(NAME)

$(NAME): $(OBJS)
	ar rcs $(NAME) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJS) $(NAME)