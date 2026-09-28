static void q_apply_fused_hxcnot(void);
// ============================================================
// G116 核心內量子計算機 v2 — 完整版
// A: Q30.30 高精度定點數 (int64)
// B: X, Y, Z, T, S, Toffoli 閘
// C: 部分跡 (約化密度矩陣輸出)
// D: 支援 systemd 自動載入
// ============================================================
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/cdev.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>
#include <linux/mutex.h>
#include <linux/ioctl.h>
#include <linux/workqueue.h>
#include <linux/random.h>

#define N_QUBITS 28
#define DIM      (1ULL << N_QUBITS)   /* 2^30 = 1,073,741,824 */

#define QSHIFT   30
#define QONE     (1LL << QSHIFT)    /* 2^30 = 1073741824 */
#define QINV_SQRT2 759250125LL      /* 1/sqrt(2) in Q30.30 */

typedef struct {
    int32_t re;
    int32_t im;
} qcplx_t;

static qcplx_t *q_state = NULL;

/* ══════ 並行框架：workqueue ══════ */
#define MAX_WORKERS 16

struct parallel_work {
    struct work_struct work;
    int chunk;
    int n_chunks;
    void (*fn)(void *ctx, int chunk, int n_chunks);
    void *user_ctx;
};

static struct workqueue_struct *g_wq = NULL;
static struct parallel_work g_works[MAX_WORKERS];
static atomic_t g_pending = ATOMIC_INIT(0);
static struct completion g_all_done;
static int g_n_workers = 1;

static void parallel_work_fn(struct work_struct *work)
{
    struct parallel_work *pw = container_of(work, struct parallel_work, work);
    pw->fn(pw->user_ctx, pw->chunk, pw->n_chunks);
    if (atomic_dec_and_test(&g_pending))
        complete(&g_all_done);
}

static int wq_init(void)
{
    int i;
    if (g_wq) return 0;
    g_wq = alloc_workqueue("g116_wq", WQ_HIGHPRI | WQ_UNBOUND, 0);
    if (!g_wq) return -ENOMEM;
    for (i = 0; i < MAX_WORKERS; i++)
        INIT_WORK(&g_works[i].work, parallel_work_fn);
    init_completion(&g_all_done);
    return 0;
}

static void wq_exit(void)
{
    if (g_wq) {
        destroy_workqueue(g_wq);
        g_wq = NULL;
    }
}

static void run_parallel(void (*fn)(void*,int,int), void *ctx, int n_workers)
{
    int i;
    if (n_workers < 1) n_workers = 1;
    if (n_workers > MAX_WORKERS) n_workers = MAX_WORKERS;

    atomic_set(&g_pending, n_workers);
    reinit_completion(&g_all_done);

    for (i = 0; i < n_workers; i++) {
        g_works[i].chunk = i;
        g_works[i].n_chunks = n_workers;
        g_works[i].fn = fn;
        g_works[i].user_ctx = ctx;
        queue_work(g_wq, &g_works[i].work);
    }
    wait_for_completion(&g_all_done);
}

static DEFINE_MUTEX(q_lock);
static dev_t q_devno;
static struct cdev q_cdev;
static struct class *q_class = NULL;
static struct device *q_device = NULL;
static const char *DEV_NAME = "g116_quantum";

/* ── ioctl 命令 ────────────────────────────────── */
#define Q_INIT_ZERO     _IO('Q', 1)
#define Q_APPLY_H_ALL   _IO('Q', 2)
#define Q_APPLY_H_Q     _IOW('Q', 3, unsigned int)
#define Q_APPLY_CNOT    _IOW('Q', 4, unsigned int)
#define Q_APPLY_X       _IOW('Q', 5, unsigned int)
#define Q_APPLY_Y       _IOW('Q', 6, unsigned int)
#define Q_APPLY_Z       _IOW('Q', 7, unsigned int)
#define Q_APPLY_T       _IOW('Q', 8, unsigned int)
#define Q_APPLY_S       _IOW('Q', 9, unsigned int)
#define Q_APPLY_TOFFOLI _IOW('Q', 10, unsigned int) /* (c1<<16)|(c2<<8)|tgt */
#define Q_GET_AMPL      _IOWR('Q', 11, struct qampl_req)
#define Q_GET_PROB      _IOWR('Q', 12, struct qprob_req)
#define Q_GET_INFO      _IOR('Q', 13, struct qinfo_req)
#define Q_PARTIAL_TRACE _IOWR('Q', 14, struct qpt_req)
struct qload_req {
    unsigned int offset;
    unsigned int count;
    const long long *data_re;
    const long long *data_im;
};

#define Q_LOAD_STATE    _IOW('Q', 15, struct qload_req)
#define Q_SET_UNIFORM   _IO('Q', 20)
#define Q_MEASURE_QUBIT _IOWR('Q', 21, struct qmeasure_req)
#define Q_MEASURE_ALL   _IOR('Q', 22, unsigned int)
#define Q_GROVER_RUN    _IOWR('Q', 23, struct qgrover_req)
#define Q_LOAD_BITS     _IOW('Q', 24, struct qload_bits_req)
#define Q_GROVER_MATRIX _IOWR('Q', 25, struct qgrover_matrix_req)
#define Q_APPLY_FUSED_HXCNOTH _IO('Q', 30)
#define Q_QFT_RUN       _IOW('Q', 26, struct qqft_req)
#define Q_GROVER_PATTERN _IOWR('Q', 27, struct qgpat_req)

