CC = ..\s32-cc\s32-cc.exe
LD = ..\s32-ld\s32-ld.exe
SIM = ..\sim_s32.exe

OBJS = start.o main.o console.o printk.o uart.o string.o \
       kalloc.o spinlock.o proc.o trap.o

all: kernel.bin

%.o: %.c
	$(CC) -c -o $@ $<

kernel.elf: $(OBJS)
	$(LD) -o $@ -e _start $(OBJS)

kernel.bin: kernel.elf
	$(LD) --oformat binary -o $@ kernel.elf

run: kernel.bin
	$(SIM) kernel.bin

clean:
	del /q *.o kernel.elf kernel.bin 2>nul
