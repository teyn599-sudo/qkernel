# Four-Gate Fusion (H-X-CNOT-H)

Composing four gates into a single 4x4 matrix and applying it in one pass.

## 28 qubits (2 GB state vector)

| Method | Time | Speedup |
|---|---|---|
| Serial (4 separate ioctls) | 519 / 520 / 525 ms | 1.0x |
| Fused (1 ioctl) | 113.1 / 111.4 / 112.2 ms | **4.6x** |

Median: 112 ms fused, 520 ms serial.

## 31 qubits (16 GB state vector)

| Method | Time | Speedup |
|---|---|---|
| Serial (4 gates) | 4114 ms | 1.0x |
| Fused (1 op) | 1948 ms | **2.13x** |

At 31q the matrix arithmetic cost dominates, so fusion gains less than at 28q.

## 20 qubits (all in L3)

Fused (1 op): 0.1 ms — the entire 8 MB state vector lives in L3 cache.

## Why it works

Each gate reads and writes the full state vector. Four gates = four
memory sweeps. Fusion composes the four 4x4 matrices into one 4x4
matrix, so the state vector is touched once.

At 28q, memory bandwidth is the bottleneck (91% of DDR4-3200 dual-channel
peak), so eliminating 3 of 4 sweeps gives 4.6x.

At 31q, the 4x4 matrix multiply per amplitude starts to dominate, so
the gain drops to 2.13x.

## Source

`src/g116_quantum.c`, ioctl `Q_APPLY_FUSED_HXCNOTH`.
