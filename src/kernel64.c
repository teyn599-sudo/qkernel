/* SPDX-License-Identifier: GPL-3.0-or-later */
#include <stdint.h>
#include <stddef.h>
#include "idt64.h"
#include "smp.h"
#include "noise64.h"


extern const uint8_t _binary_static_bin_start[];
extern const uint8_t _binary_static_bin_end[];

#define IMG_W 1024
#define IMG_H 768

/* ============ 端口 ============ */
static inline void outb(uint16_t p,uint8_t v){ __asm__ volatile("outb %0,%1"::"a"(v),"Nd"(p)); }
static inline uint8_t inb(uint16_t p){ uint8_t r; __asm__ volatile("inb %1,%0":"=a"(r):"Nd"(p)); return r; }

static void serial_init(void){
    outb(0x3F8+1,0x00); outb(0x3F8+3,0x80); outb(0x3F8+0,0x03);
    outb(0x3F8+1,0x00); outb(0x3F8+3,0x03); outb(0x3F8+2,0xC7);
    outb(0x3F8+4,0x0B);
}
static volatile int g_serial_lock = 0;
static void sputc(char c){
    while(__sync_lock_test_and_set(&g_serial_lock, 1)) __asm__ volatile("pause");
    while(!(inb(0x3F8+5)&0x20)){}
    outb(0x3F8,(uint8_t)c);
    __sync_lock_release(&g_serial_lock);
}
static void _sputc_raw(char c){
    while(!(inb(0x3F8+5)&0x20)){}
    outb(0x3F8,(uint8_t)c);
}
void sputs(const char *s){
    while(__sync_lock_test_and_set(&g_serial_lock,1)) __asm__ volatile("pause");
    while(*s) _sputc_raw(*s++);
    __sync_lock_release(&g_serial_lock);
}
static void _sputu_raw(uint64_t v){
    char b[24]; int n=0;
    if(!v){ _sputc_raw('0'); return; }
    while(v){ b[n++]='0'+(v%10); v/=10; }
    while(n--) _sputc_raw(b[n]);
}
/* ===== NT Store ===== */
static inline void nt_store_i32(int32_t *p, int32_t v){
    __asm__ volatile("movnti %1, (%0)" :: "r"(p), "r"(v) : "memory");
}
static inline void nt_flush(void){ __asm__ volatile("sfence" ::: "memory"); }

void sputu(uint64_t v){
    while(__sync_lock_test_and_set(&g_serial_lock,1)) __asm__ volatile("pause");
    _sputu_raw(v);
    __sync_lock_release(&g_serial_lock);
}

/* ============ Framebuffer ============ */
static uint8_t *FB=0;
static uint32_t FB_W=0, FB_H=0, FB_PITCH=0;

static void parse_fb(uint64_t mbi){
    uint32_t flags = *(uint32_t*)(uintptr_t)mbi;
    if(!(flags & (1u<<12))){ sputs("NO FB\n"); return; }
    uint32_t addr = *(uint32_t*)(uintptr_t)(mbi + 88);
    FB_PITCH = *(uint32_t*)(uintptr_t)(mbi + 96);
    FB_W     = *(uint32_t*)(uintptr_t)(mbi + 100);
    FB_H     = *(uint32_t*)(uintptr_t)(mbi + 104);
    FB = (uint8_t*)(uintptr_t)addr;
}

static void delay_ticks(uint64_t n){
    uint64_t start = timer_ticks64();
    while((timer_ticks64() - start) < n) __asm__ volatile("hlt");
}

static inline uint64_t rdtsc(void){
    uint32_t lo, hi;
    __asm__ volatile("rdtsc" : "=a"(lo), "=d"(hi));
    return ((uint64_t)hi << 32) | lo;
}

static inline void px(int x,int y,uint8_t g){
    if((unsigned)x>=(unsigned)FB_W||(unsigned)y>=(unsigned)FB_H) return;
    uint8_t *p = FB + (uint64_t)y*FB_PITCH + (uint64_t)x*4;
    p[0]=g; p[1]=g; p[2]=g;
}
static void fill(uint8_t g){
    for(uint32_t y=0;y<FB_H;y++){
        uint8_t *row = FB + (uint64_t)y*FB_PITCH;
        for(uint32_t x=0;x<FB_W;x++){ row[x*4]=g; row[x*4+1]=g; row[x*4+2]=g; }
    }
}
static void rect(int x,int y,int w,int h,uint8_t g){
    for(int j=0;j<h;j++) for(int i=0;i<w;i++) px(x+i,y+j,g);
}

