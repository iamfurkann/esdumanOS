SRCS := $(wildcard *.c)
OBJS := $(SRCS:.c=.o)

NAME = libc.a

# Ana Makefile'dan gelen CFLAGS'in üzerine, 
# üst dizindeki include'ları ve mevcut dizini (.) ekliyoruz.
override CFLAGS += -I ../../include -I .

all: $(NAME)

$(NAME): $(OBJS)
	ar rcs $(NAME) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) $< -o $@

clean:
	rm -f $(OBJS) $(NAME)