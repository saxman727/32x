ifdef $(GENDEV)
ROOTDIR = $(GENDEV)
else
ROOTDIR = /opt/toolchains/sega
endif

MDLD = $(ROOTDIR)/m68k-elf/bin/m68k-elf-ld
MDAS = $(ROOTDIR)/m68k-elf/bin/m68k-elf-as
SHLD = $(ROOTDIR)/sh-elf/bin/sh-elf-ld
SHCPP = $(ROOTDIR)/sh-elf/bin/sh-elf-g++
SHCC = $(ROOTDIR)/sh-elf/bin/sh-elf-gcc
SHAS = $(ROOTDIR)/sh-elf/bin/sh-elf-as
SHOBJC = $(ROOTDIR)/sh-elf/bin/sh-elf-objcopy
RM = rm -f

CPPFLAGS = -m2 -mb -O3 -Wall -c -fno-exceptions -nostartfiles -ffreestanding -fno-rtti
CCFLAGS = -m2 -mb -O3 -Wall -c -fomit-frame-pointer
HWCCFLAGS = -m2 -mb -O1 -Wall -c -fomit-frame-pointer
LINKFLAGS = -T $(ROOTDIR)/ldscripts/mars.ld -Wl,-Map=output.map -nostdlib -ffreestanding -fno-rtti

INCS = -I. -I$(ROOTDIR)/sh-elf/include -I$(ROOTDIR)/sh-elf/sh-elf/include

LIBS = -L$(ROOTDIR)/sh-elf/sh-elf/lib -L$(ROOTDIR)/sh-elf/lib/gcc/sh-elf/9.1.0 -lstdc++ -lc -lgcc -lgcc-Os-4-200 -lnosys

OBJS = sh2_crt0.o crtstuff.o main.o hw_32x.o font.o palettes.o art.o bmap.o meta.o flayout.o blayout.o

all: m68k_crt0.bin m68k_crt1.bin ProSonic32X.bin

ProSonic32X.bin: ProSonic.elf
	$(SHOBJC) -O binary $< temp.bin
	dd if=temp.bin of=$@ bs=64K conv=sync

ProSonic.elf: $(OBJS)
	$(SHCPP) $(LINKFLAGS) $(OBJS) $(LIBS) -o ProSonic.elf

m68k_crt0.bin: m68k_crt0.s
	$(MDAS) -m68000 --register-prefix-optional -o m68k_crt0.o m68k_crt0.s
	$(MDLD) -T $(ROOTDIR)/ldscripts/md.ld --oformat binary -o m68k_crt0.bin m68k_crt0.o

m68k_crt1.bin: m68k_crt1.s
	$(MDAS) -m68000 --register-prefix-optional -o m68k_crt1.o m68k_crt1.s
	$(MDLD) -T $(ROOTDIR)/ldscripts/md.ld --oformat binary -o m68k_crt1.bin m68k_crt1.o

hw_32x.o: hw_32x.c
	$(SHCC) $(HWCCFLAGS) $< -o $@

%.o: %.cpp
	$(SHCPP) $(CPPFLAGS) $< -o $@

%.o: %.c
	$(SHCC) $(CCFLAGS) $< -o $@

%.o: %.s
	$(SHAS) --small -o $@ $<

clean:
	$(RM) *.o *.bin *.elf *.map