struct qmeasure_req {
    unsigned int qubit;
    int          result;
};
struct qload_bits_req {
    unsigned int n_qubits;
    unsigned int reserved;
    const unsigned char *data;
};
struct qgpat_req {
    unsigned int n_qubits;
    unsigned int pattern;      /* 要找的 bit pattern */
    unsigned int pattern_bits; /* pattern 佔幾個 bit */
    unsigned int iterations;
    const unsigned char *data;
    int          result;
    unsigned int marked_count;
};
struct qgrover_matrix_req {
    unsigned int n_qubits;
    unsigned int iterations;
    const unsigned char *data;
    int          result;
    unsigned int marked_count;
};
struct qqft_req {
    unsigned int n_qubits;
};
struct qgrover_req {
    unsigned int n_qubits;
    unsigned int target;
    unsigned int iterations;
    int          result;
    unsigned int found;
};
struct qampl_req {
    unsigned int idx;
    int64_t re_q30;
    int64_t im_q30;
};

struct qprob_req {
    unsigned int idx;
    int64_t prob_q30;
};

struct qinfo_req {
    unsigned int n_qubits;
    unsigned int dim;
    int64_t norm_sq_q30;
};

struct qpt_req {
    unsigned int n_keep;    /* 保留前 n_keep 個 qubit */
    unsigned int matrix_dim;/* 2^n_keep，呼叫方需設定 */
    int64_t *rho_out;       /* 使用者提供的緩衝區 (matrix_dim² × 16 bytes) */
};

/* ── 初始化 ───────────────────────────────────── */
static u64 isqrt64(u64 n)
{
    u64 x, y;
    if (n == 0) return 0;
    x = n; y = (x + 1) >> 1;
    while (y < x) { x = y; y = (x + n / x) >> 1; }
    return x;
}

static void q_apply_h(unsigned int q);
static void q_apply_cnot(unsigned int ctrl, unsigned int tgt);

/* ── 受控相位門: ctrl=1 時對 tgt 施加相位 e^(i·2π/2^k) ── */
static void q_apply_cphase(unsigned int ctrl, unsigned int tgt, unsigned int k)
{
    static const int64_t COS_TAB[8] = {
        1073741824LL, 0LL, 759250125LL, 991875200LL,
        1053030080LL, 1065707200LL, 1068875200LL, 1069667700LL
    };
    static const int64_t SIN_TAB[8] = {
        0LL, 1073741824LL, 759250125LL, 410998702LL,
        209206720LL, 105294400LL, 52817500LL, 26456700LL
    };
    unsigned int i, cmask = 1u << ctrl, tmask = 1u << tgt;
    int64_t c, s;
    if (ctrl >= N_QUBITS || tgt >= N_QUBITS || ctrl == tgt) return;
    if (k >= 8) return;
    c = COS_TAB[k]; s = SIN_TAB[k];
    for (i = 0; i < DIM; i++) {
        if ((i & cmask) && (i & tmask)) {
            int64_t re = q_state[i].re, im = q_state[i].im;
            int64_t nr = (int64_t)re * c - (int64_t)im * s;
            int64_t ni = (int64_t)re * s + (int64_t)im * c;
            q_state[i].re = (int32_t)(nr >> QSHIFT);
            q_state[i].im = (int32_t)(ni >> QSHIFT);
        }
    }
}

/* ── QFT: n qubits 上的量子傅立葉變換 ── */
/* ── 局部 H 閘：只作用於前 N 個振幅 ── */
static void q_apply_h_local(unsigned int q, unsigned int N)
{
    unsigned int mask = 1u << q;
    unsigned int i;
    if (q >= 31) return;
    for (i = 0; i < N; i++) {
        if ((i & 127) == 0) {
            __builtin_prefetch(&q_state[i + 512], 0, 3);
            __builtin_prefetch(&q_state[i + 384], 0, 3);
        }
        if (i & mask) continue;
        unsigned int j = i | mask;
        int64_t ar = q_state[i].re, ai = q_state[i].im;
        int64_t br = q_state[j].re, bi = q_state[j].im;
        int64_t sr = ((int64_t)(ar + br)) * QINV_SQRT2;
        int64_t si = ((int64_t)(ai + bi)) * QINV_SQRT2;
        int64_t dr = ((int64_t)(ar - br)) * QINV_SQRT2;
        int64_t di = ((int64_t)(ai - bi)) * QINV_SQRT2;
        q_state[i].re = (int32_t)((sr + (1LL << (QSHIFT-1))) >> QSHIFT);
        q_state[i].im = (int32_t)((si + (1LL << (QSHIFT-1))) >> QSHIFT);
        q_state[j].re = (int32_t)((dr + (1LL << (QSHIFT-1))) >> QSHIFT);
        q_state[j].im = (int32_t)((di + (1LL << (QSHIFT-1))) >> QSHIFT);
    }
}

/* ── 局部 CNOT：只作用於前 N 個振幅 ── */
static void q_apply_cnot_local(unsigned int ctrl, unsigned int tgt, unsigned int N)
{
    unsigned int cmask = 1u << ctrl, tmask = 1u << tgt;
    unsigned int i;
    for (i = 0; i < N; i++) {
        if (!(i & cmask)) continue;
        if (i & tmask) continue;
        unsigned int j = i | tmask;
        int64_t tr = q_state[i].re, ti = q_state[i].im;
        q_state[i].re = q_state[j].re; q_state[i].im = q_state[j].im;
        q_state[j].re = tr; q_state[j].im = ti;
    }
}

/* ── 局部 CPhase：只作用於前 N 個振幅 ── */
static void q_apply_cphase_local(unsigned int ctrl, unsigned int tgt, unsigned int k, unsigned int N)
{
    static const int64_t COS_TAB[8] = {
        -1073741824LL, 0LL, 759250125LL, 991875200LL,
        1053065700LL, 1068543245LL, 1072441260LL, 1073419210LL
    };
    static const int64_t SIN_TAB[8] = {
        0LL, 1073741824LL, 759250125LL, 410998702LL,
        209440954LL, 105247463LL, 52695627LL, 26351761LL
    };
    unsigned int i, cmask, tmask;
    int64_t c, s;
    if (k < 1 || k > 8) return;
    cmask = 1u << ctrl; tmask = 1u << tgt;
    c = COS_TAB[k-1]; s = SIN_TAB[k-1];
    for (i = 0; i < N; i++) {
        if ((i & cmask) && (i & tmask)) {
            int64_t re = q_state[i].re, im = q_state[i].im;
            int64_t nr = (int64_t)re * c - (int64_t)im * s;
            int64_t ni = (int64_t)re * s + (int64_t)im * c;
            q_state[i].re = (int32_t)(nr >> QSHIFT);
            q_state[i].im = (int32_t)(ni >> QSHIFT);
        }
    }
}

