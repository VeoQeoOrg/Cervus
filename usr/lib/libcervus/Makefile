MKDIR_PATH := $(abspath $(dir $(lastword $(MAKEFILE_LIST))))
REPO_ROOT  := $(abspath $(MKDIR_PATH)/../../..)
SYSROOT    := $(REPO_ROOT)/usr/sysroot
INCDIR     := $(SYSROOT)/usr/include
LIBDIR     := $(SYSROOT)/usr/lib

CC      ?= gcc
AR      ?= ar
NASM    ?= nasm

CFLAGS  := -ffreestanding -nostdlib -static -fno-stack-protector \
           -mno-sse -mno-sse2 -mno-mmx -mno-avx -mno-avx2 \
           -mno-red-zone -fno-pie -fno-pic \
           -O0 -g -Wall -Wextra \
           -nostdinc -isystem $(INCDIR)

CFLAGS_FLOAT := -ffreestanding -nostdlib -static -fno-stack-protector \
           -mno-red-zone -fno-pie -fno-pic \
           -O0 -g -Wall -Wextra \
           -nostdinc -isystem $(INCDIR)

NASMFLAGS := -f elf64

OBJS := \
    libcervus.o \
    ctype/ctype.o \
    string/string.o \
    memory/memory.o \
    stdlib/stdlib.o \
    stdlib/strtod.o \
    stdio/stdio.o \
    stdio/scanf.o \
    time/time.o \
    signal/signal.o \
    dirent/dirent.o \
    setjmp.o

CRT     := crt0.o
LIBNAME := libcervus.a

.PHONY: all clean install

all: $(LIBNAME) $(CRT)

$(LIBNAME): $(OBJS)
	$(AR) rcs $@ $^

libcervus.o: libcervus.c
	$(CC) $(CFLAGS) -c -o $@ $<

ctype/ctype.o: ctype/ctype.c
	$(CC) $(CFLAGS) -c -o $@ $<

string/string.o: string/string.c
	$(CC) $(CFLAGS) -c -o $@ $<

memory/memory.o: memory/memory.c
	$(CC) $(CFLAGS) -c -o $@ $<

stdlib/stdlib.o: stdlib/stdlib.c
	$(CC) $(CFLAGS) -c -o $@ $<

stdlib/strtod.o: stdlib/strtod.c
	$(CC) $(CFLAGS_FLOAT) -c -o $@ $<

stdio/stdio.o: stdio/stdio.c
	$(CC) $(CFLAGS) -c -o $@ $<

stdio/scanf.o: stdio/scanf.c
	$(CC) $(CFLAGS_FLOAT) -c -o $@ $<

time/time.o: time/time.c
	$(CC) $(CFLAGS) -c -o $@ $<

signal/signal.o: signal/signal.c
	$(CC) $(CFLAGS) -c -o $@ $<

dirent/dirent.o: dirent/dirent.c
	$(CC) $(CFLAGS) -c -o $@ $<

setjmp.o: setjmp.asm
	$(NASM) $(NASMFLAGS) -o $@ $<

crt0.o: crt0.asm
	$(NASM) $(NASMFLAGS) -o $@ $<

install: all
	mkdir -p $(LIBDIR)
	cp $(LIBNAME) $(LIBDIR)/
	cp $(CRT)     $(LIBDIR)/
	@echo "[libcervus] installed into $(LIBDIR)"

clean:
	rm -f $(OBJS) $(CRT) $(LIBNAME)