TARGET := build/UfpFfu.efi
CC := clang
LD := lld-link
CFLAGS := --target=aarch64-pc-windows-msvc -ffreestanding -fshort-wchar -fno-stack-protector -fno-builtin -Wall -Wextra -Werror -Iinclude
LDFLAGS := /subsystem:efi_application /entry:EfiMain /machine:arm64 /nodefaultlib

SRCS := src/uefi_min.c src/ufp_proto.c src/ffu.c src/usb_transport.c src/main.c
OBJS := $(SRCS:src/%.c=build/%.obj)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(OBJS)
	$(LD) $(LDFLAGS) /out:$@ $^

build/%.obj: src/%.c include/*.h | build
	$(CC) $(CFLAGS) -c $< -o $@

build:
	mkdir -p build

clean:
	rm -f build/*.obj $(TARGET)
