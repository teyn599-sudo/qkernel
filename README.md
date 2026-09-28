# QKernel

QKernel is a quantum simulation stack written from scratch — with no Python runtime overhead, no CUDA dependence, and no external frameworks. It uses a uniform Q30 fixed-point arithmetic engine across two deployment targets:

## 1. Bare-metal microkernel (ON1 OS)

A 64-bit microkernel booted via GRUB Multiboot2, featuring 4-level identity page tables, hugepage-backed heap, and 16-core SMP via AP trampoline. It executes a 28-qubit H-gate in **134.0 ms**.

## 2. Linux kernel module (g116_quantum.ko)

Exposes a character device /dev/g116_quantum with zero-copy ioctl interfaces for standard gates (H/X/Y/Z/S/T, CNOT, Toffoli, QFT, Grover, QEC). Utilizing workqueue parallelism across 16 cores, it runs a 28-qubit H-gate in **122.7 ms**.

The design goal is to bring 28-qubit state-vector simulation (2^28 = 268M complex amplitudes, 2 GB int32 storage) directly to consumer desktop hardware without relying on GPU clusters or expensive cloud infrastructure.

## Benefit to the Ecosystem & Benchmark Proof

High-qubit quantum simulation (28+ qubits) typically requires server clusters or enterprise GPUs. QKernel demonstrates that raw hardware speed can be achieved directly on standard desktop hardware.

### Test Hardware Setup
- CPU: Intel Core i5-14400 (10 cores / 16 threads)
- Motherboard: Gigabyte Z690M DDR4
- RAM: 64 GB DDR4
- Virtualization: KVM (No GPU used)

### Performance Benchmark (28-Qubit Single-Qubit H-Gate / 2^27 Pair Operations)
- Linux Kernel Module (/dev/g116_quantum): **122.7 ms per gate**
- Bare-Metal Microkernel (ON1 OS): **134.0 ms per gate**

### Verified Quantum Behaviors (100% Exact Verification)
- Bell State: |00> and |11> amplitudes = +759250125 (Q30 representation of 1/sqrt(2)); |01> and |10> = exactly 0.
- GHZ State & Collapse: Exact 3-qubit entanglement; post-measurement collapse verified with 100% precision.
- Quantum Algorithms: Full verification of QFT uniform superposition, 4-qubit Grover search (target amplitude grows from 6% to 97% in 3 iterations), and 3-qubit bit-flip QEC.

## Source

- src/kernel64.c, qft64.c, grover64.c, qec64.c, noise64.c, demo64.c — bare-metal microkernel
- src/g116_quantum.c — Linux kernel module
- src/qk_bell.c, qk_bell_ghz.c, qk_measure.c — userspace tests

## License

QKernel is released under the **GNU General Public License v3.0 (GPLv3)**. See the `LICENSE` file for the full text.

## Commercial Licensing

For commercial use that cannot comply with GPLv3 — for example, proprietary redistribution or embedding in closed-source products — a separate commercial license is available.

Contact: teyn599@gmail.com
