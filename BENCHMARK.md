# Benchmark Results

Hardware: Intel Core i5-14400 / Gigabyte Z690M DDR4 / 64 GB DDR4
OS: Linux 7.0.0-15-generic
GPU: none

## Bare-metal microkernel (ON1 OS) — 5 runs

    121 / 148 / 172 / 123 / 122 ms
    Median: 123 ms

## Linux kernel module (/dev/g116_quantum)

### Single-qubit H gate

| Qubits | Time | Note |
|---|---|---|
| 28 | 108 ms | 2 GB state vector |
| 30 | 456 ms | 8 GB |
| 31 | 854 ms | 16 GB |

### Four-gate fusion (H-X-CNOT-H)

#### 28 qubits

| Method | Time (median of 3) |
|---|---|
| Serial (4 gates) | 520 ms |
| Fused (1 op) | 112 ms |
| **Speedup** | **4.6x** |

#### 31 qubits

| Method | Time |
|---|---|
| Serial (4 gates) | 4114 ms |
| Fused (1 op) | 1948 ms |
| Speedup | 2.13x |

#### 20 qubits (fits in L3)

Fused (1 op): 0.1 ms

## Verified quantum behavior

Bell state, GHZ state, measurement collapse, QFT, Grover (97% in 3 iter),
3-qubit QEC, QAOA MaxCut (p=4, 100%), VQE (2/4/6/8 qubit).

See `QAOA.md`, `VQE.md`, `FUSION.md` for details.

## Hardware limits

- 28q state vector: 2 GB
- Memory bandwidth utilization: 105% of DDR4-3200 dual-channel peak (with RFO)
- Bottleneck: memory bandwidth (dual channel)
- IPC: 2.33 (perf measured)
