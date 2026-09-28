/* demo64.c - 量子演算法可視化 */
#include <stdint.h>
#define QSHIFT 30
#define QONE   (1<<QSHIFT)
#define QADD(a,b) ((int32_t)((int64_t)(a) + (int64_t)(b)))
#define QSUB(a,b) ((int32_t)((int64_t)(a) - (int64_t)(b)))
#define QMUL(a,b) ((int32_t)(((int64_t)(a) * (int64_t)(b)) >> QSHIFT))

extern int32_t *g_qstate;
extern int g_nq;
extern uint64_t g_qdim;
extern void q_apply_h(int q);
extern void q_apply_cnot(int c, int t);
extern void sputs(const char *s);
extern void sputu(uint64_t v);
extern void *heap_alloc(uint64_t sz);
extern void qft_run(int nq);

/* 印出每個基底態的 |amp| 用 # 條形圖 */
static void show_hist(int nq, const char *title){
    uint64_t dim = 1ULL << nq;
    sputs("\n");
    if (title && title[0]){ sputs(title); sputs("\n"); }
    for (uint64_t i = 0; i < dim; i++){
        int32_t re = g_qstate[i*2];
        uint64_t mag = (uint64_t)(re < 0 ? -re : re) >> 20;   /* 0..1024 */
        for (int b = nq-1; b >= 0; b--) sputs((i>>b)&1 ? "1" : "0");
        sputs(" |");
        for (uint64_t b = 0; b < mag/25; b++) sputs("#");
        for (uint64_t b = mag/25; b < 41; b++) sputs(" ");
        sputs("| ");
        sputu(mag * 100 / 1024);
        sputs("%\n");
    }
}

static void demo_qft(void){
    int nq = 3;
    uint64_t dim = 1ULL << nq;
    int32_t *buf = (int32_t*)heap_alloc(dim * 8);
    if (!buf) return;
    for (uint64_t i = 0; i < dim; i++){ buf[i*2]=0; buf[i*2+1]=0; }
    buf[0] = QONE;
    g_qstate = buf; g_nq = nq; g_qdim = dim;

    sputs("\n");
    sputs("========================================\n");
    sputs("  DEMO 1: QFT 3 qubit\n");
    sputs("  |000> --> 均勻疊加\n");
    sputs("========================================\n");
    qft_run(nq);
    show_hist(nq, "");
}

static void demo_grover(void){
    int nq = 4;
    uint64_t dim = 1ULL << nq;
    uint64_t target = 0b1011;
    int32_t *buf = (int32_t*)heap_alloc(dim * 8);
    if (!buf) return;
    g_qstate = buf; g_nq = nq; g_qdim = dim;

    sputs("\n");
    sputs("========================================\n");
    sputs("  DEMO 2: Grover 4q 搜尋 |1011>\n");
    sputs("========================================\n");

    for (int iter = 0; iter <= 3; iter++){
        for (uint64_t i = 0; i < dim; i++){ buf[i*2]=0; buf[i*2+1]=0; }
        buf[0] = QONE;
        for (int q = 0; q < nq; q++) q_apply_h(q);
        for (int k = 0; k < iter; k++){
            buf[target*2] = -buf[target*2];
            int64_t sum = 0;
            for (uint64_t i = 0; i < dim; i++) sum += buf[i*2];
            int64_t avg = QMUL(sum, QONE / (int64_t)dim);
            for (uint64_t i = 0; i < dim; i++){
                buf[i*2]   = 2*avg - buf[i*2];
                buf[i*2+1] = -buf[i*2+1];
            }
        }
        sputs("\n--- iter=");
        sputu(iter);
        sputs(" ---\n");
        show_hist(nq, "");
    }
}

static void demo_qec(void){
    int nq = 3;
    uint64_t dim = 1ULL << nq;
    int32_t *buf = (int32_t*)heap_alloc(dim * 8);
    if (!buf) return;
    for (uint64_t i = 0; i < dim; i++){ buf[i*2]=0; buf[i*2+1]=0; }
    buf[1*2] = QONE;   /* |100>：q0=1, q1=q2=0 */
    g_qstate = buf; g_nq = nq; g_qdim = dim;

    sputs("\n");
    sputs("========================================\n");
    sputs("  DEMO 3: QEC 3q 編碼 |100> -> |111>\n");
    sputs("========================================\n");
    sputs("\n--- 初始 |100> ---\n");
    show_hist(nq, "");

    q_apply_cnot(0, 1);
    q_apply_cnot(0, 2);

    sputs("\n--- 編碼後 |111> ---\n");
    show_hist(nq, "");
}

void demo_all(void){
    demo_qft();
    demo_grover();
    demo_qec();
    sputs("\n=== DEMO 完成 ===\n");
}
