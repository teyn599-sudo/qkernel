/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define Q_INIT_ZERO   _IO('Q', 1)
#define Q_APPLY_H_Q   _IOW('Q', 3, unsigned int)
#define Q_APPLY_CNOT  _IOW('Q', 4, unsigned int)
#define Q_GET_AMPL    _IOWR('Q', 11, struct qampl_req)
struct qampl_req { unsigned int idx; int64_t re_q30; int64_t im_q30; };

static int64_t read_ampl(int fd, unsigned int idx) {
    struct qampl_req r = { .idx = idx };
    ioctl(fd, Q_GET_AMPL, &r);
    return r.re_q30;
}

int main(void) {
    int fd = open("/dev/g116_quantum", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    unsigned int q0 = 0, q1 = 1;
    unsigned int cnot01 = (0u << 8) | 1u;

    // ── 步驟 1：|00...0⟩ ──
    ioctl(fd, Q_INIT_ZERO);
    printf("=== 步驟 1：初始態 |00...0⟩ ===\n");
    printf("  amp[0] = %+lld\n", (long long)read_ampl(fd, 0));
    printf("  amp[1] = %+lld\n", (long long)read_ampl(fd, 1));
    printf("  amp[2] = %+lld\n", (long long)read_ampl(fd, 2));
    printf("  amp[3] = %+lld\n\n", (long long)read_ampl(fd, 3));

    // ── 步驟 2：對 qubit 0 做 H ──
    ioctl(fd, Q_APPLY_H_Q, &q0);
    printf("=== 步驟 2：H(0) → (|0⟩+|1⟩)/√2 ⊗ |0...0⟩ ===\n");
    printf("  amp[0] = %+lld   ← 期望 +759250125\n", (long long)read_ampl(fd, 0));
    printf("  amp[1] = %+lld\n", (long long)read_ampl(fd, 1));
    printf("  amp[2] = %+lld   ← 期望 +759250125\n", (long long)read_ampl(fd, 2));
    printf("  amp[3] = %+lld\n\n", (long long)read_ampl(fd, 3));

    // ── 步驟 3：CNOT(0→1)，產生貝爾態 ──
    ioctl(fd, Q_APPLY_CNOT, &cnot01);
    printf("=== 步驟 3：CNOT(0→1) → (|00⟩+|11⟩)/√2 ⊗ |0...0⟩ ===\n");
    int64_t a0 = read_ampl(fd, 0);
    int64_t a1 = read_ampl(fd, 1);
    int64_t a2 = read_ampl(fd, 2);
    int64_t a3 = read_ampl(fd, 3);
    printf("  amp[0] (|00⟩) = %+lld   ← 期望 +759250125\n", (long long)a0);
    printf("  amp[1] (|01⟩) = %+lld   ← 期望 0\n", (long long)a1);
    printf("  amp[2] (|10⟩) = %+lld   ← 期望 0\n", (long long)a2);
    printf("  amp[3] (|11⟩) = %+lld   ← 期望 +759250125\n\n", (long long)a3);

    // ── 驗證 ──
    int64_t expected = 759250125LL;   // QONE / sqrt(2)
    printf("=== 貝爾態驗證 ===\n");
    printf("  |00⟩ 有值:  %s\n", (a0 == expected)  ? "✅" : "❌");
    printf("  |01⟩ 為 0:  %s\n", (a1 == 0)         ? "✅" : "❌");
    printf("  |10⟩ 為 0:  %s\n", (a2 == 0)         ? "✅" : "❌");
    printf("  |11⟩ 有值:  %s\n", (a3 == expected)  ? "✅" : "❌");
    printf("  |00⟩ = |11⟩ (對稱): %s\n", (a0 == a3) ? "✅" : "❌");
    printf("  |01⟩ = |10⟩ = 0 (反相關): %s\n", (a1 == 0 && a2 == 0) ? "✅" : "❌");

    if (a0 == expected && a1 == 0 && a2 == 0 && a3 == expected) {
        printf("\n🎯 貝爾態建立成功：qubit 0 和 qubit 1 完全糾纏\n");
        printf("   測 qubit 0 得 0 → qubit 1 必為 0\n");
        printf("   測 qubit 0 得 1 → qubit 1 必為 1\n");
    }

    close(fd);
    return 0;
}