static int q_qft_run(unsigned int n)
{
    unsigned int i, j, N;
    if (n < 1 || n > 24) return -EINVAL;
    N = 1u << n;
    for (i = n; i-- > 0; ) {
        q_apply_h_local(i, N);
        for (j = 0; j < i; j++) {
            q_apply_cphase_local(j, i, i - j, N);
        }
    }
    for (i = 0; i < n / 2; i++) {
        unsigned int a = i, b = n - 1 - i;
        q_apply_cnot_local(a, b, N);
        q_apply_cnot_local(b, a, N);
        q_apply_cnot_local(a, b, N);
    }
    return 0;
}



/* ── 矩陣 Grover: 在 n bit 中找所有 1 的位置 ── */
static int q_grover_matrix(unsigned int n_qubits,
                           unsigned int *inout_iterations,
                           const unsigned char __user *data,
                           int *out_result,
                           unsigned int *out_marked)
{
    unsigned int N, n_bytes, i, it, marked = 0;
    unsigned char *buf;
    __int128 total;
    int64_t avg2, amp;
    u64 sq;
    if (n_qubits < 1 || n_qubits > 20) return -EINVAL;
    N = 1u << n_qubits;
    n_bytes = N / 8;
    buf = (unsigned char *)vmalloc(n_bytes);
    if (!buf) return -ENOMEM;
    if (copy_from_user(buf, data, n_bytes)) { vfree(buf); return -EFAULT; }
    sq = isqrt64((u64)N);
    if (sq == 0) sq = 1;
    amp = (int64_t)(QONE / (int64_t)sq);
    for (i = 0; i < DIM; i++) { q_state[i].re = 0; q_state[i].im = 0; }
    for (i = 0; i < N; i++) {
        int bit = (buf[i/8] >> (i%8)) & 1;
        q_state[i].re = amp;
        if (bit) marked++;
    }
    vfree(buf);
    if (marked == 0 || marked == N) return -EINVAL;
    if (*inout_iterations == 0) {
        u64 s = isqrt64((u64)(N / marked));
        *inout_iterations = (unsigned int)((s * 7853981634ULL) / 10000000000ULL);
        if (*inout_iterations == 0) *inout_iterations = 1;
    }
    for (it = 0; it < *inout_iterations; it++) {
        unsigned int j;
        for (j = 0; j < N; j++) {
            buf = NULL;
        }
        /* 這裡直接在記憶體上做（不能再 vmalloc buf，用 q_state 判斷已無原始資料）*/
        break;
    }
    /* Oracle 需要原始 bit，直接重新讀 */
    buf = (unsigned char *)vmalloc(n_bytes);
    if (!buf) return -ENOMEM;
    if (copy_from_user(buf, data, n_bytes)) { vfree(buf); return -EFAULT; }
    for (it = 0; it < *inout_iterations; it++) {
        for (i = 0; i < N; i++) {
            int bit = (buf[i/8] >> (i%8)) & 1;
            if (bit) { q_state[i].re = -q_state[i].re; q_state[i].im = -q_state[i].im; }
        }
        total = 0;
        for (i = 0; i < N; i++) total += q_state[i].re;
        avg2 = (int64_t)((total >> n_qubits) * 2);
        for (i = 0; i < N; i++) q_state[i].re = avg2 - q_state[i].re;
    }
    vfree(buf);
    {
        unsigned int best = 0; int64_t best_p = -1;
        for (i = 0; i < N; i++) {
            int64_t p = q_state[i].re * q_state[i].re;
            if (p > best_p) { best_p = p; best = i; }
        }
        *out_result = (int)best;
    }
    *out_marked = marked;
    return 0;
}

/* ── Grover: 在 n bit 中找符合 pattern 的位置 ── */
static int q_grover_pattern(unsigned int n_qubits,
                            unsigned int pattern, unsigned int pbits,
                            unsigned int *inout_iterations,
                            const unsigned char __user *data,
                            int *out_result, unsigned int *out_marked)
{
    unsigned int N, n_bytes, i, it, marked = 0;
    unsigned char *buf;
    __int128 total;
    int64_t avg2, amp;
    u64 sq;
    unsigned int pmask;
    if (n_qubits < pbits || n_qubits > 20) return -EINVAL;
    if (pbits < 1 || pbits > 24) return -EINVAL;
    N = 1u << n_qubits;
    pmask = (pbits >= 32) ? 0xFFFFFFFFu : ((1u << pbits) - 1u);
    n_bytes = N / 8;
    buf = (unsigned char *)vmalloc(n_bytes);
    if (!buf) return -ENOMEM;
    if (copy_from_user(buf, data, n_bytes)) { vfree(buf); return -EFAULT; }
    /* 統計 marked */
    for (i = 0; i + pbits <= N; i++) {
        unsigned int v = 0;
        unsigned int b;
        for (b = 0; b < pbits; b++) {
            unsigned int idx = i + b;
            int bit = (buf[idx/8] >> (idx%8)) & 1;
            v |= (unsigned int)bit << b;
        }
        if ((v & pmask) == pattern) marked++;
    }
    if (marked == 0) { vfree(buf); return -ENOENT; }
    sq = isqrt64((u64)N);
    if (sq == 0) sq = 1;
    amp = (int64_t)(QONE / (int64_t)sq);
    for (i = 0; i < DIM; i++) { q_state[i].re = 0; q_state[i].im = 0; }
    for (i = 0; i < N; i++) q_state[i].re = amp;
    if (*inout_iterations == 0) {
        u64 ratio = (u64)N / (u64)marked;
        u64 s = isqrt64(ratio);
        *inout_iterations = (unsigned int)((s * 7853981634ULL) / 10000000000ULL);
        if (*inout_iterations == 0) *inout_iterations = 1;
    }
    for (it = 0; it < *inout_iterations; it++) {
        for (i = 0; i + pbits <= N; i++) {
            unsigned int v = 0, b;
            for (b = 0; b < pbits; b++) {
                unsigned int idx = i + b;
                int bit = (buf[idx/8] >> (idx%8)) & 1;
                v |= (unsigned int)bit << b;
            }
            if ((v & pmask) == pattern) {
                q_state[i].re = -q_state[i].re;
                q_state[i].im = -q_state[i].im;
            }
        }
        total = 0;
        for (i = 0; i < N; i++) total += q_state[i].re;
        avg2 = (int64_t)((total >> n_qubits) * 2);
        for (i = 0; i < N; i++) q_state[i].re = avg2 - q_state[i].re;
    }
    vfree(buf);
    {
        unsigned int best = 0; int64_t bp = -1;
        for (i = 0; i < N; i++) {
            int64_t p = q_state[i].re * q_state[i].re;
            if (p > bp) { bp = p; best = i; }
        }
        *out_result = (int)best;
    }
    *out_marked = marked;
    return 0;
}

