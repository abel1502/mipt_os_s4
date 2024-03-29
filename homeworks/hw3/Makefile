SHELL := /bin/bash

AS=x86_64-elf-gcc
CC=x86_64-elf-gcc
LD=x86_64-elf-ld
OBJCOPY=x86_64-elf-objcopy
SED=sed
GRUB_MKRESCUE=grub-mkrescue

ROOT=$(shell pwd)
ASFLAGS=-g -I$(ROOT)
CCFLAGS=-I$(ROOT) -mno-mmx -mno-sse -mno-sse2 -maddress-mode=long -mcmodel=kernel -g -m64 -mno-red-zone -std=c11 -ffreestanding -fno-common -Wall -Wextra -nostdlib
LDFLAGS=-nostdlib --no-dynamic-linker --warn-constructors --warn-common --no-eh-frame-hdr --fatal-warnings
export

all: .git/hooks/post-checkout qemu

define POST_CHECKOUT
#!/bin/sh -x
make clean
endef
export POST_CHECKOUT

.git/hooks/post-checkout:
	@echo "$$POST_CHECKOUT" > $@
	@chmod +x $@

update:
	../tools/update.sh

kernel.iso: kernel.bin
	mkdir -p isodir/boot/grub
	cp grub.cfg isodir/boot/grub
	cp kernel.bin isodir/boot
	$(GRUB_MKRESCUE) -o kernel.iso isodir
	rm -rf isodir

kernel.bin:
	$(MAKE) -C boot/
	$(MAKE) -C drivers/
	$(MAKE) -C kernel/
	$(MAKE) -C mm/
	$(LD) $(LDFLAGS) -T <(cpp -P -E linker.ld) `find $(ROOT) -name '*.o'` -o kernel.bin
	$(OBJCOPY) --only-keep-debug kernel.bin kernel.sym
	$(OBJCOPY) --strip-debug kernel.bin

clean:
	$(MAKE) -C boot/ clean
	$(MAKE) -C drivers/ clean
	$(MAKE) -C kernel/ clean
	$(MAKE) -C mm/ clean
	rm -f kernel.bin
	rm -f kernel.sym
	rm -f kernel.iso


QEMU = qemu-system-x86_64
# TODO?: -monitor stdio
# TODO: add a disk drive
QEMUOPTS = -cdrom kernel.iso -monitor stdio

qemu: kernel.iso
	$(QEMU) $(QEMUOPTS)

qemu-gdb: kernel.iso
	$(QEMU) $(QEMUOPTS) -s -S

qemu-nox: kernel.iso
	$(QEMU) $(QEMUOPTS) -display none

qemu-gdb-nox: kernel.iso
	$(QEMU) $(QEMUOPTS) -s -S -display none

.PHONY: all kernel.bin clean update qemu qemu-gdb qemu-nox qemu-gdb-nox

