#include <stdint.h>
#include "qops.h"
#define QSHIFT 30
#define QONE   (1<<QSHIFT)
#define QADD(a,b) ((int32_t)((int64_t)(a) + (int64_t)(b)))
#define QSUB(a,b) ((int32_t)((int64_t)(a) - (int64_t)(b)))
#define QMUL(a,b) ((int32_t)(((int64_t)(a) * (int64_t)(b)) >> QSHIFT))
extern int32_t *g_qstate;
extern void q_apply_h(int q);

/* cos(pi/2^k) × 2^30, k=1..10 */
static const int64_t COS_T[] = {0, 759250125, 991934144, 1053263836, 1068658509,
                                1072535696, 1073482642, 1073721080, 1073781353, 1073790688};
static const int64_t SIN_T[] = {1073741824, 759250125, 410861679, 209406176, 105233267,
                                52666990, 26343121, 13174108, 6586901, 3293412};

static void q_cphase(int nq, int c, int t, int k){
    int32_t *st = g_qstate;
    uint64_t dim = 1ULL << nq;
    uint64_t mc = 1ULL << c, mt = 1ULL << t;
    int64_t cs = COS_T[k-1], sn = SIN_T[k-1];
    for (uint64_t i = 0; i < dim; i++){
        if ((i & mc) && (i & mt)){
            int64_t re = st[i*2], im = st[i*2+1];
            st[i*2]   = QMUL(re, cs) - QMUL(im, sn);
            st[i*2+1] = QMUL(re, sn) + QMUL(im, cs);
        }
    }
}
static void q_swap(int nq, int a, int b){
    int32_t *st = g_qstate;
    uint64_t dim = 1ULL << nq;
    uint64_t ma = 1ULL << a, mb = 1ULL << b;
    for (uint64_t i = 0; i < dim; i++){
        if (((i>>a)&1) != ((i>>b)&1)){
            uint64_t ip = i ^ ma ^ mb;
            if (i < ip){
                int64_t tr=st[i*2], ti=st[i*2+1];
                st[i*2]=st[ip*2]; st[i*2+1]=st[ip*2+1];
                st[ip*2]=tr; st[ip*2+1]=ti;
            }
        }
    }
}
void qft_run(int nq){
    for (int i = 0; i < nq; i++){
        q_apply_h(i);
        for (int j = i+1; j < nq; j++){
            int k = j-i;
            if (k <= 10) q_cphase(nq, j, i, k);
        }
    }
    for (int i = 0; i < nq/2; i++) q_swap(nq, i, nq-1-i);
}
