#/mnt/musl-for-directfs/obj/musl-gcc -g -nostartfiles -static -o fdlhelper fdlhelper.c -Wl,-T,custom.ld -e my_entry_point -mcmodel=medium -Wl,--no-relax
#../musl-for-aep/obj/musl-gcc
#-nostartfiles -static -Wl,-T,custom.ld -e my_entry_point -Wl,--no-relax

AEP ?= 0
DIRECT_LINK ?= 0

ifeq "$(AEP)" "1"
CC = ../musl-for-aep/obj/musl-gcc
LDFLAGS = -nostartfiles -static -Wl,-T,custom.ld -e my_entry_point -Wl,--no-relax
CFLAGS = -g -mcmodel=medium -DCONFIG_AEP
TARGET = aep-malloc
else
CC = gcc
LDFLAGS = -Wl,--no-relax
CFLAGS = -g -mcmodel=medium
ifeq "$(DIRECT_LINK)" "1"
TARGET = libaepmalloc.a
CFLAGS += -DAEP_DSM_DIRECT_LINK
else
TARGET = aep-malloc
endif
endif

LIBS = -latomic -lpthread
ALLOCATOR_SRC = topo.c aep-malloc.c init.c \
		level-1-alloc.c level-1-ref.c level-2-alloc.c \
		level-2-global.c level-2-ref.c mailbox.c page-queue.c \
		transfer-level-1.c transfer-level-2.c recovery.c
HEADERS = topo.h aeplib.h cxlmalloc-nodelevel.h init.h malloc-clientlevel.h tool.h recovery.h
SRC = $(ALLOCATOR_SRC)

ifeq "$(AEP)$(DIRECT_LINK)" "00"
SRC += test.c
HEADER += test.h
endif

ifeq "$(DIRECT_LINK)" "1"
OBJ = $(ALLOCATOR_SRC:.c=.direct.o)
ALLOCATOR_OBJ = $(OBJ)
else
OBJ = $(SRC:.c=.o)
ALLOCATOR_OBJ = $(ALLOCATOR_SRC:.c=.o)
endif

all: $(TARGET)

aep-malloc: $(OBJ)
	$(CC) $(LDFLAGS) -o $@ $^ $(LIBS)

libaepmalloc.a: $(ALLOCATOR_OBJ)
	$(AR) rcs $@ $^

%.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $<

%.direct.o: %.c $(HEADERS)
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f aep-malloc libaepmalloc.a *.o *.direct.o

.PHONY: all clean





