/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdio.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>

#define Q_INIT_ZERO     _IO('Q', 1)
#define Q_APPLY_H_Q     _IOW('Q', 3, unsigned int)
#define Q_APPLY_CNOT    _IOW('Q', 4, unsigned int)
#define Q_MEASURE_QUBIT _IOWR('Q', 21, struct qmeasure_req)
#define Q_MEASURE_ALL   _IOR('Q', 22, unsigned int)
#define Q_GET_AMPL      _IOWR('Q', 11, struct qampl_req)
#define Q_GET_INFO      _IOR('Q', 13, struct qinfo_req)

struct qmeasure_req { unsigned int qubit; int result; };
struct qampl_req { unsigned int idx; int64_t re_q30; int64_t im_q30; };
struct qinfo_req { unsigned int n_qubits; unsigned int dim; int64_t norm_sq_q30; };

int main(void) {
    int fd = open("/dev/g116_quantum", O_RDWR);
    if (fd < 0) { perror("open"); return 1; }

    unsigned int q0 = 0;
    unsigned int cnot01 = (0u << 8) | 1u;
    struct qinfo_req info;
    struct qampl_req a;

    /* ══════ 實驗 1：H 閘後的單 qubit 測量 ══════ */
    printf("╔══════════════════════════════════════════╗\n");
    printf("║  實驗 1：H 閘後測量 qubit 0 (100 次)     ║\n");
    printf("║  期望：0 和 1 各約 50 次                  ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    int count0 = 0, count1 = 0;
    for (int trial = 0; trial < 100; trial++) {
        ioctl(fd, Q_INIT_ZERO);
        ioctl(fd, Q_APPLY_H_Q, &q0);

        struct qmeasure_req r = { .qubit = 0 };
        ioctl(fd, Q_MEASURE_QUBIT, &r);
        if (r.result == 0) count0++;
        else                count1++;
    }
    printf("  測到 0：%d 次\n", count0);
    printf("  測到 1：%d 次\n", count1);
    printf("  比例：%.1f%% / %.1f%%\n\n", count0*100.0/100, count1*100.0/100);

    /* ══════ 實驗 2：貝爾態測量 ══════ */
    printf("╔══════════════════════════════════════════╗\n");
    printf("║  實驗 2：貝爾態測量 (50 次)              ║\n");
    printf("║  期望：q0 和 q1 永遠一致 (00 或 11)      ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    int same = 0, diff = 0, n00 = 0, n11 = 0;
    for (int trial = 0; trial < 50; trial++) {
        ioctl(fd, Q_INIT_ZERO);
        ioctl(fd, Q_APPLY_H_Q, &q0);
        ioctl(fd, Q_APPLY_CNOT, &cnot01);

        struct qmeasure_req r0 = { .qubit = 0 };
        struct qmeasure_req r1 = { .qubit = 1 };
        ioctl(fd, Q_MEASURE_QUBIT, &r0);
        ioctl(fd, Q_MEASURE_QUBIT, &r1);

        if (r0.result == r1.result) {
            same++;
            if (r0.result == 0) n00++;
            else                n11++;
        } else {
            diff++;
        }
    }
    printf("  00 出現：%d 次\n", n00);
    printf("  11 出現：%d 次\n", n11);
    printf("  01/10 出現：%d 次\n", diff);
    printf("  一致率：%.1f%%\n\n", same*100.0/50);

    /* ══════ 實驗 3：測量後態的塌縮 ══════ */
    printf("╔══════════════════════════════════════════╗\n");
    printf("║  實驗 3：測量後態塌縮（觀察振幅變化）    ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    ioctl(fd, Q_INIT_ZERO);
    ioctl(fd, Q_APPLY_H_Q, &q0);

    a.idx = 0; ioctl(fd, Q_GET_AMPL, &a);
    printf("  測量前 amp[0] = %+lld\n", (long long)a.re_q30);
    a.idx = 1; ioctl(fd, Q_GET_AMPL, &a);
    printf("  測量前 amp[1] = %+lld\n", (long long)a.re_q30);

    struct qmeasure_req r = { .qubit = 0 };
    ioctl(fd, Q_MEASURE_QUBIT, &r);
    printf("\n  測量 qubit 0 的結果 = %d\n\n", r.result);

    a.idx = 0; ioctl(fd, Q_GET_AMPL, &a);
    printf("  測量後 amp[0] = %+lld\n", (long long)a.re_q30);
    a.idx = 1; ioctl(fd, Q_GET_AMPL, &a);
    printf("  測量後 amp[1] = %+lld\n", (long long)a.re_q30);

    if (r.result == 0) {
        printf("  → amp[0] 應 = +1073741824 (QONE), amp[1] 應 = 0\n");
    } else {
        printf("  → amp[0] 應 = 0, amp[1] 應 = +1073741824 (QONE)\n");
    }

    /* ══════ 實驗 4：全測量 ══════ */
    printf("\n╔══════════════════════════════════════════╗\n");
    printf("║  實驗 4：全測量（3-qubit GHZ 態）        ║\n");
    printf("║  期望：只出現 000 或 111                 ║\n");
    printf("╚══════════════════════════════════════════╝\n\n");

    unsigned int q2 = 2;
    unsigned int cnot12 = (1u << 8) | 2u;
    int n000 = 0, n111 = 0, nother = 0;
    for (int trial = 0; trial < 50; trial++) {
        ioctl(fd, Q_INIT_ZERO);
        ioctl(fd, Q_APPLY_H_Q, &q0);
        ioctl(fd, Q_APPLY_CNOT, &cnot01);
        ioctl(fd, Q_APPLY_CNOT, &cnot12);

        unsigned int result;
        ioctl(fd, Q_MEASURE_ALL, &result);
        if (result == 0)        n000++;
        else if (result == 7)   n111++;
        else                    nother++;
    }
    printf("  000 (index 0)：%d 次\n", n000);
    printf("  111 (index 7)：%d 次\n", n111);
    printf("  其他：%d 次  ← 應為 0\n", nother);
    printf("\n  %s\n", (nother == 0) ? "🎯 全測量正確：GHZ 態只塌縮到 000 或 111" : "❌ 異常");

    close(fd);
    return 0;
}
