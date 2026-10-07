#include <stdio.h>
#include <math.h>
#include <stdlib.h>
typedef struct { double re, im; } cplx;
#define NQ 4
#define DIM 16
static const int E[5][2]={{0,1},{1,2},{2,3},{3,0},{0,2}};
#define PMAX 8

static int cost(int b){
    int c=0;
    for(int e=0;e<5;e++){
        int x=(b>>E[e][0])&1, y=(b>>E[e][1])&1;
        if(x!=y) c++;
    }
    return c;
}
static void apply_cost(cplx *s, double g){
    for(int b=0;b<DIM;b++){
        double ph=-g*cost(b), c=cos(ph), si=sin(ph);
        double re=s[b].re, im=s[b].im;
        s[b].re=re*c-im*si;
        s[b].im=re*si+im*c;
    }
}
static void apply_mixer_q(cplx *s, int q, double b){
    double c=cos(b), si=sin(b);
    int m=1<<q;
    for(int i=0;i<DIM;i++){
        if(i&m) continue;
        int j=i|m;
        double ar=s[i].re, ai=s[i].im;
        double br=s[j].re, bi=s[j].im;
        s[i].re = c*ar + si*bi;
        s[i].im = c*ai - si*br;
        s[j].re = c*br + si*ai;
        s[j].im = c*bi - si*ar;
    }
}
static void apply_mixer(cplx *s, double b){
    for(int q=0;q<NQ;q++) apply_mixer_q(s, q, b);
}
static void build(cplx *s, double *g, double *b, int p){
    for(int i=0;i<DIM;i++){ s[i].re=1.0/sqrt((double)DIM); s[i].im=0.0; }
    for(int L=0;L<p;L++){
        apply_cost(s, g[L]);
        apply_mixer(s, b[L]);
    }
    double n=0;
    for(int i=0;i<DIM;i++) n += s[i].re*s[i].re + s[i].im*s[i].im;
    n=sqrt(n);
    for(int i=0;i<DIM;i++){ s[i].re/=n; s[i].im/=n; }
}
static double Ecost(cplx *s){
    double e=0;
    for(int i=0;i<DIM;i++) e += (s[i].re*s[i].re+s[i].im*s[i].im)*cost(i);
    return e;
}

static double optimize_layer(double *g, double *b, int p, cplx *s, int restarts){
    double best_E = -1e9;
    double bg[PMAX], bb[PMAX];
    for(int r=0; r<restarts; r++){
        double cg[PMAX], cb[PMAX];
        for(int k=0;k<p;k++){
            if(r == 0){
                /* 第 1 次：從上一層結果複製，最後一層隨機 */
                cg[k] = (k < p-1) ? g[k] : 0.1 + 0.9*((double)rand()/RAND_MAX);
                cb[k] = (k < p-1) ? b[k] : 0.1 + 0.9*((double)rand()/RAND_MAX);
            } else {
                for(int kk=0;kk<p;kk++){
                    cg[kk] = 0.1 + 0.9*((double)rand()/RAND_MAX);
                    cb[kk] = 0.1 + 0.9*((double)rand()/RAND_MAX);
                }
                break;
            }
        }
        /* momentum SGD */
        double vg[PMAX]={0}, vb[PMAX]={0};
        double beta_m = 0.9;
        for(int it=0; it<300; it++){
            double eps=1e-5, gr[PMAX], br[PMAX];
            for(int k=0;k<p;k++){
                double o=cg[k];
                cg[k]=o+eps; build(s,cg,cb,p); double ep=Ecost(s);
                cg[k]=o-eps; build(s,cg,cb,p); double em=Ecost(s);
                cg[k]=o; gr[k]=(ep-em)/(2*eps);
            }
            for(int k=0;k<p;k++){
                double o=cb[k];
                cb[k]=o+eps; build(s,cg,cb,p); double ep=Ecost(s);
                cb[k]=o-eps; build(s,cg,cb,p); double em=Ecost(s);
                cb[k]=o; br[k]=(ep-em)/(2*eps);
            }
            double lr = 0.1 / (1.0 + 0.005*it);
            for(int k=0;k<p;k++){
                vg[k] = beta_m*vg[k] + (1-beta_m)*gr[k];
                vb[k] = beta_m*vb[k] + (1-beta_m)*br[k];
                cg[k] += lr*vg[k];
                cb[k] += lr*vb[k];
            }
        }
        build(s, cg, cb, p);
        double E = Ecost(s);
        if(E > best_E){
            best_E = E;
            for(int k=0;k<p;k++){ bg[k]=cg[k]; bb[k]=cb[k]; }
        }
    }
    for(int k=0;k<p;k++){ g[k]=bg[k]; b[k]=bb[k]; }
    return best_E;
}

int main(void){
    srand(42);
    printf("QAOA MaxCut with warm start + momentum\n");
    printf("Optimal = 4\n\n");
    cplx *s = malloc(sizeof(cplx)*DIM);
    double g[PMAX]={0}, b[PMAX]={0};
    for(int p=1; p<=PMAX; p++){
        double E = optimize_layer(g, b, p, s, 50);
        printf("p=%2d  <H_C> = %.4f  (%.1f%%)\n", p, E, 100*E/4);
    }
    free(s);
    return 0;
}
