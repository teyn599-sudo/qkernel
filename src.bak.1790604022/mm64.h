/* SPDX-License-Identifier: GPL-3.0-or-later */
#ifndef MM64_H
#define MM64_H
#include <stdint.h>

/* E820 提供的 low RAM 最大區（kernel64.c 填） */
extern uint64_t g_low_base, g_low_size;

void     pmm_init(void);
uint64_t pmm_alloc(void);                /* 回傳 4KB 頁的實體位址，0=失敗 */
void     pmm_free(uint64_t pa);
uint64_t pmm_free_pages(void);
uint64_t pmm_total_pages(void);

void    *kmalloc(uint64_t sz);
void     kfree(void *p);

#endif
