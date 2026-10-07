# QKernel

A bare-metal + Linux-kernel quantum state-vector simulator with
Q30 fixed-point arithmetic.

## Hardware

Intel Core i5-14400, 64 GB DDR4, no GPU.

## Performance (28 qubits, 2 GB state vector)

| Operation | Time | Notes |
|---|---|---|
| Single H gate | 108 ms | 16 cores |
| 4 gates, serial | 517 ms | 4 separate ioctls |
| **4 gates, fused** | **113 ms** | single ioctl, 16 cores |
| 31-qubit single H | 854 ms | 8 GB state vector |

The fused version composes H-X-CNOT-H into a single 4x4 matrix and
applies it in one pass. Speedup over serial: 4.57x.

Memory bandwidth utilization: 91% of DDR4-3200 dual-channel peak.

## QAOA MaxCut (4 nodes, 5 edges)

Optimal = 4.0

| Layers p | <H_C> | Ratio |
|---|---|---|
| 1 | 3.2371 | 80.9% |
| 2 | 3.5561 | 88.9% |
| 3 | 3.9870 | 99.7% |
| 4 | 3.9990 | 100.0% |
| 5 | 4.0000 | 100.0% |

Method: warm-start (each layer initializes from layer p-1's solution),
50 random restarts per layer, momentum SGD with learning rate decay.

Without warm start, optimization collapses at p >= 5 (barren plateau).

## VQE

Tested on 2/4/6/8 qubits with H = -sum Z_i + sum X_i X_{i+1}.

| qubits | params | iters | error |
|---|---|---|---|
| 2 | 4 | 156 | 2.9e-08 |
| 4 | 16 | 527 | 4.4e-04 |
| 6 | 36 | 2924 | 1.6e-03 |
| 8 | 64 | 1409 | 4.1e-04 |

Under shot noise (1000 shots/eval, fixed budget):
- SPSA vs SGD: 550x / 195x / 43x faster (2q/4q/6q)
- SPSA vs coordinate descent: 2.5x / 4x

## Source

- `src/kernel64.c` — bare-metal microkernel (ON1 OS)
- `src/g116_quantum.c` — Linux kernel module
- `src/qaoa2.c` — QAOA MaxCut
- `src/qft64.c`, `grover64.c`, `qec64.c`, `noise64.c`
- `src/qk_bell.c`, `qk_bell_ghz.c`, `qk_measure.c` — userspace tests

## Build

cd src
make
sudo insmod g116_quantum.ko
sudo chmod 666 /dev/g116_quantum

## License

MIT (bare-metal and userspace code), GPLv2 (Linux kernel module).

## Documentation

- [BENCHMARK.md](BENCHMARK.md) — raw benchmark output
- [FUSION.md](FUSION.md) — 4-gate fusion
- [VQE.md](VQE.md) — VQE basics
- [QAOA.md](QAOA.md) — QAOA MaxCut results
