/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef QOPS_H
#define QOPS_H
#include <stdint.h>

/* 量子門（在 kernel64.c 定義，供 qft64/grover64/qec64 呼叫） */
void q_apply_h(int q);
void q_apply_x(int q);
void q_apply_cnot(int c, int t);
void q_apply_toffoli(int c1, int c2, int t);

/* 全域量子態指標（noise64/qft64/... 共用） */
extern int32_t *g_qstate;

#endif

/* 序列埠輸出（在 kernel64.c 定義） */
void sputs(const char *s);
void sputu(uint64_t v);
void sputc(char c);

/* 量子態全域（在 kernel64.c 定義） */
extern int g_nq;
extern uint64_t g_qdim;

/* Heap（在 kernel64.c 定義）*/
void     *heap_alloc(uint64_t sz);
uint64_t  heap_free(void);
