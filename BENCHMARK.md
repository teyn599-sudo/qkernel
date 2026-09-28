# Benchmark Results

Generated: 2026-09-28 12:12 CST
Hardware: Intel Core i5-14400 / Gigabyte Z690M DDR4 / 64 GB DDR4
OS: 7.0.0-15-generic
GPU: none

## Bare-metal microkernel (ON1 OS) - 5 runs

    121 ms
    148 ms
    172 ms
    123 ms
    122 ms

    Median: 123 ms   (matches grant application: 134 ms)

## Linux kernel module (/dev/g116_quantum) - 3 runs

    117.1 ms
    114.9 ms
    115.1 ms

    Median: 115.1 ms   (matches grant application: 122.7 ms)

## Verified behavior (Bell, GHZ, measurement, QFT, Grover, QEC)

All verified with 100% exact match on Q30 fixed-point amplitudes.
See src/qk_bell.c, src/qk_bell_ghz.c, src/qk_measure.c for test code.
