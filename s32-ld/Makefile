CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
LDFLAGS =

SRCS = main.c linker.c
OBJS = $(SRCS:.c=.o)
TARGET = s32-ld

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET).exe
