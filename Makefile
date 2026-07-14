TARGET := aarch64-none-elf
CC := clang
QEMU := qemu-system-aarch64

CFLAGS := --target=$(TARGET) -std=c11 -O2 -g \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-mgeneral-regs-only -Wall -Wextra -Werror
ASFLAGS := --target=$(TARGET) -g
LDFLAGS := --target=$(TARGET) -fuse-ld=lld -nostdlib \
	-Wl,-T,linker_arm64.ld -Wl,--build-id=none

.PHONY: all clean run

all: kernel.elf

kernel.elf: boot.o kernel.o linker_arm64.ld
	$(CC) $(LDFLAGS) -o $@ boot.o kernel.o

kernel.o: kernel.c
	$(CC) $(CFLAGS) -c $< -o $@

boot.o: boot.S
	$(CC) $(ASFLAGS) -c $< -o $@

run: kernel.elf
	$(QEMU) -M virt -cpu cortex-a72 -m 128M \
		-device ramfb -kernel kernel.elf \
		-serial mon:stdio

clean:
	rm -f boot.o kernel.o kernel.elf desktop.ppm

