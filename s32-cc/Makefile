CC = gcc
CFLAGS = -Wall -Wextra -O2 -std=c11
LDFLAGS =

SRCS = main.c lexer.c ast.c symtable.c parser.c codegen.c
OBJS = $(SRCS:.c=.o)
TARGET = s32-cc

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f $(OBJS) $(TARGET) $(TARGET).exe
