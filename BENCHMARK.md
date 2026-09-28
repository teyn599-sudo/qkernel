# Benchmark Results

Hardware: Intel Core i5-14400 (10 cores / 16 threads)
Motherboard: Gigabyte Z690M DDR4
RAM: 64 GB DDR4
OS: Linux 7.0.0-15-generic (KVM)
GPU: none

## Bare-metal microkernel (ON1 OS)

    === ON1 OS 64-bit BOOT ===
    [e820] 8191 MB
    [heap] HIGH base=4G size=6144M
    [pmm] free=2030 MB
    [smp] cpus online = 16

    === 16-CORE H GATE ===
    cores=16  pairs=134217728
    WALL: 336084531 cyc = 134 ms
    *** DONE ***

## Linux kernel module (/dev/g116_quantum)

    $ /tmp/qsingle
    Single H gate on 28q: 122.7 ms

## Bell state verification

    === CNOT(0->1) step ===
    amp[0] (|00>) = +759250125
    amp[1] (|01>) = +0
    amp[2] (|10>) = +0
    amp[3] (|11>) = +759250125
    |00> = |11> (symmetric):   OK
    |01> = |10> = 0 (anti-corr): OK

## GHZ state verification (3 qubit)

    |000> has value:  OK
    |001> = 0:        OK
    |010> = 0:        OK
    |011> = 0:        OK
    |100> = 0:        OK
    |101> = 0:        OK
    |110> = 0:        OK
    |111> has value:  OK
    |000> = |111>:    OK

## Measurement collapse

    Before: amp[0] = +759250125
    Before: amp[1] = +759250125
    Measured qubit 0 = 1
    After:  amp[0] = +0
    After:  amp[1] = +1073763836 (QONE)

## 100-run measurement statistics (H gate)

    Measured 0: 46 times
    Measured 1: 54 times

## GHZ full measurement (50 runs)

    000 (index 0): 24 times
    111 (index 7): 26 times
    Other outcomes: 0
