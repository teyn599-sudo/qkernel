# ON1 OS — Bare-Metal Microkernel

A 64-bit x86-64 microkernel with no OS underneath.

## Architecture
- GRUB Multiboot2 bootloader
- GDT / IDT / PIT 100 Hz
- 4-level identity page tables
- E820 memory detection
- Hugepage-backed heap (2 MB pages)
- 16-core SMP via AP trampoline (16 → 32 → 64 bit)
- Framebuffer 1024x768

## Benchmark (28 qubits, 16 cores)
- 28-qubit H gate: 123 ms

Implementation: src/kernel64.c
