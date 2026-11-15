CC     = gcc
CFLAGS = -std=c11 -Wall -Wextra -Wpedantic -Werror -D_GNU_SOURCE -Iinclude
TARGET = minihttpd

SRCS = src/main.c src/server.c src/http.c src/files.c src/mime.c
HDRS = include/server.h include/http.h include/files.h include/mime.h
OBJS = $(SRCS:.c=.o)

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(OBJS) -o $@

src/%.o: src/%.c $(HDRS)
	$(CC) $(CFLAGS) -c $< -o $@

run: $(TARGET)
	./$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)