CC ?= gcc
CFLAGS ?= -Wall -Werror -g -O2
LDFLAGS ?= -lpthread
TARGET = aesdsocket
SOURCES = aesdsocket.c

# Cross compilation support
ifdef CROSS_COMPILE
	CC = $(CROSS_COMPILE)gcc
endif

.PHONY: all default clean

all: $(TARGET)

default: $(TARGET)

$(TARGET): $(SOURCES)
	$(CC) $(CFLAGS) -o $(TARGET) $(SOURCES) $(LDFLAGS)

clean:
	rm -f $(TARGET)