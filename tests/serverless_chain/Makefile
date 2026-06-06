CC ?= gcc
DIRECT_LINK ?= 0
ALLOCATOR_DIR := ../../allocator
CFLAGS ?= -O2 -g -Wall -Wextra -pthread -I../aep_dsm_client
LDFLAGS ?= -pthread
TARGET := serverless_chain

ifeq "$(DIRECT_LINK)" "1"
CFLAGS += -DAEP_DSM_DIRECT_LINK
ALLOCATOR_LIB := $(ALLOCATOR_DIR)/libaepmalloc.a
LDFLAGS += $(ALLOCATOR_LIB) -latomic
else
ALLOCATOR_LIB :=
endif

all: $(TARGET)

$(ALLOCATOR_DIR)/libaepmalloc.a: FORCE
	$(MAKE) -C $(ALLOCATOR_DIR) DIRECT_LINK=1 libaepmalloc.a

FORCE:

$(TARGET): serverless_chain.c $(ALLOCATOR_LIB)
	$(CC) $(CFLAGS) -o $@ $< $(LDFLAGS)

clean:
	rm -f $(TARGET)

.PHONY: all clean FORCE
