CFLAGS = -m64 -ffreestanding -fno-pie -fno-stack-protector -Wall \
         -fno-builtin -O2 -mno-red-zone -fno-asynchronous-unwind-tables \
         -mno-sse -mno-sse2 -mno-mmx -mno-80387
LDFLAGS = -m64 -T linker64.ld -nostdlib -ffreestanding -no-pie

OBJS = boot64.o ap_trampoline.o interrupts64.o idt64.o smp.o \
       kernel64.o mm64.o noise64.o qft64.o grover64.o qec64.o demo64.o circuit64.o static64.o

all: on1_64.iso
%.o: %.c
	gcc $(CFLAGS) -c $< -o $@
iso/boot/on1_64: $(OBJS)
	gcc $(LDFLAGS) -o $@ $(OBJS) -lgcc
on1_64.iso: iso/boot/on1_64
	grub-mkrescue -o $@ iso 2>/dev/null
run: on1_64.iso
	sudo pkill -9 qemu-system 2>/dev/null; sleep 1
	rm -f ~/on1_64.log
	sudo qemu-system-x86_64 -machine q35,accel=kvm,kernel_irqchip=on \
	    -cpu host,+invtsc -smp 16 -m 8192 -mem-path /mnt/huge -mem-prealloc \
	    -cdrom on1_64.iso -vnc :2 -serial file:$$HOME/on1_64.log >/dev/null 2>&1 &
	sleep 8
	grep -E "qft|grover|qec|noise|WALL|DONE" ~/on1_64.log
	sudo pkill -9 qemu-system
clean:
	rm -f *.o iso/boot/on1_64 on1_64.iso

static64.o: static.bin
	objcopy -I binary -O elf64-x86-64 -B i386:x86-64 $< $@

.PHONY: all clean run