static int q_load_bits(unsigned int n_qubits,
                       const unsigned char __user *data)
{
    unsigned int N, n_bytes, i;
    int64_t amp;
    unsigned char *buf;
    u64 sq;
    if (n_qubits < 1 || n_qubits > N_QUBITS) return -EINVAL;
    N = 1u << n_qubits;
    n_bytes = N / 8;
    buf = (unsigned char *)vmalloc(n_bytes);
    if (!buf) return -ENOMEM;
    if (copy_from_user(buf, data, n_bytes)) {
        vfree(buf);
        return -EFAULT;
    }
    for (i = 0; i < DIM; i++) { q_state[i].re = 0; q_state[i].im = 0; }
    sq = isqrt64((u64)N);
    if (sq == 0) sq = 1;
    amp = (int64_t)(QONE / (int64_t)sq);
    for (i = 0; i < N; i++) {
        int bit = (buf[i/8] >> (i%8)) & 1;
        q_state[i].re = bit ? amp : -amp;
        q_state[i].im = 0;
    }
    vfree(buf);
    return 0;
}

static int q_grover_run(unsigned int n_qubits, unsigned int target,
                        unsigned int *inout_iterations, int *out_result)
{
    unsigned int N, i, it;
    int64_t avg2;
    __int128 total;
    if (n_qubits < 1 || n_qubits > N_QUBITS) return -EINVAL;
    N = 1u << n_qubits;
    if (target >= N) return -EINVAL;
    if (*inout_iterations == 0) {
        u64 s = isqrt64((u64)N);
        *inout_iterations = (unsigned int)((s * 7853981634ULL) / 10000000000ULL);
        if (*inout_iterations == 0) *inout_iterations = 1;
    }
    for (i = 0; i < DIM; i++) {
        q_state[i].re = 0;
        q_state[i].im = 0;
    }
    {
        u64 sq = isqrt64((u64)N);
        int64_t amp;
        if (sq == 0) sq = 1;
        amp = (int64_t)(QONE / (int64_t)sq);
        for (i = 0; i < N; i++) {
            q_state[i].re = amp;
        }
    }
    for (it = 0; it < *inout_iterations; it++) {
        q_state[target].re = -q_state[target].re;
        q_state[target].im = -q_state[target].im;
        total = 0;
        for (i = 0; i < N; i++) {
            total += q_state[i].re;
        }
        avg2 = (int64_t)((total >> n_qubits) * 2);
        for (i = 0; i < N; i++) {
            q_state[i].re = avg2 - q_state[i].re;
        }
    }
    {
        unsigned int best = 0;
        int64_t best_p = -1;
        for (i = 0; i < N; i++) {
            int64_t p = q_state[i].re * q_state[i].re;
            if (p > best_p) { best_p = p; best = i; }
        }
        *out_result = (int)best;
    }
    return 0;
}

static int q_measure_qubit(unsigned int q, int *out)
{
    unsigned int mask, i;
    __int128 p0 = 0, p1 = 0, p_keep;
    uint64_t total, chosen;
    u64 r64;
    int result;
    uint64_t s, r, b;
    if (q >= N_QUBITS) return -EINVAL;
    mask = 1u << q;
    for (i = 0; i < DIM; i++) {
        __int128 re = q_state[i].re, im = q_state[i].im;
        __int128 p = (re*re + im*im) >> (2*QSHIFT - 30);
        if (i & mask) p1 += p; else p0 += p;
    }
    total = (uint64_t)(p0 + p1);
    if (total == 0) return -EINVAL;
    r64 = get_random_u64();
    chosen = (uint64_t)(((__uint128_t)r64 * (uint64_t)total) >> 64);
    result = (chosen < (uint64_t)p0) ? 0 : 1;
    p_keep = (result == 0) ? p0 : p1;
    if (p_keep <= 0) {
        result = 1 - result;
        p_keep = (result == 0) ? p0 : p1;
        if (p_keep <= 0) return -EINVAL;
    }
    {
        u64 pk = (u64)p_keep;
        u64 t = isqrt64(pk);
        u64 s;
        if (t == 0) t = 1;
        s = (1ULL << 45) / t;
        for (i = 0; i < DIM; i++) {
            int bit = (i & mask) ? 1 : 0;
            if (bit != result) {
                q_state[i].re = 0; q_state[i].im = 0;
            } else {
                q_state[i].re = (int64_t)(((__int128)q_state[i].re * s) >> 30);
                q_state[i].im = (int64_t)(((__int128)q_state[i].im * s) >> 30);
            }
        }
    }
    *out = result;
    return 0;
}

