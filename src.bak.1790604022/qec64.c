/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include "qops.h"
extern int32_t *g_qstate;
extern void q_apply_cnot(int c, int t);

/*
 * 3-qubit 位元翻轉碼
 * 編碼：|0>→|000>, |1>→|111>（用兩個 CNOT）
 * 錯誤：一個 bit 被翻
 * 糾錯：多數決
 */

/* 編碼：把 q[0] 的狀態複製到 q[1], q[2] */
void qec_encode(int q0, int q1, int q2){
    q_apply_cnot(q0, q1);
    q_apply_cnot(q0, q2);
}

/* 解碼：把 q1, q2 帶回 q0（逆編碼）*/
void qec_decode(int q0, int q1, int q2){
    q_apply_cnot(q0, q1);
    q_apply_cnot(q0, q2);
}
