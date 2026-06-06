CC ?= gcc
CFLAGS ?= -O2 -g -Wall -Wextra
LDFLAGS ?=
TARGET := cxl_map

all: $(TARGET)

$(TARGET): cxl_map.c
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean
