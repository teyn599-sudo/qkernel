#include <stdint.h>
#include "qops.h"
#define QSHIFT 30
#define QONE   (1<<QSHIFT)
#define QADD(a,b) ((int32_t)((int64_t)(a) + (int64_t)(b)))
#define QSUB(a,b) ((int32_t)((int64_t)(a) - (int64_t)(b)))
#define QMUL(a,b) ((int32_t)(((int64_t)(a) * (int64_t)(b)) >> QSHIFT))
extern int32_t *g_qstate;
extern void q_apply_h(int q);

/* Oracle：翻轉 target 態的相位 */
static void oracle(int nq, uint64_t target){
    int32_t *st = g_qstate;
    st[target*2]   = -st[target*2];
    st[target*2+1] = -st[target*2+1];
}

/* Diffuser：關於平均值翻轉（反射）*/
static void diffuser(int nq){
    int32_t *st = g_qstate;
    uint64_t dim = 1ULL << nq;
    int64_t sum = 0;
    for (uint64_t i = 0; i < dim; i++) sum += st[i*2];
    int64_t inv_dim = QONE / (int64_t)dim;
    int64_t avg = QMUL(sum, inv_dim);
    for (uint64_t i = 0; i < dim; i++){
        st[i*2]   = 2*avg - st[i*2];
        st[i*2+1] = -st[i*2+1];
    }
}

/* Grover：nq qubit，目標 target，跑 iter 次 */
void grover_run(int nq, uint64_t target, int iter){
    int32_t *st = g_qstate;
    uint64_t dim = 1ULL << nq;
    for (uint64_t i = 0; i < dim; i++){ st[i*2]=0; st[i*2+1]=0; }
    st[0] = QONE;
    for (int q = 0; q < nq; q++) q_apply_h(q);
    for (int it = 0; it < iter; it++){
        oracle(nq, target);
        diffuser(nq);
        int64_t v = st[target*2]; if (v<0) v=-v;
        sputs("[grover] iter "); sputu((uint64_t)it);
        sputs(": amp = "); sputu((uint64_t)(v >> 20));
        sputs("\n");
    }
}
