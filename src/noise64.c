#include "noise64.h"
#include "qops.h"

#define QSHIFT 30
#define QONE   (1<<QSHIFT)
#define QADD(a,b) ((int32_t)((int64_t)(a) + (int64_t)(b)))
#define QSUB(a,b) ((int32_t)((int64_t)(a) - (int64_t)(b)))
#define QMUL(a,b) ((int32_t)(((int64_t)(a) * (int64_t)(b)) >> QSHIFT))

/* ---- xorshift64 RNG ---- */
static uint64_t g_rng;
void noise_init(uint64_t seed){ g_rng = seed ? seed : 0x9E3779B97F4A7C15ULL; }
uint32_t noise_u32(void){
    g_rng ^= g_rng << 13;
    g_rng ^= g_rng >> 7;
    g_rng ^= g_rng << 17;
    return (uint32_t)(g_rng >> 16);
}
int64_t noise_q30(void){ return (int64_t)(noise_u32() >> 2); }

/* ---- 外部量子態（在 kernel64.c 定義）---- */
extern int32_t *g_qstate;

/* ---- T1 + T2 雜訊 ---- */
void noise_qubit(int nq, int q, int64_t decay_amp, int64_t p_phase){
    int32_t *st = g_qstate;
    uint64_t dim = 1ULL << nq;
    uint64_t mask = 1ULL << q;
    int do_phase = (noise_q30() < p_phase);
    for (uint64_t k = 0; k < dim; k++){
        if (k & mask){
            int64_t re = QMUL(st[k*2],   decay_amp);
            int64_t im = QMUL(st[k*2+1], decay_amp);
            if (do_phase){ re = -re; im = -im; }
            st[k*2]   = re;
            st[k*2+1] = im;
        }
    }
}

/* ---- 閘錯誤 ---- */
void noise_gate_error(int nq, int q, int64_t p){
    if (noise_q30() >= p) return;
    int32_t *st = g_qstate;
    uint64_t dim = 1ULL << nq;
    uint64_t mask = 1ULL << q;
    /* 一半機率 X，一半機率 Z */
    int use_x = (noise_u32() & 1);
    if (use_x){
        /* X：交換 |0> 和 |1> 振幅 */
        for (uint64_t k = 0; k < dim; k++){
            if ((k & mask) == 0){
                uint64_t k2 = k | mask;
                int64_t tr = st[k*2], ti = st[k*2+1];
                st[k*2]   = st[k2*2];   st[k*2+1]   = st[k2*2+1];
                st[k2*2]  = tr;         st[k2*2+1]  = ti;
            }
        }
    } else {
        /* Z：|1> 振幅變號 */
        for (uint64_t k = 0; k < dim; k++)
            if (k & mask){ st[k*2] = -st[k*2]; st[k*2+1] = -st[k*2+1]; }
    }
}

/* ---- 保真度 F = |⟨a|b⟩|² ---- */
int64_t state_fidelity(int nq, const int64_t *a, const int64_t *b){
    uint64_t dim = 1ULL << nq;
    int64_t sr = 0, si = 0;
    for (uint64_t k = 0; k < dim; k++){
        sr += QMUL(a[k*2],   b[k*2])   + QMUL(a[k*2+1], b[k*2+1]);
        si += QMUL(a[k*2+1], b[k*2])   - QMUL(a[k*2],   b[k*2+1]);
    }
    return QMUL(sr,sr) + QMUL(si,si);
}
