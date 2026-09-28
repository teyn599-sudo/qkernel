#include "mm64.h"

#define PAGE_SIZE   4096ULL
#define MAX_PAGES   (1ULL << 22)          /* 16 GB 上限 */
static uint8_t  g_bm[MAX_PAGES / 8];
static uint64_t g_pmm_base, g_pmm_pages, g_pmm_last;

static inline int  bm_t(uint64_t i){ return (g_bm[i>>3] >> (i&7)) & 1; }
static inline void bm_s(uint64_t i){ g_bm[i>>3] |=  (1u << (i&7)); }
static inline void bm_c(uint64_t i){ g_bm[i>>3] &= ~(1u << (i&7)); }

void pmm_init(void){
    g_pmm_base  = g_low_base;
    g_pmm_pages = g_low_size / PAGE_SIZE;
    if (g_pmm_pages > MAX_PAGES) g_pmm_pages = MAX_PAGES;
    for (uint64_t i = 0; i < sizeof(g_bm); i++) g_bm[i] = 0xFF;
    /* 前 16 MB 保留給 kernel */
    uint64_t start = (16ULL << 20) / PAGE_SIZE;
    if (start > g_pmm_pages) start = g_pmm_pages;
    for (uint64_t i = start; i < g_pmm_pages; i++) bm_c(i);
    g_pmm_last = start;
}

uint64_t pmm_alloc(void){
    for (uint64_t i = 0; i < g_pmm_pages; i++){
        uint64_t p = (g_pmm_last + i) % g_pmm_pages;
        if (!bm_t(p)){
            bm_s(p);
            g_pmm_last = (p + 1) % g_pmm_pages;
            return g_pmm_base + p * PAGE_SIZE;
        }
    }
    return 0;
}

void pmm_free(uint64_t pa){
    if (pa < g_pmm_base) return;
    uint64_t i = (pa - g_pmm_base) / PAGE_SIZE;
    if (i >= g_pmm_pages) return;
    bm_c(i);
}

uint64_t pmm_free_pages(void){
    uint64_t n = 0;
    for (uint64_t i = 0; i < g_pmm_pages; i++) if (!bm_t(i)) n++;
    return n;
}

uint64_t pmm_total_pages(void){ return g_pmm_pages; }

/* ---- kmalloc：16 個 size class（16B ~ 512KB）---- */
#define KM_CLASSES  16
#define KM_MAGIC    0xCAFEBABEu
struct km_hdr { uint32_t cls; uint32_t magic; };
static struct km_hdr *g_km_free[KM_CLASSES];

static uint64_t cs(int c){ return 16ULL << c; }

void *kmalloc(uint64_t sz){
    if (!sz) return 0;
    int c = -1;
    for (int i = 0; i < KM_CLASSES; i++) if (sz <= cs(i)){ c = i; break; }
    if (c < 0) return 0;
    if (g_km_free[c]){
        struct km_hdr *h = g_km_free[c];
        g_km_free[c] = *(struct km_hdr**)h;
        return (void*)((uint8_t*)h + 16);
    }
    uint64_t pa = pmm_alloc();
    if (!pa) return 0;
    uint64_t csz = cs(c), n = PAGE_SIZE / csz;
    uint8_t *base = (uint8_t*)pa;
    for (uint64_t i = 0; i < n; i++){
        struct km_hdr *h = (struct km_hdr*)(base + i*csz);
        h->cls = (uint32_t)c; h->magic = KM_MAGIC;
        *(struct km_hdr**)h = g_km_free[c];
        g_km_free[c] = h;
    }
    return kmalloc(sz);
}

void kfree(void *p){
    if (!p) return;
    struct km_hdr *h = (struct km_hdr*)((uint8_t*)p - 16);
    if (h->magic != KM_MAGIC) return;
    uint32_t c = h->cls;
    if (c >= KM_CLASSES) return;
    *(struct km_hdr**)h = g_km_free[c];
    g_km_free[c] = h;
}
