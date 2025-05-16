# Compiler and flags
CC = cc
CFLAGS = -Wall -Wextra -std=c11 -pedantic -ggdb $(shell pkg-config --cflags sdl2 SDL2_ttf)
LIBS = $(shell pkg-config --libs sdl2 SDL2_ttf) -lm

# Source files
SRCS = main.c vec.c glyph.c gap.c line.c file.c
OBJS = $(SRCS:.c=.o)
TARGET = text

# Build rules
all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $(OBJS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) $(TARGET)

.PHONY: all clean