static int q_measure_all(unsigned int *out)
{
    unsigned int i;
    __int128 acc = 0, total = 0;
    uint64_t chosen;
    u64 r64;
    unsigned int result = 0;
    for (i = 0; i < DIM; i++) {
        __int128 re = q_state[i].re, im = q_state[i].im;
        total += (re*re + im*im) >> (2*QSHIFT - 30);
    }
    if (total == 0) return -EINVAL;
    r64 = get_random_u64();
    chosen = (uint64_t)(((__uint128_t)r64 * (uint64_t)total) >> 64);
    for (i = 0; i < DIM; i++) {
        __int128 re = q_state[i].re, im = q_state[i].im;
        acc += (re*re + im*im) >> (2*QSHIFT - 30);
        if ((uint64_t)acc > chosen) { result = i; break; }
    }
    for (i = 0; i < DIM; i++) { q_state[i].re = 0; q_state[i].im = 0; }
    q_state[result].re = QONE;
    q_state[result].im = 0;
    *out = result;
    return 0;
}

static void q_init_zero(void)
{
    unsigned int i;
    for (i = 0; i < DIM; i++) {
        q_state[i].re = 0;
        q_state[i].im = 0;
    }
    q_state[0].re = QONE;
}


/* ── H 閘（workqueue 版） ── */
struct h_ctx { unsigned int q; };

static void h_chunk(void *ctx, int chunk, int n_chunks)
{
    struct h_ctx *hc = ctx;
    unsigned int q = hc->q;
    unsigned int mask = 1u << q;
    unsigned int block = mask << 1;
    unsigned int n_blocks = DIM / block;
    unsigned int per = n_blocks / (unsigned int)n_chunks;
    unsigned int start = (unsigned int)chunk * per;
    unsigned int end = (chunk == n_chunks - 1) ? n_blocks : start + per;
    unsigned int b, off;

    for (b = start; b < end; b++) {
        unsigned int base = b * block;
        for (off = 0; off < mask; off++) {
            unsigned int i = base + off;
            unsigned int j = i | mask;
            if ((off & 63) == 0) {
                __builtin_prefetch(&q_state[i + 512], 0, 1);
                __builtin_prefetch(&q_state[j + 512], 1, 1);
            }
            int64_t ar = q_state[i].re, ai = q_state[i].im;
            int64_t br = q_state[j].re, bi = q_state[j].im;
            int64_t sr = ((int64_t)(ar + br)) * QINV_SQRT2;
            int64_t si = ((int64_t)(ai + bi)) * QINV_SQRT2;
            int64_t dr = ((int64_t)(ar - br)) * QINV_SQRT2;
            int64_t di = ((int64_t)(ai - bi)) * QINV_SQRT2;
            q_state[i].re = (int32_t)((sr + (1LL << (QSHIFT-1))) >> QSHIFT);
            q_state[i].im = (int32_t)((si + (1LL << (QSHIFT-1))) >> QSHIFT);
            q_state[j].re = (int32_t)((dr + (1LL << (QSHIFT-1))) >> QSHIFT);
            q_state[j].im = (int32_t)((di + (1LL << (QSHIFT-1))) >> QSHIFT);
        }
    }
}

static void q_apply_h(unsigned int q)
{
    struct h_ctx hc;
    unsigned int mask, block, n_blocks;
    int nw;
    if (q >= N_QUBITS) return;
    mask = 1u << q;
    block = mask << 1;
    n_blocks = DIM / block;
    nw = g_n_workers;
    if ((unsigned int)nw > n_blocks) nw = (int)n_blocks;
    if (nw < 1) nw = 1;
    hc.q = q;
    run_parallel(h_chunk, &hc, nw);
}

static void q_apply_h_all(void)
{
    unsigned int q;
    for (q = 0; q < N_QUBITS; q++) q_apply_h(q);
}


/* ── 快速設均勻疊加態 ── */
static void q_set_uniform(void)
{
    unsigned int i;
    int64_t amp = QONE >> (N_QUBITS / 2);
    for (i = 0; i < DIM; i++) {
        q_state[i].re = amp;
        q_state[i].im = 0;
    }
}

/* ── CNOT ──────────────────────────────────────── */
static void q_apply_cnot(unsigned int ctrl, unsigned int tgt)
{
    unsigned int i, cmask = 1u << ctrl, tmask = 1u << tgt;
    if (ctrl >= N_QUBITS || tgt >= N_QUBITS || ctrl == tgt) return;

    for (i = 0; i < DIM; i++) {
        if (!(i & cmask)) continue;
        if (i & tmask) continue;
        unsigned int j = i | tmask;
        qcplx_t tmp = q_state[i];
        q_state[i] = q_state[j];
        q_state[j] = tmp;
    }
}

/* ── X 閘 (bit flip) ────────────────────────────── */
static void q_apply_x(unsigned int q)
{
    unsigned int i, mask = 1u << q;
    if (q >= N_QUBITS) return;
    for (i = 0; i < DIM; i++) {
        if (i & mask) continue;
        unsigned int j = i | mask;
        qcplx_t tmp = q_state[i];
        q_state[i] = q_state[j];
        q_state[j] = tmp;
    }
}

/* ── Y 閘 (bit+phase flip) ──────────────────────── */
static void q_apply_y(unsigned int q)
{
    unsigned int i, mask = 1u << q;
    if (q >= N_QUBITS) return;
    for (i = 0; i < DIM; i++) {
        if (i & mask) continue;
        unsigned int j = i | mask;
        int64_t a_re = q_state[i].re, a_im = q_state[i].im;
        int64_t b_re = q_state[j].re, b_im = q_state[j].im;
        /* Y: |0>→i|1>, |1>→-i|0> */
        /* a' = i*b, b' = -i*a */
        q_state[i].re = -b_im;
        q_state[i].im =  b_re;
        q_state[j].re =  a_im;
        q_state[j].im = -a_re;
    }
}


/* ── Z 閘（workqueue 版） ── */
struct z_ctx { unsigned int q; };

