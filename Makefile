TARGET := aarch64-none-elf
CC := clang
QEMU := qemu-system-aarch64

CFLAGS := --target=$(TARGET) -std=c11 -O2 -g \
	-ffreestanding -fno-builtin -fno-stack-protector \
	-mgeneral-regs-only -Wall -Wextra -Werror
ASFLAGS := --target=$(TARGET) -g
LDFLAGS := --target=$(TARGET) -fuse-ld=lld -nostdlib \
	-Wl,-T,linker_arm64.ld -Wl,--build-id=none

.PHONY: all clean run demo-video bcs-walkthrough

all: kernel.elf

OBJECTS := boot.o kernel.o graphics.o virtio.o fdt.o desktop.o exceptions.o exceptions_asm.o

kernel.elf: $(OBJECTS) linker_arm64.ld
	$(CC) $(LDFLAGS) -o $@ $(OBJECTS)

kernel.o graphics.o virtio.o fdt.o desktop.o exceptions.o: %.o: %.c os.h
	$(CC) $(CFLAGS) -c $< -o $@

boot.o: boot.S
	$(CC) $(ASFLAGS) -c $< -o $@

exceptions_asm.o: exceptions.S
	$(CC) $(ASFLAGS) -c $< -o $@

document.raw:
	truncate -s 1M $@

run: kernel.elf document.raw
	$(QEMU) -M virt -cpu cortex-a72 -m 128M \
		-global virtio-mmio.force-legacy=false \
		-device ramfb \
		-device virtio-keyboard-device \
		-device virtio-tablet-device \
		-drive if=none,id=document,file=document.raw,format=raw,cache=directsync \
		-device virtio-blk-device,drive=document \
		-kernel kernel.elf \
		-serial mon:stdio

demo-video: kernel.elf
	python3 scripts/record_demo.py

bcs-walkthrough: kernel.elf
	python3 scripts/bcs_walkthrough.py

clean:
	rm -f $(OBJECTS) kernel.elf desktop.ppm

