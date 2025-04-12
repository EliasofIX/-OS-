ARCH ?= arm64

ifeq ($(ARCH),arm64)
    CC=/opt/homebrew/bin/aarch64-elf-gcc
    LD=/opt/homebrew/bin/aarch64-elf-ld
    CFLAGS=-ffreestanding -nostdlib -fno-builtin -fno-stack-protector
    LDFLAGS=-T linker_arm64.ld
endif

all:
	$(CC) $(CFLAGS) -c kernel.c -o kernel.o
	$(CC) $(CFLAGS) -c boot.S -o boot.o
	$(LD) $(LDFLAGS) -o kernel.elf boot.o kernel.o

clean:
	rm -f *.o kernel.elf