static void z_chunk(void *ctx, int chunk, int n_chunks)
{
    struct z_ctx *zc = ctx;
    unsigned int mask = 1u << zc->q;
    unsigned int per = DIM / (unsigned int)n_chunks;
    unsigned int start = (unsigned int)chunk * per;
    unsigned int end = (chunk == n_chunks - 1) ? DIM : start + per;
    unsigned int i;
    for (i = start; i < end; i++) {
        if (!(i & mask)) continue;
        q_state[i].re = -q_state[i].re;
        q_state[i].im = -q_state[i].im;
    }
}

static void q_apply_z(unsigned int q)
{
    struct z_ctx zc;
    if (q >= N_QUBITS) return;
    zc.q = q;
    run_parallel(z_chunk, &zc, g_n_workers);
}

static void q_apply_s(unsigned int q)
{
    unsigned int i, mask = 1u << q;
    if (q >= N_QUBITS) return;
    for (i = 0; i < DIM; i++) {
        if (!(i & mask)) continue;
        /* S: |1> → i|1> */
        int64_t re = q_state[i].re, im = q_state[i].im;
        q_state[i].re = -im;
        q_state[i].im =  re;
    }
}

/* ── T 閘 (π/8 相位) ────────────────────────────── */
static void q_apply_t(unsigned int q)
{
    unsigned int i, mask = 1u << q;
    if (q >= N_QUBITS) return;
    /* T 需要 cos45+sin45i = (1+i)/√2 */
    for (i = 0; i < DIM; i++) {
        if (!(i & mask)) continue;
        int64_t re = q_state[i].re, im = q_state[i].im;
        int64_t nr = ((int64_t)(re - im)) * QINV_SQRT2;
        int64_t ni = ((int64_t)(re + im)) * QINV_SQRT2;
        q_state[i].re = (int32_t)(nr >> QSHIFT);
        q_state[i].im = (int32_t)(ni >> QSHIFT);
    }
}

/* ── Toffoli (CCX) ─────────────────────────────── */
static void q_apply_toffoli(unsigned int c1, unsigned int c2, unsigned int tgt)
{
    unsigned int i;
    unsigned int m1 = 1u << c1, m2 = 1u << c2, mt = 1u << tgt;
    if (c1 >= N_QUBITS || c2 >= N_QUBITS || tgt >= N_QUBITS) return;

    for (i = 0; i < DIM; i++) {
        if ((i & m1) && (i & m2)) {
            if (i & mt) continue;
            unsigned int j = i | mt;
            qcplx_t tmp = q_state[i];
            q_state[i] = q_state[j];
            q_state[j] = tmp;
        }
    }
}

/* ── norm² (整數) ───────────────────────────────── */
static int64_t q_norm_sq_q30(void)
{
    unsigned int i;
    int64_t s = 0;
    for (i = 0; i < DIM; i++) {
        int64_t re = q_state[i].re;
        int64_t im = q_state[i].im;
        s += (re * re + im * im) >> (2 * QSHIFT - 30);
    }
    return s;
}

/* ── 部分跡：把後 NB 個 qubit 跡掉，留前 NA 個 ──── */
static int q_partial_trace(unsigned int NA, int64_t __user *out)
{
    unsigned int NB, DA, DB, i, j, k;
    int64_t *rho;

    if (NA == 0 || NA >= N_QUBITS) return -EINVAL;
    NB = N_QUBITS - NA;
    DA = 1u << NA;
    DB = 1u << NB;

    rho = (int64_t *)vzalloc((size_t)DA * DA * 2 * sizeof(int64_t));
    if (!rho) return -ENOMEM;

    /* rho[i,j] = Σ_k ψ(i<<NB | k) · ψ*(j<<NB | k) */
    for (i = 0; i < DA; i++) {
        for (j = 0; j < DA; j++) {
            __int128 s_re = 0, s_im = 0;
            for (k = 0; k < DB; k++) {
                int64_t ar = q_state[(i << NB) | k].re;
                int64_t ai = q_state[(i << NB) | k].im;
                int64_t br = q_state[(j << NB) | k].re;
                int64_t bi = q_state[(j << NB) | k].im;
                /* a * conj(b) */
                s_re += ((__int128)ar * br + (__int128)ai * bi);
                s_im += ((__int128)ai * br - (__int128)ar * bi);
            }
            /* 從 Q60.60 降到 Q30.30 */
            rho[(i * DA + j) * 2 + 0] = (int64_t)(s_re >> QSHIFT);
            rho[(i * DA + j) * 2 + 1] = (int64_t)(s_im >> QSHIFT);
        }
    }

    if (copy_to_user(out, rho, (size_t)DA * DA * 2 * sizeof(int64_t))) {
        vfree(rho);
        return -EFAULT;
    }
    vfree(rho);
    return 0;
}

