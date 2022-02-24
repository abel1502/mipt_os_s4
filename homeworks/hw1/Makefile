AS=x86_64-elf-gcc
CC=x86_64-elf-gcc
LD=x86_64-elf-gcc
AS_FLAGS=-c -g
CC_FLAGS=-mno-mmx -mno-sse -mno-sse2 -fno-pie -c -g -mno-red-zone -std=gnu99 -ffreestanding -Wall -Wextra
LD_FLAGS=
OBJCOPY=objcopy
GRUB_MKRESCUE=grub-mkrescue

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

image: build
	mkdir -p isodir/boot/grub
	cp grub.cfg isodir/boot/grub
	cp kernel.bin isodir/boot
	$(GRUB_MKRESCUE) -o kernel.iso isodir
	rm -rf isodir

build:
	$(AS) $(AS_FLAGS) boot.S -o boot.o
	$(CC) $(CC_FLAGS) vga.c -o vga.o
	$(CC) $(CC_FLAGS) kernel.c -o kernel.o
	$(CC) $(CC_FLAGS) multiboot.c -o multiboot.o
	$(CC) $(CC_FLAGS) printk.c -o printk.o
	$(CC) $(CC_FLAGS) panic.c -o panic.o
	$(LD) $(LD_FLAGS) -T linker.ld -ffreestanding -O2 -nostdlib boot.o vga.o printk.o panic.o multiboot.o kernel.o -o kernel.bin

clean:
	rm -f *.o
	rm -f kernel.iso
	rm -f kernel.bin

QEMU = qemu-system-x86_64
# TODO?: -monitor stdio
# TODO: add a disk drive
QEMUOPTS = -cdrom kernel.iso -monitor stdio

qemu: image
	$(QEMU) $(QEMUOPTS)

qemu-gdb: image
	$(QEMU) $(QEMUOPTS) -s -S

qemu-nox: image
	$(QEMU) $(QEMUOPTS) -display none

qemu-gdb-nox: image
	$(QEMU) $(QEMUOPTS) -s -S -display none

.PHONY: all build clean update qemu qemu-gdb qemu-nox qemu-gdb-nox