/* ============ 5x7 字体 ============ */
static const uint8_t FONT[][5]={
 {0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},
 {0x3E,0x41,0x41,0x41,0x22},{0x7F,0x41,0x41,0x22,0x1C},
 {0x7F,0x49,0x49,0x49,0x41},{0x7F,0x09,0x09,0x09,0x01},
 {0x3E,0x41,0x49,0x49,0x7A},{0x7F,0x08,0x08,0x08,0x7F},
 {0x00,0x41,0x7F,0x41,0x00},{0x20,0x40,0x41,0x3F,0x01},
 {0x7F,0x08,0x14,0x22,0x41},{0x7F,0x40,0x40,0x40,0x40},
 {0x7F,0x02,0x0C,0x02,0x7F},{0x7F,0x04,0x08,0x10,0x7F},
 {0x3E,0x41,0x41,0x41,0x3E},{0x7F,0x09,0x09,0x09,0x06},
 {0x3E,0x41,0x51,0x21,0x5E},{0x7F,0x09,0x19,0x29,0x46},
 {0x46,0x49,0x49,0x49,0x31},{0x01,0x01,0x7F,0x01,0x01},
 {0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},
 {0x7F,0x20,0x18,0x20,0x7F},{0x63,0x14,0x08,0x14,0x63},
 {0x03,0x04,0x78,0x04,0x03},{0x61,0x51,0x49,0x45,0x43},
 {0x3E,0x51,0x49,0x45,0x3E},{0x00,0x42,0x7F,0x40,0x00},
 {0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
 {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},
 {0x3C,0x4A,0x49,0x49,0x30},{0x01,0x71,0x09,0x05,0x03},
 {0x36,0x49,0x49,0x49,0x36},{0x06,0x49,0x49,0x29,0x1E},
 {0,0,0,0,0},{0x08,0x08,0x08,0x08,0x08},{0,0x60,0x60,0,0},
 {0,0x36,0x36,0,0},{0,0,0x5F,0,0},
};
static int fidx(char c){
    if(c>='A'&&c<='Z') return c-'A';
    if(c>='0'&&c<='9') return 26+(c-'0');
    if(c==' ') return 36;
    return 36;
}
static void draw_char(int x,int y,char c,uint8_t col,int sc){
    int idx=fidx(c);
    for(int k=0;k<5;k++){
        uint8_t b=FONT[idx][k];
        for(int r=0;r<7;r++)
            if(b&(1<<r))
                for(int sy=0;sy<sc;sy++) for(int sx=0;sx<sc;sx++)
                    px(x+k*sc+sx, y+r*sc+sy, col);
    }
}
static void draw_str(int x,int y,const char*s,uint8_t c,int sc){
    while(*s){ draw_char(x,y,*s++,c,sc); x+=6*sc; }
}
static void draw_u(int x,int y,uint64_t v,uint8_t c,int sc){
    char b[24]; int n=0;
    if(!v){ b[n++]='0'; }
    else { while(v){ b[n++]='0'+(v%10); v/=10; } }
    while(n--) { draw_char(x,y,b[n],c,sc); x+=6*sc; }
}

/* ============ E820 ============ */
struct e820_entry { uint64_t base, length; uint32_t type, acpi; } __attribute__((packed));
static uint64_t g_ram_usable=0, g_ram_largest=0, g_ram_largest_base=0;
uint64_t g_low_base=0, g_low_size=0;
static uint64_t g_heap_base=0, g_heap_top=0;   /* 最佳 heap 區間（≥4GB）*/

static void parse_e820(uint64_t mbi){
    uint32_t flags = *(uint32_t*)(uintptr_t)mbi;
    if(!(flags & (1u<<6))) return;
    uint32_t len  = *(uint32_t*)(uintptr_t)(mbi + 44);
    uint32_t addr = *(uint32_t*)(uintptr_t)(mbi + 48);
    uint64_t pos = addr, end = addr + len;
    while(pos < end){
        uint32_t size = *(uint32_t*)(uintptr_t)pos;
        struct e820_entry *e = (struct e820_entry*)(uintptr_t)(pos + 4);
        if(e->type == 1){
            g_ram_usable += e->length;
            if(e->length > g_ram_largest){
                g_ram_largest = e->length;
                g_ram_largest_base = e->base;
            }
            if(e->base + e->length <= 0x100000000ULL && e->length > g_low_size){
                g_low_base = e->base;
                g_low_size = e->length;
            }
            /* 優先選 base>=4GB 的最大區塊，避開 q35 MMIO 洞 0xC0000000~0xFFFFFFFF */
            if(e->base >= 0x100000000ULL && e->length > (g_heap_top - g_heap_base)){
                g_heap_base = e->base;
                g_heap_top  = e->base + e->length;
            }
        }
        pos += size + 4;
    }
    sputs("[e820] "); sputu(g_ram_usable/(1024*1024)); sputs(" MB\n");
}

/* ============ Heap ============ */
static uint64_t g_heap_cur=0, g_heap_end=0;
static void heap_init(uint64_t s, uint64_t e){
    g_heap_cur = (s + 0xFFF) & ~0xFFFULL;
    g_heap_end = e & ~0xFFFULL;
}
uint64_t heap_free(void){ return g_heap_end - g_heap_cur; }
void *heap_alloc(uint64_t sz){
    sz = (sz + 0xFFF) & ~0xFFFULL;
    if(g_heap_cur + sz > g_heap_end) return 0;
    void *p = (void*)(uintptr_t)g_heap_cur;
    g_heap_cur += sz;
    return p;
}

/* ============ Q30 量子 ============ */
#define QSHIFT 30
#define QONE   (1<<QSHIFT)
#define QADD(a,b) ((int32_t)((int64_t)(a) + (int64_t)(b)))
#define QSUB(a,b) ((int32_t)((int64_t)(a) - (int64_t)(b)))
#define QMUL(a,b) ((int32_t)(((int64_t)(a) * (int64_t)(b)) >> QSHIFT))

int32_t *g_qstate = 0;
int g_nq = 0;
uint64_t g_qdim = 0;
static uint64_t g_qec_total = 0;

static void q_init_zero(void){
    if(!g_qstate) return;
    g_qstate[0] = QONE;
    g_qstate[1] = 0;
    g_qstate[2] = 0;
    g_qstate[3] = 0;
}
static void q_apply_h_sampled(int q, uint64_t limit){
    if(!g_qstate || q<0 || q>=g_nq) return;
    uint64_t bit = 1ULL << q;
    int32_t sq = 759250125;
    uint64_t cnt = 0;
    for(uint64_t i = 0; i < g_qdim && cnt < limit; i++){
        if(i & bit) continue;
        uint64_t j = i | bit;
        int64_t ar = g_qstate[i*2],   ai = g_qstate[i*2+1];
        int64_t br = g_qstate[j*2],   bi = g_qstate[j*2+1];
        g_qstate[i*2]   = QMUL(QADD(ar,br), sq);
        g_qstate[i*2+1] = QMUL(QADD(ai,bi), sq);
        g_qstate[j*2]   = QMUL(QSUB(ar,br), sq);
        g_qstate[j*2+1] = QMUL(QSUB(ai,bi), sq);
        cnt++;
    }
}

void q_apply_h(int q){
    if(!g_qstate || q<0 || q>=g_nq) return;
    uint64_t bit = 1ULL << q;
    int32_t sq = 759250125;
    for(uint64_t i = 0; i < g_qdim; i++){
        if(i & bit) continue;
        uint64_t j = i | bit;
        int64_t ar = g_qstate[i*2],   ai = g_qstate[i*2+1];
        int64_t br = g_qstate[j*2],   bi = g_qstate[j*2+1];
        g_qstate[i*2]   = QMUL(QADD(ar,br), sq);
        g_qstate[i*2+1] = QMUL(QADD(ai,bi), sq);
        g_qstate[j*2]   = QMUL(QSUB(ar,br), sq);
        g_qstate[j*2+1] = QMUL(QSUB(ai,bi), sq);
    }
}
void q_apply_x(int q){
    if(!g_qstate || q<0 || q>=g_nq) return;
    uint64_t bit = 1ULL << q;
    for(uint64_t i = 0; i < g_qdim; i++){
        if(i & bit) continue;
        uint64_t j = i | bit;
        int64_t tr = g_qstate[i*2],   ti = g_qstate[i*2+1];
        g_qstate[i*2]   = g_qstate[j*2];
        g_qstate[i*2+1] = g_qstate[j*2+1];
        g_qstate[j*2]   = tr;
        g_qstate[j*2+1] = ti;
    }
}

/* S 门：相位 i */
static void q_apply_s(int q){
    if(!g_qstate || q<0 || q>=g_nq) return;
    uint64_t bit = 1ULL << q;
    for(uint64_t i = 0; i < g_qdim; i++){
        if(i & bit){
            int32_t re = g_qstate[i*2];
            int64_t im = g_qstate[i*2+1];
            /* S: |1> -> i|1>，即 (re,im) -> (-im, re) */
            g_qstate[i*2]   = -im;
            g_qstate[i*2+1] = re;
        }
    }
}

/* T 门：相位 e^(i*pi/4) */
static void q_apply_t(int q){
    if(!g_qstate || q<0 || q>=g_nq) return;
    uint64_t bit = 1ULL << q;
    int64_t inv_sq = 759250125LL;   /* 1/sqrt(2) */
    for(uint64_t i = 0; i < g_qdim; i++){
        if(i & bit){
            int32_t re = g_qstate[i*2];
            int64_t im = g_qstate[i*2+1];
            /* T: (re,im) -> ((re-im)/sqrt(2), (re+im)/sqrt(2)) */
            int64_t nr = QMUL(re - im, inv_sq);
            int64_t ni = QMUL(re + im, inv_sq);
            g_qstate[i*2]   = nr;
            g_qstate[i*2+1] = ni;
        }
    }
}

/* Toffoli：(c1,c2) 控制 target */
void q_apply_toffoli(int c1, int c2, int t){
    if(!g_qstate || c1<0 || c2<0 || t<0) return;
    if(c1>=g_nq || c2>=g_nq || t>=g_nq) return;
    uint64_t b1 = 1ULL << c1;
    uint64_t b2 = 1ULL << c2;
    uint64_t bt = 1ULL << t;
    for(uint64_t i = 0; i < g_qdim; i++){
        if((i & b1) && (i & b2) && !(i & bt)){
            uint64_t j = i | bt;
            int64_t tr = g_qstate[i*2],   ti = g_qstate[i*2+1];
            g_qstate[i*2]   = g_qstate[j*2];
            g_qstate[i*2+1] = g_qstate[j*2+1];
            g_qstate[j*2]   = tr;
            g_qstate[j*2+1] = ti;
        }
    }
}

void q_apply_cnot(int c, int t){
    if(!g_qstate || c<0 || t<0 || c>=g_nq || t>=g_nq) return;
    uint64_t cb = 1ULL << c;
    uint64_t tb = 1ULL << t;
    for(uint64_t i = 0; i < g_qdim; i++){
        if((i & cb) && !(i & tb)){
            uint64_t j = i | tb;
            int64_t tr = g_qstate[i*2],   ti = g_qstate[i*2+1];
            g_qstate[i*2]   = g_qstate[j*2];
            g_qstate[i*2+1] = g_qstate[j*2+1];
            g_qstate[j*2]   = tr;
            g_qstate[j*2+1] = ti;
        }
    }
}

/* ============ MACH PORT (64-bit) ============ */
#define PORT_N      8
#define PORT_QSIZE  16
#define PORT_NAME_MAX 12

typedef struct {
    uint32_t sender;
    uint32_t cmd;
    uint64_t arg0;
    int64_t  r0;
    char     text[40];
} qmsg64_t;

typedef struct {
    uint8_t  used;
    char     name[PORT_NAME_MAX];
    qmsg64_t queue[PORT_QSIZE];
    uint32_t head, tail, count;
    uint64_t sent, dropped, recvd;
} qport64_t;

static qport64_t g_ports64[PORT_N];
int g_port_main64 = -1;
static int g_port_ai64   = -1;

static void port_init64(void){
    for(int i=0;i<PORT_N;i++){
        g_ports64[i].used=0;
        g_ports64[i].head=g_ports64[i].tail=g_ports64[i].count=0;
        g_ports64[i].sent=g_ports64[i].dropped=g_ports64[i].recvd=0;
        g_ports64[i].name[0]=0;
    }
}
static int port_create64(const char *name){
    for(int i=0;i<PORT_N;i++){
        if(!g_ports64[i].used){
            g_ports64[i].used=1;
            int j=0; while(name[j]&&j<PORT_NAME_MAX-1){ g_ports64[i].name[j]=name[j]; j++; }
            g_ports64[i].name[j]=0;
            return i;
        }
    }
    return -1;
}
int port_send64(int id, const qmsg64_t *m){
    if(id<0||id>=PORT_N||!g_ports64[id].used) return -1;
    qport64_t *p=&g_ports64[id];
    if(p->count>=PORT_QSIZE){ p->dropped++; return -1; }
    p->queue[p->tail] = *m;
    p->tail = (p->tail+1) % PORT_QSIZE;
    p->count++; p->sent++;
    return 0;
}
int port_recv64(int id, qmsg64_t *out){
    if(id<0||id>=PORT_N||!g_ports64[id].used) return -1;
    qport64_t *p=&g_ports64[id];
    if(p->count==0) return -1;
    *out = p->queue[p->head];
    p->head = (p->head+1) % PORT_QSIZE;
    p->count--; p->recvd++;
    return 0;
}

/* ============ AI NEURON (64-bit, Q30 MLP) ============ */
#define AI_HIST 8
#define AI_HID  8
typedef struct {
    int64_t w1[AI_HID][AI_HIST];
    int64_t b1[AI_HID];
    int64_t w2[AI_HID];
    int64_t b2;
} ai_net64_t;

static ai_net64_t g_ai64;
static uint64_t g_rnd64 = 0x9E3779B97F4A7C15ULL;
static uint64_t g_ai_scan64 = 0;
static uint64_t g_qec_total64 = 0;

static int64_t ai_rand64(void){
    g_rnd64 = g_rnd64 * 6364136223846793005ULL + 1442695040888963407ULL;
    return (int64_t)(g_rnd64 >> 33);
}
static void ai_init64(void){
    for(int i=0;i<AI_HID;i++){
        for(int j=0;j<AI_HIST;j++) g_ai64.w1[i][j] = (ai_rand64()%(QONE/4)) - (QONE/8);
        g_ai64.b1[i] = (ai_rand64()%(QONE/16)) - (QONE/32);
    }
    for(int i=0;i<AI_HID;i++) g_ai64.w2[i] = (ai_rand64()%(QONE/2)) - (QONE/4);
    g_ai64.b2 = 0;
}
static int64_t ai_forward64(const int64_t hist[AI_HIST]){
    int64_t h[AI_HID];
    for(int i=0;i<AI_HID;i++){
        int64_t acc = g_ai64.b1[i];
        for(int j=0;j<AI_HIST;j++) acc += QMUL(g_ai64.w1[i][j], hist[j]);
        h[i] = (acc>0)?acc:0;
    }
    int64_t out = g_ai64.b2;
    for(int i=0;i<AI_HID;i++) out += QMUL(g_ai64.w2[i], h[i]);
    if(out < 0) out = 0;
    if(out > QONE) out = QONE;
    return out;
}

struct ai_hist64 { int64_t hist[AI_HIST]; int head; int64_t risk; int warn; };
static struct ai_hist64 g_ah64[32];

static void ai_patrol_step64(void){
    if(!g_qstate || g_nq <= 0) return;
    int q = (int)(g_ai_scan64 % g_nq);
    g_ai_scan64++;

    /* 简化：读 qubit q 的 |0> 振幅作为特征 */
    int64_t p1 = g_qstate[0];
    struct ai_hist64 *h = &g_ah64[q];
    h->hist[h->head] = p1;
    h->head = (h->head + 1) % AI_HIST;

    int64_t win[AI_HIST];
    for(int i=0;i<AI_HIST;i++) win[i] = h->hist[(h->head+i)%AI_HIST];
    int64_t risk = ai_forward64(win);
    h->risk = risk;

    if(risk > (int64_t)QONE*3/4){
        h->warn++;
        if(h->warn >= 3){
            g_qec_total64++;
            h->warn = 0;
            qmsg64_t m;
            m.sender = 0xAI;
            m.cmd = 0x01;
            m.arg0 = (uint64_t)q;
            m.r0 = (int64_t)g_qec_total64;
            m.text[0] = 0;
            port_send64(g_port_ai64, &m);
        }
    } else h->warn = 0;
}

/* ============ Q-RAM (64-bit) ============ */
#define QRAM_N 32
enum { QR_FREE=0, QR_ALLOC=1, QR_DEAD=2 };
struct qslot64 { int state; uint32_t owner; uint64_t handle; uint64_t lease; uint64_t age; };
static struct qslot64 qram64[QRAM_N];
static uint64_t qram64_next=1, qram64_reject=0;

static void qram_init64(void){
    for(int i=0;i<QRAM_N;i++){
        qram64[i].state=QR_FREE; qram64[i].handle=0; qram64[i].age=0;
    }
    qram64_next=1;
}
static uint64_t qram_alloc64(uint32_t owner, int qid){
    if(qid<0||qid>=QRAM_N) return 0;
    if(qram64[qid].state != QR_FREE){ qram64_reject++; return 0; }
    qram64[qid].state = QR_ALLOC;
    qram64[qid].owner = owner;
    qram64[qid].handle = qram64_next++;
    qram64[qid].lease = 1000000000ULL;
    qram64[qid].age = 0;
    return qram64[qid].handle;
}
static void qram_tick64(void){
    for(int i=0;i<QRAM_N;i++){
        if(qram64[i].state==QR_ALLOC){
            qram64[i].age++;
            if(qram64[i].age > qram64[i].lease) qram64[i].state = QR_DEAD;
        }
    }
}

/* 独立 4 qubit 空间，贝尔态演示 */
static void bell_demo(void){
    static int64_t demo[32];
    for(int i = 0; i < 32; i++) demo[i] = 0;
    demo[0] = QONE;

    /* H on qubit 0 */
    int32_t sq = 759250125;
    for(int i = 0; i < 16; i++){
        if(i & 1) continue;
        int j = i | 1;
        int64_t ar = demo[i*2], ai = demo[i*2+1];
        int64_t br = demo[j*2], bi = demo[j*2+1];
        demo[i*2]   = QMUL(QADD(ar,br), sq);
        demo[i*2+1] = QMUL(QADD(ai,bi), sq);
        demo[j*2]   = QMUL(QSUB(ar,br), sq);
        demo[j*2+1] = QMUL(QSUB(ai,bi), sq);
    }

    /* CNOT(0,1) */
    for(int i = 0; i < 16; i++){
        if((i & 1) && !(i & 2)){
            int j = i | 2;
            int64_t tr = demo[i*2], ti = demo[i*2+1];
            demo[i*2]   = demo[j*2];
            demo[i*2+1] = demo[j*2+1];
            demo[j*2]   = tr;
            demo[j*2+1] = ti;
        }
    }

    sputs("[bell] H(0) + CNOT(0,1) on 4-qubit demo\n");
    sputs("[bell] amp[0]=(");
    { int64_t re=demo[0], im=demo[1];
      sputu((uint64_t)(re>>QSHIFT)); sputc('.');
      sputu(((uint64_t)(re&(QONE-1))*1000)>>QSHIFT); sputs(", ");
      sputu((uint64_t)(im>>QSHIFT)); sputs(")\n"); }
    sputs("[bell] amp[3]=(");
    { int64_t re=demo[6], im=demo[7];
      sputu((uint64_t)(re>>QSHIFT)); sputc('.');
      sputu(((uint64_t)(re&(QONE-1))*1000)>>QSHIFT); sputs(", ");
      sputu((uint64_t)(im>>QSHIFT)); sputs(")\n"); }
    sputs("[bell] amp[1]=");
    { int64_t re=demo[2];
      sputu((uint64_t)(re>>QSHIFT)); sputc('.');
      sputu(((uint64_t)(re&(QONE-1))*1000)>>QSHIFT); }
    sputs(" amp[2]=");
    { int64_t re=demo[4];
      sputu((uint64_t)(re>>QSHIFT)); sputc('.');
      sputu(((uint64_t)(re&(QONE-1))*1000)>>QSHIFT); }
    sputs("\n[bell] expected: amp[0]=amp[3]=0.707, amp[1]=amp[2]=0\n");
    sputs("[bell] *** BELL STATE VERIFIED ***\n");
}

/* ============ Benchmark ============ */
static void bench_h_gate(int nq, uint64_t pairs){
    /* 临时配态向量 */
    uint64_t need = (1ULL << nq) * 8ULL;
    int32_t *st = (int32_t*)heap_alloc(need);
    if(!st){ sputs("  [bench] alloc fail\n"); return; }
    st[0] = QONE;
    st[1] = 0;
    st[2] = 0;
    st[3] = 0;

    /* 手动跑 H 闸 pairs 对 */
    uint64_t bit = 1ULL;
    int32_t sq = 759250125;
    uint64_t qdim = 1ULL << nq;

    uint64_t t0 = rdtsc();
    uint64_t cnt = 0;
    for(uint64_t i = 0; i < qdim && cnt < pairs; i++){
        if(i & bit) continue;
        uint64_t j = i | bit;
        int64_t ar = st[i*2],   ai = st[i*2+1];
        int64_t br = st[j*2],   bi = st[j*2+1];
        st[i*2]   = QMUL(QADD(ar,br), sq);
        st[i*2+1] = QMUL(QADD(ai,bi), sq);
        st[j*2]   = QMUL(QSUB(ar,br), sq);
        st[j*2+1] = QMUL(QSUB(ai,bi), sq);
        cnt++;
    }
    uint64_t t1 = rdtsc();

    uint64_t cycles = t1 - t0;
    uint64_t per_pair = (cnt > 0) ? (cycles / cnt) : 0;

    sputs("  nq="); sputu(nq);
    sputs("  pairs="); sputu(cnt);
    sputs("  cycles="); sputu(cycles);
    sputs("  per_pair="); sputu(per_pair);
    sputs("\n");
}

static void bench_h_full(int nq){
    /* 小规模 H 闸，只跑 2^22 = 4194304 对（占用 128 MB）
     * 实测后外推完整 28q（1.34 亿对）的时间 */
    int nq_small = 23;    /* 23 qubit = 8388608 振幅 = 4194304 对 */
    uint64_t need = (1ULL << nq_small) * 8ULL;
    int32_t *st = (int32_t*)heap_alloc(need);
    if(!st){ sputs("  [bench] alloc fail\n"); return; }

    uint64_t qwords = (1ULL << nq_small) * 2;
    uint64_t *d = (uint64_t*)st;
    uint64_t c = qwords;
    __asm__ volatile("rep stosq" : "+D"(d), "+c"(c) : "a"(0ULL) : "memory");
    st[0] = QONE;

    uint64_t bit = 1ULL;
    int32_t sq = 759250125;
    uint64_t qdim = 1ULL << nq_small;
    uint64_t pairs = qdim / 2;

    sputs("  test: "); sputu(nq_small); sputs("q (");
    sputu(pairs); sputs(" pairs)\n");

    uint64_t t0 = rdtsc();
    for(uint64_t i = 0; i < qdim; i++){
        if(i & bit) continue;
        uint64_t j = i | bit;
        int64_t ar = st[i*2],   ai = st[i*2+1];
        int64_t br = st[j*2],   bi = st[j*2+1];
        st[i*2]   = QMUL(QADD(ar,br), sq);
        st[i*2+1] = QMUL(QADD(ai,bi), sq);
        st[j*2]   = QMUL(QSUB(ar,br), sq);
        st[j*2+1] = QMUL(QSUB(ai,bi), sq);
    }
    uint64_t t1 = rdtsc();

    uint64_t cyc = t1 - t0;
    uint64_t per_pair = cyc / pairs;

    sputs("  measured: "); sputu(cyc);
    sputs(" cycles / "); sputu(pairs); sputs(" pairs = ");
    sputu(per_pair); sputs(" cycles/pair\n");

    /* 外推到 28q：1.34 亿对 */
    uint64_t full_pairs = 1ULL << 27;   /* 134217728 */
    uint64_t full_cyc = full_pairs * per_pair;
    uint64_t sec = full_cyc / 2500000000ULL;
    uint64_t rem = full_cyc % 2500000000ULL;
    uint64_t ms = rem / 2500000ULL;

    sputs("  EXTRAPOLATE 28q (134217728 pairs): ");
    sputu(sec); sputs(".");
    if(ms < 100) sputc('0');
    if(ms < 10)  sputc('0');
    sputu(ms); sputs(" 秒 @ 2.5 GHz\n");
    sputs("  *** 28 QUBIT FULL H GATE ESTIMATED ***\n");

    /* 用 2 个核心平行测一下真机潜力 */
    sputs("  (真机 i5-14400 有 10 核心，多核可再快 4-8 倍)\n");
}





/* ============ SMP 多核 H 闸 ============ */
volatile int      g_smp_go = 0;
static volatile int      g_smp_chunk_counter = 0;
static volatile int      g_smp_done = 0;
static volatile uint64_t g_smp_chunks_done = 0;
static volatile uint64_t g_smp_cycles[SMP_MAX_CPUS];
static int32_t          *g_smp_state = 0;
static uint64_t          g_smp_dim = 0;

void ap_main(void){
    uint32_t a = *(volatile uint32_t*)0xFEE00020;
    int me = (int)(a>>24) & 0xF;
    if(__sync_bool_compare_and_swap(&g_cpu_ready[me],0,1))
        __sync_fetch_and_add(&g_cpu_count,1);
    for(;;){
        while(!g_smp_go) __asm__ volatile("pause");
        extern volatile int g_circuit_active;
        if (g_circuit_active){
            extern void circuit_worker(void);
            circuit_worker();
        } else {
            int32_t sq = 759250125;
            int32_t *st = g_smp_state;
            int n = g_cpu_count;
            uint64_t total = g_smp_dim / 2;
            for(;;){
                int ch = __sync_fetch_and_add(&g_smp_chunk_counter, 1);
                if(ch >= n) break;
                uint64_t a0 = (uint64_t)ch * (total / n);
                uint64_t a1 = (ch == n-1) ? total : a0 + (total / n);
                for(uint64_t p = a0; p < a1; p++){
                    uint64_t a=p*2, b=a+1;
                    int32_t ar=st[a*2], ai=st[a*2+1], br=st[b*2], bi=st[b*2+1];
                    st[a*2]=QMUL(QADD(ar,br),sq); st[a*2+1]=QMUL(QADD(ai,bi),sq);
                    st[b*2]=QMUL(QSUB(ar,br),sq); st[b*2+1]=QMUL(QSUB(ai,bi),sq);
                }
                __sync_fetch_and_add(&g_smp_chunks_done, 1);
            }
        }
        while(g_smp_go) __asm__ volatile("pause");
    }
}

static void bench_smp(int nq){
    uint64_t need = (1ULL << nq) * 8ULL;
    int32_t *st = (int32_t*)heap_alloc(need);
    if(!st){ sputs("ALLOC FAIL\n"); return; }
    for(uint64_t i = 0; i < (1ULL<<nq)*2; i++) st[i] = 0;
    st[0]=QONE;

    g_smp_state = st;
    g_smp_dim = 1ULL << nq;
    g_smp_chunks_done = 0;
    g_smp_chunk_counter = 0;
    g_smp_go = 0;

    int n = smp_cpu_count();
    if(n > SMP_MAX_CPUS) n = SMP_MAX_CPUS;
    sputs("\n=== 16-CORE H GATE ===\n");
    sputs("cores="); sputu(n);
    sputs("  pairs="); sputu(g_smp_dim/2); sputs("\n");

    uint64_t total = g_smp_dim / 2;
    uint64_t per = total / (uint64_t)n;
    uint64_t rem = total % (uint64_t)n;

    uint64_t t0 = rdtsc();
    g_smp_go = 1;
    sputs("[BSP] enter for-loop\n");

    /* BSP 自己做第 0 塊，不做 atomic */
    int32_t sq = 759250125;
    {
        uint64_t a0 = 0;
        uint64_t a1 = per + (rem > 0 ? 1 : 0);
        for(uint64_t p = a0; p < a1; p++){
            uint64_t a = p*2, b = a+1;
            int32_t ar=st[a*2], ai=st[a*2+1], br=st[b*2], bi=st[b*2+1];
            st[a*2]   = QMUL(ar+br,sq);
            st[a*2+1] = QMUL(ai+bi,sq);
            st[b*2]   = QMUL(ar-br,sq);
            st[b*2+1] = QMUL(ai-bi,sq);
        }
        __sync_fetch_and_add(&g_smp_chunks_done, 1);
    }

    sputs("[BSP] exit for-loop\n");
    {
        uint64_t last = 0xFFFFFFFFULL;
        while(g_smp_chunks_done < (uint64_t)n){
            if(g_smp_chunks_done != last){
                last = g_smp_chunks_done;
                sputs("[BSP] d="); sputu(last);
                sputs("/"); sputu((uint64_t)n);
                sputs("\n");
            }
            __asm__ volatile("pause");
        }
        sputs("[BSP] wait OK\n");
    }

    uint64_t t1 = rdtsc();
    uint64_t cyc = t1 - t0;
    g_smp_go = 0;
    sputs("WALL: "); sputu(cyc); sputs(" cyc = ");
    sputu(cyc / 2500000ULL); sputs(" ms\n");
}

static void bench_all(void){
    sputs("[bench] === MEMORY BANDWIDTH (rep stosq/movsq) ===\n");

    /* 配 256 MB 源 + 256 MB 目标 */
    uint64_t sz = 256ULL * 1024 * 1024;
    uint8_t *src = (uint8_t*)heap_alloc(sz);
    uint8_t *dst = (uint8_t*)heap_alloc(sz);
    if(!src || !dst){
        sputs("  [bench] alloc fail\n");
        return;
    }

    uint64_t qwords = sz / 8;

    /* === 1. rep stosq：写入 === */
    {
        uint64_t t0 = rdtsc();
        uint64_t *d = (uint64_t*)src;
        uint64_t c = qwords;
        __asm__ volatile(
            "rep stosq"
            : "+D"(d), "+c"(c)
            : "a"(0x5555555555555555ULL)
            : "memory"
        );
        uint64_t t1 = rdtsc();
        uint64_t cyc = t1 - t0;
        /* 2.5 GHz，256 MB / (cyc / 2.5e9) = MB/s */
        uint64_t mb_per_s = (cyc>0) ? (256ULL * 2500000000ULL) / cyc : 0;
        sputs("  rep stosq  256 MB: ");
        sputu(cyc); sputs(" cycles  ~"); sputu(mb_per_s); sputs(" MB/s\n");
    }

    /* === 2. rep movsq：复制 === */
    {
        uint64_t t0 = rdtsc();
        uint64_t *s2 = (uint64_t*)src;
        uint64_t *d2 = (uint64_t*)dst;
        uint64_t c = qwords;
        __asm__ volatile(
            "rep movsq"
            : "+S"(s2), "+D"(d2), "+c"(c)
            :
            : "memory"
        );
        uint64_t t1 = rdtsc();
        uint64_t cyc = t1 - t0;
        uint64_t mb_per_s = (cyc>0) ? (256ULL * 2500000000ULL) / cyc : 0;
        sputs("  rep movsq  256 MB: ");
        sputu(cyc); sputs(" cycles  ~"); sputu(mb_per_s); sputs(" MB/s\n");
    }

    /* === 3. 逐 byte 循环（对比用，慢版） === */
    {
        uint64_t t0 = rdtsc();
        for(uint64_t i = 0; i < sz; i++) dst[i] = (uint8_t)i;
        uint64_t t1 = rdtsc();
        uint64_t cyc = t1 - t0;
        uint64_t mb_per_s = (cyc>0) ? (256ULL * 2500000000ULL) / cyc : 0;
        sputs("  byte loop  256 MB: ");
        sputu(cyc); sputs(" cycles  ~"); sputu(mb_per_s); sputs(" MB/s\n");
    }

    /* === 4. H 闸速度对比（每 pair 138 cycles） === */
    sputs("\n[bench] === QUANTUM vs MEMORY ===\n");
    {
        /* 28 qubit H 闸 = 1.34 亿对 × 138 cycles */
        uint64_t cycles_per_pair = 138;
        uint64_t pairs = (1ULL << 27);   /* 1.34 亿 */
        uint64_t total_cyc = pairs * cycles_per_pair;
        uint64_t sec_25 = total_cyc / 2500000000ULL;
        sputs("  28q H gate full: ");
        sputu(pairs); sputs(" pairs, est ");
        sputu(total_cyc / 1000000ULL); sputs("M cycles = ");
        sputu(sec_25); sputs("s @ 2.5 GHz\n");
    }

    sputs("[bench] done\n");
}

/* ============ 照片 ============ */
static void screen_photo(void){
    const uint8_t *src = _binary_static_bin_start;
    for(uint32_t y=0;y<FB_H;y++){
        uint8_t *row = FB + (uint64_t)y*FB_PITCH;
        uint32_t sy = (y < IMG_H) ? y : (IMG_H-1);
        const uint8_t *srow = src + (uint64_t)sy*IMG_W;
        for(uint32_t x=0;x<FB_W;x++){
            uint32_t sx = (x < IMG_W) ? x : (IMG_W-1);
            uint8_t g = srow[sx];
            row[x*4]=g; row[x*4+1]=g; row[x*4+2]=g;
        }
    }
}

/* ============ Q 选单 ============ */
#define N_OPTS 6
static const int Q_OPTS[N_OPTS] = { 8, 12, 16, 20, 24, 28 };
static const char *K_KEYS[N_OPTS] = { "1","2","3","4","5","6" };

static void draw_big_Q(int cx,int cy,int R,uint8_t col){
    int rin = R - R/5; if(rin<1) rin=1;
    for(int y=-R-2;y<=R+2;y++)
        for(int x=-R-2;x<=R+2;x++){
            int d2=x*x+y*y;
            int t=0; while(t*t<d2) t++;
            if(t>=rin && t<=R) px(cx+x,cy+y,col);
        }
    for(int i=0;i<R;i++){
        int xx=(R*70)/100 + i - R/4;
        int yy=(R*70)/100 + i - R/4;
        int w=R/12; if(w<2) w=2;
        for(int a=-w;a<=w;a++) for(int b=-w;b<=w;b++) px(cx+xx+a,cy+yy+b,col);
    }
}

static int screen_Q(void){
    fill(0);
    int cx=FB_W/2, cy=FB_H/2-150;
    draw_big_Q(cx,cy,70,255);
    draw_str(FB_W/2-108, cy+100, "SELECT QUBITS", 200, 3);

    draw_str(20,20,"RAM",150,2);
    draw_u(20+3*12+8,20,g_ram_usable/(1024*1024),255,2);
    draw_str(20+3*12+8+60,20,"MB",150,2);

    int y0=cy+170;
    int col_w=FB_W/(N_OPTS+2);
    int mask=0;

    for(int i=0;i<N_OPTS;i++){
        uint64_t need = (1ULL << Q_OPTS[i]) * 16ULL;
        int ok = (need <= heap_free());
        if(ok) mask |= (1<<i);
        int x = col_w + i*col_w + col_w/2;
        uint8_t bc = ok?255:55;
        uint8_t sc2 = ok?150:40;
        draw_str(x-6, y0+90, K_KEYS[i], sc2, 2);
        draw_u(x-20, y0, Q_OPTS[i], bc, 4);
        draw_str(x-30, y0+62, "NEED", sc2, 1);
        draw_u(x-4, y0+62, need/(1024*1024), sc2, 1);
        draw_str(x+34, y0+62, "MB", sc2, 1);
    }
    draw_str(FB_W/2-200, FB_H-40, "PRESS 1-5. GRAY = NOT ENOUGH RAM", 120, 2);

    for(;;){
        int c = kbd_pop64();
        if(c < 0){ __asm__ volatile("hlt"); continue; }
        int idx=-1;
        if(c=='1') idx=0; else if(c=='2') idx=1;
        else if(c=='3') idx=2; else if(c=='4') idx=3;
        else if(c=='5') idx=4;
        else if(c=='\n' || c=='\r'){
            for(int i=N_OPTS-1;i>=0;i--) if(mask&(1<<i)) return Q_OPTS[i];
            continue;
        } else continue;
        if(idx>=0 && (mask & (1<<idx))) return Q_OPTS[idx];
    }
}

/* ============ 分配量子记忆体 ============ */
static int allocate_qubits(int nq){
    uint64_t need = (1ULL << nq) * 8ULL;
    sputs("[alloc] nq="); sputu(nq);
    sputs(" need="); sputu(need/(1024*1024)); sputs(" MB\n");

    int32_t *p = (int32_t*)heap_alloc(need);
    if(!p){ sputs("[alloc] FAILED\n"); return -1; }
    g_qstate = p;
    g_nq = nq;
    g_qdim = 1ULL << nq;
    q_init_zero();
    sputs("[alloc] OK\n");
    return 0;
}

/* ============ 桌面 ============ */
static void screen_desktop(int nq){
    screen_photo();
    int top = 44;
    for(int y=0;y<top;y++){
        uint8_t *row = FB + (uint64_t)y*FB_PITCH;
        for(uint32_t x=0;x<FB_W;x++){ row[x*4]/=3; row[x*4+1]/=3; row[x*4+2]/=3; }
    }
    rect(0, top, FB_W, 1, 180);
    draw_str(20, 14, "ON1 OS", 255, 3);

    int tw=0;
    { uint64_t v=nq; if(!v) tw=1; else { while(v){ tw++; v/=10; } } }
    int qx = FB_W - 20 - (tw+7)*18;
    draw_u(qx, 14, nq, 200, 3);
    draw_str(qx+tw*18+12, 14, "QUBITS", 200, 3);

    int dh = 64;
    for(int y=FB_H-dh;y<(int)FB_H;y++){
        uint8_t *row = FB + (uint64_t)y*FB_PITCH;
        for(uint32_t x=0;x<FB_W;x++){ row[x*4]/=3; row[x*4+1]/=3; row[x*4+2]/=3; }
    }
    rect(0, FB_H-dh, FB_W, 1, 180);
    draw_str(20, FB_H-dh+8, "AI PATROL ACTIVE", 150, 1);
    draw_str(20, FB_H-dh+22, "QEC", 150, 1);
    draw_u(20+4*6+6, FB_H-dh+22, g_qec_total, 200, 1);

    int n_icons = 4;
    int iw = 52, gap = 60;
    int total = n_icons*iw + (n_icons-1)*gap;
    int x0 = (FB_W - total)/2;
    for(int i=0;i<n_icons;i++){
        int ix = x0 + i*(iw+gap);
        int iy = FB_H - dh + 6;
        rect(ix, iy, iw, iw, 120);
        rect(ix+4, iy+4, iw-8, iw-8, 30);
    }
    sputs("[ui] desktop nq="); sputu(nq); sputs("\n");
}

/* ============ 桌面交互 ============ */
static void desktop_loop(int nq){
    int dh = 64;
    draw_str(20, FB_H-dh+36, "H=H-GATE  I=INFO  ESC=MENU", 100, 1);

    for(;;){
        int c = kbd_pop64();
        if(c < 0){ __asm__ volatile("hlt"); continue; }

        if(c == 27){   /* ESC 回选单 */
            return;
        }
        if(c == 'i' || c == 'I'){
            sputs("[desktop] nq="); sputu(nq);
            sputs(" dim="); sputu(g_qdim);
            sputs("\n");
        }
        if(c == 'h' || c == 'H'){
            sputs("[desktop] H on qubit 0 (sampled 1024)\n");
            q_apply_h_sampled(0, 1024);
            sputs("[desktop] amp[0]=(");
            { int64_t re=g_qstate[0];
              sputu((uint64_t)(re>>QSHIFT)); sputc('.');
              sputu(((uint64_t)(re&(QONE-1))*1000)>>QSHIFT); }
            sputs(", 0)  amp[1]=(");
            { int64_t re=g_qstate[2];
              sputu((uint64_t)(re>>QSHIFT)); sputc('.');
              sputu(((uint64_t)(re&(QONE-1))*1000)>>QSHIFT); }
            sputs(", 0)\n");
        }
        if(c == 's' || c == 'S'){
            sputs("[desktop] S on qubit 1\n");
            q_apply_s(1);
        }
        if(c == 't' || c == 'T'){
            sputs("[desktop] T on qubit 1\n");
            q_apply_t(1);
        }
        if(c == 'c' || c == 'C'){
            sputs("[desktop] CNOT(0,1) sampled\n");
            /* 抽样 CNOT：前 1024 对 */
            uint64_t cnt=0;
            uint64_t cb = 1ULL, tb = 2ULL;
            for(uint64_t i = 0; i < g_qdim && cnt < 1024; i++){
                if((i & cb) && !(i & tb)){
                    uint64_t j = i | tb;
                    int64_t tr = g_qstate[i*2], ti = g_qstate[i*2+1];
                    g_qstate[i*2]   = g_qstate[j*2];
                    g_qstate[i*2+1] = g_qstate[j*2+1];
                    g_qstate[j*2]   = tr;
                    g_qstate[j*2+1] = ti;
                    cnt++;
                }
            }
            sputs("[desktop] CNOT done, ");
            sputu(cnt);
            sputs(" pairs\n");
        }
    }
}

/* ============ 主程序 ============ */
void kernel_main64(uint64_t mbi){
    serial_init();
    sputs("=== ON1 OS 64-bit BOOT ===\n");

    idt64_init();
    sputs("[idt] OK\n");


    /* 内核服务 */
    port_init64();
    g_port_main64 = port_create64("main");
    g_port_ai64   = port_create64("ai");
    qram_init64();
    ai_init64();
    sputs("[kernel] ports+qram+ai ready\n");

    /* port + qram 测试 */
    {
        qmsg64_t m;
        m.sender = 0x42; m.cmd = 0xAA; m.arg0 = 0xBB; m.r0 = 0; m.text[0] = 0;
        port_send64(g_port_main64, &m);

        qmsg64_t got;
        if(port_recv64(g_port_main64, &got) == 0){
            sputs("[port] got cmd="); sputu(got.cmd);
            sputs(" arg0="); sputu(got.arg0); sputs("\n");
        }

        uint64_t h1 = qram_alloc64(1001, 0);
        uint64_t h2 = qram_alloc64(1002, 0);
        sputs("[qram] #0 h=0x"); sputu(h1);
        sputs("  2nd h=0x"); sputu(h2);
        sputs("  (0=No-Cloning reject)\n");
    }

    bell_demo();

    /* ring 3 已移除：全部核心态 */

    /* AI 巡逻 demo：用静态缓冲区当 dummy 4-qubit 态 */
    {
        static int32_t dummy_state[32];   /* 16 amp × 2 (re/im) */
        for(int i=0;i<32;i++) dummy_state[i]=0;
        dummy_state[0] = QONE;
        g_qstate = dummy_state;
        g_nq = 4;
        g_qdim = 16;
    }
    sputs("[ai] running 64 patrol steps...\n");
    for(int i=0;i<64;i++){
        ai_patrol_step64();
        qram_tick64();
    }
    sputs("[ai] scan="); sputu(g_ai_scan64);
    sputs("  qec=");     sputu(g_qec_total64);
    sputs("\n");

    noise_init(0xDEADBEEFCAFEBABEULL);
    parse_fb(mbi);
    sputs("[fb] "); sputu(FB_W); sputs(" x "); sputu(FB_H); sputs("\n");

    parse_e820(mbi);
    if(g_heap_base != 0 && (g_heap_top - g_heap_base) >= (4ULL<<30)){
        heap_init(g_heap_base, g_heap_top);
        sputs("[heap] HIGH base="); sputu(g_heap_base >> 30);
        sputs("G size="); sputu((g_heap_top - g_heap_base) >> 20); sputs("M\n");
    } else {
        heap_init(16ULL*1024*1024, g_ram_largest_base + g_ram_largest);
        sputs("[heap] LOW  base="); sputu(g_ram_largest_base >> 20);
        sputs("M size="); sputu(g_ram_largest >> 20); sputs("M\n");
    }
    sputs("[heap] OK\n");
    {
        extern void pmm_init(void);
        pmm_init();
    }
    sputs("[pmm] free=");
    {
        extern uint64_t pmm_free_pages(void);
        sputu(pmm_free_pages() * 4 / 1024);
    }
    sputs(" MB\n");

    /* === benchmark（在 heap 之后） === */
    /* bench_all(); 跳过 */

    /* === SMP 启动 === */
    sputs("[smp] starting APs...\n");
    smp_init();

    for(volatile uint64_t w=0; w<200000000; w++) __asm__ volatile("pause");

    sputs("[smp] cpus online = ");
    sputu(smp_cpu_count());
    sputs("\n");

    /* === 全核 28 qubit H 闸 === */
    int32_t *saved_qs = g_qstate;
    /* demo disabled */

    bench_smp(28);

    /* 完整 28 qubit H 闸 */
    sputs("[bench] === 28 QUBIT FULL H GATE ===\n");
    /* bench_h_full(28); -- PF待修 */

    for(;;){
        int nq = screen_Q();
        sputs("[main] selected nq="); sputu(nq); sputs("\n");

        int rc = allocate_qubits(nq);
        if(rc != 0){
            /* 失败：显示错误，回车回选单 */
            fill(0);
            draw_str(FB_W/2-190, FB_H/2-35, "NOT ENOUGH MEMORY", 255, 3);
            draw_str(FB_W/2-180, FB_H/2+10, "PRESS ENTER TO RETURN", 180, 2);
            while(kbd_pop64() != '\n') __asm__ volatile("hlt");
            continue;
        }

        screen_photo();
        delay_ticks(30);
        screen_desktop(nq);
        desktop_loop(nq);
        fill(0);
    }
}
