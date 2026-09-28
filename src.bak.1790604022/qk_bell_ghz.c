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

static int64_t ampl(int fd, unsigned int idx) {
    struct qampl_req r = { .idx = idx };
    ioctl(fd, Q_GET_AMPL, &r);
    return r.re_q30;
}

int main(void) {
    int fd = open("/dev/g116_quantum", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    unsigned int q0 = 0, q1 = 1, q2 = 2;
    unsigned int cnot01 = (0u << 8) | 1u;   /* ctrl=0, tgt=1 */
    unsigned int cnot12 = (1u << 8) | 2u;   /* ctrl=1, tgt=2 */

    int64_t EXP = 759250125LL;   /* QONE / sqrt(2) */

    /* ══════════ 貝爾態（2 qubit） ══════════ */
    printf("╔══════════════════════════════════════════╗\n");
    printf("║  貝爾態測試 (2 qubit)                    ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    ioctl(fd, Q_INIT_ZERO);
    printf("--- 步驟 1：|00...0⟩ ---\n");
    for (int i = 0; i < 4; i++) printf("  amp[%d] = %+lld\n", i, (long long)ampl(fd, i));

    ioctl(fd, Q_APPLY_H_Q, &q0);
    printf("\n--- 步驟 2：H(0) → (|00⟩+|01⟩)/√2 ---\n");
    for (int i = 0; i < 4; i++) {
        int64_t v = ampl(fd, i);
        const char* exp = (i == 0 || i == 1) ? "← 期望 759250125" : "← 期望 0";
        printf("  amp[%d] = %+lld   %s\n", i, (long long)v, exp);
    }

    ioctl(fd, Q_APPLY_CNOT, &cnot01);
    printf("\n--- 步驟 3：CNOT(0→1) → (|00⟩+|11⟩)/√2 ---\n");
    int64_t b[4];
    for (int i = 0; i < 4; i++) {
        b[i] = ampl(fd, i);
        const char* exp = (i == 0 || i == 3) ? "← 期望 759250125" : "← 期望 0";
        printf("  amp[%d] = %+lld   %s\n", i, (long long)b[i], exp);
    }

    printf("\n--- 貝爾態驗證 ---\n");
    int bell_ok = (b[0]==EXP && b[1]==0 && b[2]==0 && b[3]==EXP);
    printf("  |00⟩ 有值:       %s\n", (b[0]==EXP) ? "✅" : "❌");
    printf("  |01⟩ 為 0:       %s\n", (b[1]==0)   ? "✅" : "❌");
    printf("  |10⟩ 為 0:       %s\n", (b[2]==0)   ? "✅" : "❌");
    printf("  |11⟩ 有值:       %s\n", (b[3]==EXP) ? "✅" : "❌");
    printf("  |00⟩ = |11⟩:     %s\n", (b[0]==b[3]) ? "✅" : "❌");
    printf("\n  %s\n\n", bell_ok ? "🎯 貝爾態成功" : "❌ 貝爾態失敗");

    /* ══════════ GHZ 態（3 qubit） ══════════ */
    printf("╔══════════════════════════════════════════╗\n");
    printf("║  GHZ 態測試 (3 qubit)                    ║\n");
    printf("║  目標：(|000⟩ + |111⟩)/√2                ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    ioctl(fd, Q_INIT_ZERO);
    printf("--- 步驟 1：|000...0⟩ ---\n");
    for (int i = 0; i < 8; i++) printf("  amp[%d] = %+lld\n", i, (long long)ampl(fd, i));

    ioctl(fd, Q_APPLY_H_Q, &q0);
    printf("\n--- 步驟 2：H(0) → (|000⟩+|001⟩)/√2 ---\n");
    for (int i = 0; i < 8; i++) {
        int64_t v = ampl(fd, i);
        const char* exp = (i == 0 || i == 1) ? "← 期望 759250125" : "← 期望 0";
        printf("  amp[%d] = %+lld   %s\n", i, (long long)v, exp);
    }

    ioctl(fd, Q_APPLY_CNOT, &cnot01);
    printf("\n--- 步驟 3：CNOT(0→1) → (|000⟩+|011⟩)/√2 ---\n");
    for (int i = 0; i < 8; i++) {
        int64_t v = ampl(fd, i);
        const char* exp = (i == 0 || i == 3) ? "← 期望 759250125" : "← 期望 0";
        printf("  amp[%d] = %+lld   %s\n", i, (long long)v, exp);
    }

    ioctl(fd, Q_APPLY_CNOT, &cnot12);
    printf("\n--- 步驟 4：CNOT(1→2) → (|000⟩+|111⟩)/√2 ---\n");
    int64_t g[8];
    for (int i = 0; i < 8; i++) {
        g[i] = ampl(fd, i);
        const char* exp = (i == 0 || i == 7) ? "← 期望 759250125" : "← 期望 0";
        printf("  amp[%d] = %+lld   %s\n", i, (long long)g[i], exp);
    }

    printf("\n--- GHZ 態驗證 ---\n");
    int ghz_ok = (g[0]==EXP && g[7]==EXP);
    for (int i = 1; i < 7; i++) if (g[i] != 0) ghz_ok = 0;
    printf("  |000⟩ 有值:      %s\n", (g[0]==EXP) ? "✅" : "❌");
    printf("  |001⟩ 為 0:      %s\n", (g[1]==0)   ? "✅" : "❌");
    printf("  |010⟩ 為 0:      %s\n", (g[2]==0)   ? "✅" : "❌");
    printf("  |011⟩ 為 0:      %s\n", (g[3]==0)   ? "✅" : "❌");
    printf("  |100⟩ 為 0:      %s\n", (g[4]==0)   ? "✅" : "❌");
    printf("  |101⟩ 為 0:      %s\n", (g[5]==0)   ? "✅" : "❌");
    printf("  |110⟩ 為 0:      %s\n", (g[6]==0)   ? "✅" : "❌");
    printf("  |111⟩ 有值:      %s\n", (g[7]==EXP) ? "✅" : "❌");
    printf("  |000⟩ = |111⟩:   %s\n", (g[0]==g[7]) ? "✅" : "❌");
    printf("\n  %s\n", ghz_ok ? "🎯 GHZ 態成功：三個 qubit 完全糾纏" : "❌ GHZ 態失敗");

    if (ghz_ok) {
        printf("\n  意義：三個 qubit 要嘛全部是 0，要嘛全部是 1\n");
        printf("        測 qubit 0 得 0 → qubit 1 和 2 必為 0\n");
        printf("        測 qubit 0 得 1 → qubit 1 和 2 必為 1\n");
    }

    close(fd);
    return 0;
}
