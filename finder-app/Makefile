
CC ?= gcc

CFLAGS = -Wall -Werror

TARGET = writer

SRCS = writer.c

OBJS = $(SRCS:.c=.o)

all: $(TARGET)

$(TARGET): $(OBJS)
	$(CC) $(CFLAGS) -o $(TARGET) $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(TARGET) $(OBJS)

ifeq ($(CROSS_COMPILE),aarch64-none-linux-gnu-)
    CC = aarch64-none-linux-gnu-gcc
endif