/* ── ioctl ─────────────────────────────────────── */
static long q_ioctl(struct file *f, unsigned int cmd, unsigned long arg)
{
    int ret = 0;
    unsigned int x;

    mutex_lock(&q_lock);
    switch (cmd) {
    case Q_INIT_ZERO:
        q_init_zero();
        pr_info("g116_quantum: init |0...0>\n"); break;
    case Q_APPLY_H_ALL:
        q_apply_h_all();
        pr_info("g116_quantum: H×%d\n", N_QUBITS); break;
    case Q_APPLY_H_Q:
        if (copy_from_user(&x, (void __user *)arg, sizeof(x))) { ret=-EFAULT; break; }
        q_apply_h(x); break;
    case Q_APPLY_CNOT:
        if (copy_from_user(&x, (void __user *)arg, sizeof(x))) { ret=-EFAULT; break; }
        q_apply_cnot(x >> 8, x & 0xFF); break;
    case Q_APPLY_X:
        if (copy_from_user(&x, (void __user *)arg, sizeof(x))) { ret=-EFAULT; break; }
        q_apply_x(x); break;
    case Q_APPLY_Y:
        if (copy_from_user(&x, (void __user *)arg, sizeof(x))) { ret=-EFAULT; break; }
        q_apply_y(x); break;
    case Q_APPLY_Z:
        if (copy_from_user(&x, (void __user *)arg, sizeof(x))) { ret=-EFAULT; break; }
        q_apply_z(x); break;
    case Q_APPLY_S:
        if (copy_from_user(&x, (void __user *)arg, sizeof(x))) { ret=-EFAULT; break; }
        q_apply_s(x); break;
    case Q_APPLY_T:
        if (copy_from_user(&x, (void __user *)arg, sizeof(x))) { ret=-EFAULT; break; }
        q_apply_t(x); break;
    case Q_APPLY_TOFFOLI:
        if (copy_from_user(&x, (void __user *)arg, sizeof(x))) { ret=-EFAULT; break; }
        q_apply_toffoli((x >> 16) & 0xFF, (x >> 8) & 0xFF, x & 0xFF); break;
    case Q_GET_AMPL: {
        struct qampl_req r;
        if (copy_from_user(&r, (void __user *)arg, sizeof(r))) { ret=-EFAULT; break; }
        if (r.idx >= DIM) { ret=-EINVAL; break; }
        r.re_q30 = q_state[r.idx].re;
        r.im_q30 = q_state[r.idx].im;
        if (copy_to_user((void __user *)arg, &r, sizeof(r))) ret=-EFAULT;
        break;
    }
    case Q_GET_PROB: {
        struct qprob_req r;
        if (copy_from_user(&r, (void __user *)arg, sizeof(r))) { ret=-EFAULT; break; }
        if (r.idx >= DIM) { ret=-EINVAL; break; }
        int64_t re = q_state[r.idx].re, im = q_state[r.idx].im;
        r.prob_q30 = (re*re + im*im) >> (2*QSHIFT - 30);
        if (copy_to_user((void __user *)arg, &r, sizeof(r))) ret=-EFAULT;
        break;
    }
    case Q_GET_INFO: {
        struct qinfo_req r;
        r.n_qubits = N_QUBITS;
        r.dim = DIM;
        r.norm_sq_q30 = q_norm_sq_q30();
        if (copy_to_user((void __user *)arg, &r, sizeof(r))) ret=-EFAULT;
        break;
    }
    case Q_PARTIAL_TRACE: {
        struct qpt_req r;
        if (copy_from_user(&r, (void __user *)arg, sizeof(r))) { ret=-EFAULT; break; }
        ret = q_partial_trace(r.n_keep, r.rho_out);
        break;
    }
    case Q_LOAD_BITS: {
        struct qload_bits_req r;
        if (copy_from_user(&r, (void __user *)arg, sizeof(r))) { ret=-EFAULT; break; }
        ret = q_load_bits(r.n_qubits, r.data);
        if (ret == 0)
            pr_info("g116_quantum: load_bits n=%u\n", r.n_qubits);
        break;
    }
    case Q_GROVER_PATTERN: {
        struct qgpat_req r;
        if (copy_from_user(&r, (void __user *)arg, sizeof(r))) { ret=-EFAULT; break; }
        ret = q_grover_pattern(r.n_qubits, r.pattern, r.pattern_bits,
                               &r.iterations, r.data, &r.result, &r.marked_count);
        if (ret == 0) {
            if (copy_to_user((void __user *)arg, &r, sizeof(r))) ret = -EFAULT;
            pr_info("g116_quantum: grover_pat n=%u pat=0x%x bits=%u marked=%u -> %d\n",
                    r.n_qubits, r.pattern, r.pattern_bits, r.marked_count, r.result);
        }
        break;
    }
    case Q_GROVER_MATRIX: {
        struct qgrover_matrix_req r;
        if (copy_from_user(&r, (void __user *)arg, sizeof(r))) { ret=-EFAULT; break; }
        ret = q_grover_matrix(r.n_qubits, &r.iterations, r.data, &r.result, &r.marked_count);
        if (ret == 0) {
            if (copy_to_user((void __user *)arg, &r, sizeof(r))) ret = -EFAULT;
            pr_info("g116_quantum: grover_matrix n=%u marked=%u -> %d\n",
                    r.n_qubits, r.marked_count, r.result);
        }
        break;
    }
    case Q_APPLY_FUSED_HXCNOTH:
        q_apply_fused_hxcnot();
        return 0;
    case Q_QFT_RUN: {
        struct qqft_req r;
        if (copy_from_user(&r, (void __user *)arg, sizeof(r))) { ret=-EFAULT; break; }
        ret = q_qft_run(r.n_qubits);
        if (ret == 0)
            pr_info("g116_quantum: QFT n=%u\n", r.n_qubits);
        break;
    }
    case Q_GROVER_RUN: {
        struct qgrover_req r;
        if (copy_from_user(&r, (void __user *)arg, sizeof(r))) { ret=-EFAULT; break; }
        ret = q_grover_run(r.n_qubits, r.target, &r.iterations, &r.result);
        if (ret == 0) {
            r.found = ((unsigned int)r.result == r.target) ? 1u : 0u;
            if (copy_to_user((void __user *)arg, &r, sizeof(r))) ret = -EFAULT;
            pr_info("g116_quantum: grover n=%u tgt=%u it=%u -> %d (%s)\n",
                    r.n_qubits, r.target, r.iterations, r.result,
                    r.found ? "FOUND" : "MISS");
        }
        break;
    }
    case Q_MEASURE_QUBIT: {
        struct qmeasure_req r;
        if (copy_from_user(&r, (void __user *)arg, sizeof(r))) { ret=-EFAULT; break; }
        ret = q_measure_qubit(r.qubit, &r.result);
        if (ret == 0) {
            if (copy_to_user((void __user *)arg, &r, sizeof(r))) ret = -EFAULT;
            pr_info("g116_quantum: measure q%u = %d\n", r.qubit, r.result);
        }
        break;
    }
    case Q_MEASURE_ALL: {
        unsigned int result;
        ret = q_measure_all(&result);
        if (ret == 0) {
            if (copy_to_user((void __user *)arg, &result, sizeof(result))) ret = -EFAULT;
            pr_info("g116_quantum: measure all = %u\n", result);
        }
        break;
    }
    case Q_SET_UNIFORM:
        q_set_uniform();
        pr_info("g116_quantum: set uniform\n");
        break;
    case Q_LOAD_STATE: {
        struct qload_req r;
        if (copy_from_user(&r, (void __user *)arg, sizeof(r))) { ret=-EFAULT; break; }
        if (r.offset + r.count > DIM) { ret=-EINVAL; break; }
        int64_t *re_buf = (int64_t *)vmalloc(r.count * sizeof(int64_t));
        int64_t *im_buf = (int64_t *)vmalloc(r.count * sizeof(int64_t));
        if (!re_buf || !im_buf) {
            vfree(re_buf); vfree(im_buf);
            ret = -ENOMEM; break;
        }
        if (copy_from_user(re_buf, (void __user *)r.data_re, r.count * sizeof(int64_t)) ||
            copy_from_user(im_buf, (void __user *)r.data_im, r.count * sizeof(int64_t))) {
            vfree(re_buf); vfree(im_buf);
            ret = -EFAULT; break;
        }
        for (unsigned int i = 0; i < r.count; i++) {
            q_state[r.offset + i].re = re_buf[i];
            q_state[r.offset + i].im = im_buf[i];
        }
        vfree(re_buf); vfree(im_buf);
        pr_info("g116_quantum: 載入 %u 個狀態 (offset=%u)\n", r.count, r.offset);
        break;
    }
    default:
        ret = -EINVAL;
    }
    mutex_unlock(&q_lock);
    return ret;
}

