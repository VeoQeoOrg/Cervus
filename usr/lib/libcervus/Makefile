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

NASMFLAGS := -f elf64

OBJS    := libcervus.o
CRT     := crt0.o
LIBNAME := libcervus.a

.PHONY: all clean install

all: $(LIBNAME) $(CRT)

$(LIBNAME): $(OBJS)
	$(AR) rcs $@ $^

libcervus.o: libcervus.c
	$(CC) $(CFLAGS) -c -o $@ $<

crt0.o: crt0.asm
	$(NASM) $(NASMFLAGS) -o $@ $<

install: all
	mkdir -p $(LIBDIR)
	cp $(LIBNAME) $(LIBDIR)/
	cp $(CRT)     $(LIBDIR)/
	@echo "[libcervus] installed into $(LIBDIR)"

clean:
	rm -f $(OBJS) $(CRT) $(LIBNAME)