static int q_open(struct inode *i, struct file *f)
{ pr_info("g116_quantum: open\n"); return 0; }
static int q_release(struct inode *i, struct file *f)
{ pr_info("g116_quantum: close\n"); return 0; }

static struct file_operations q_fops = {
    .owner = THIS_MODULE,
    .open = q_open,
    .release = q_release,
    .unlocked_ioctl = q_ioctl,
};

static int __init g116_quantum_init(void)
{
    int ret;
    size_t sz = (size_t)DIM * sizeof(qcplx_t);

    pr_info("g116_quantum: init N=%d DIM=%llu %zu KB\n", N_QUBITS, DIM, sz/1024);

    g_n_workers = num_online_cpus();
    if (g_n_workers > MAX_WORKERS) g_n_workers = MAX_WORKERS;
    if (g_n_workers < 1) g_n_workers = 1;
    wq_init();
    pr_info("g116_quantum: workers=%d\n", g_n_workers);
    q_state = (qcplx_t *)vzalloc(sz);
    if (!q_state) return -ENOMEM;
    q_init_zero();

    ret = alloc_chrdev_region(&q_devno, 0, 1, DEV_NAME);
    if (ret < 0) { vfree(q_state); return ret; }

    cdev_init(&q_cdev, &q_fops);
    q_cdev.owner = THIS_MODULE;
    ret = cdev_add(&q_cdev, q_devno, 1);
    if (ret < 0) { unregister_chrdev_region(q_devno,1); vfree(q_state); return ret; }

    q_class = class_create(DEV_NAME);
    if (IS_ERR(q_class)) {
        cdev_del(&q_cdev); unregister_chrdev_region(q_devno,1);
        vfree(q_state); return PTR_ERR(q_class);
    }
    q_device = device_create(q_class, NULL, q_devno, NULL, DEV_NAME);
    if (IS_ERR(q_device)) {
        class_destroy(q_class); cdev_del(&q_cdev);
        unregister_chrdev_region(q_devno,1); vfree(q_state);
        return PTR_ERR(q_device);
    }
    pr_info("g116_quantum: OK /dev/%s\n", DEV_NAME);
    return 0;
}

static void __exit g116_quantum_exit(void)
{
    device_destroy(q_class, q_devno);
    class_destroy(q_class);
    cdev_del(&q_cdev);
    unregister_chrdev_region(q_devno, 1);
    if (q_state) vfree(q_state);
    wq_exit();
    pr_info("g116_quantum: unloaded\n");
}

module_init(g116_quantum_init);
module_exit(g116_quantum_exit);

MODULE_LICENSE("GPL");
static void q_apply_fused_hxcnot(void)
{
    const int64_t sq = 759250125LL;
    const int64_t half = 1LL << (QSHIFT - 1);
    uint64_t base;
    for (base = 0; base < DIM; base += 4) {
        if ((base & 255) == 0 && base + 512 < DIM) {
            __builtin_prefetch(&q_state[base + 256], 1, 3);
            __builtin_prefetch(&q_state[base + 384], 1, 3);
        }
        int64_t r0 = q_state[base+0].re, i0 = q_state[base+0].im;
        int64_t r1 = q_state[base+1].re, i1 = q_state[base+1].im;
        int64_t r2 = q_state[base+2].re, i2 = q_state[base+2].im;
        int64_t r3 = q_state[base+3].re, i3 = q_state[base+3].im;
        q_state[base+0].re = (int32_t)((sq * (r1 - r3) + half) >> QSHIFT);
        q_state[base+0].im = (int32_t)((sq * (i1 - i3) + half) >> QSHIFT);
        q_state[base+1].re = (int32_t)((sq * (r0 + r2) + half) >> QSHIFT);
        q_state[base+1].im = (int32_t)((sq * (i0 + i2) + half) >> QSHIFT);
        q_state[base+2].re = (int32_t)((sq * (r1 + r3) + half) >> QSHIFT);
        q_state[base+2].im = (int32_t)((sq * (i1 + i3) + half) >> QSHIFT);
        q_state[base+3].re = (int32_t)((sq * (r0 - r2) + half) >> QSHIFT);
        q_state[base+3].im = (int32_t)((sq * (i0 - i2) + half) >> QSHIFT);
    }
}
