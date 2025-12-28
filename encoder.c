// encoder.c  (Methods 0/1/2/3)
// MMSP Final Project - Compatible CLI for method 0/1/2/3
// - Method 0: BMP -> R/G/B txt + dim.txt
// - Method 1: BMP -> QT txt + dim.txt + qF raw (int16) + eF raw (float32) + print SQNR_Freq (3x64)
// - Method 2: BMP -> RLE (ascii or binary)  [pipeline: RGB->YCbCr->DCT->Quant->DPCM(DC)->ZigZag->RLE]
// - Method 3: Method2-binary payload -> Huffman (ascii or binary), with codebook.txt

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ========================== BMP ========================== */
#pragma pack(push,1)
typedef struct {
    uint16_t bfType;      // 'BM'
    uint32_t bfSize;
    uint16_t bfReserved1;
    uint16_t bfReserved2;
    uint32_t bfOffBits;
} BMPFileHeader;

typedef struct {
    uint32_t biSize;      // 40
    int32_t  biWidth;
    int32_t  biHeight;    // + : bottom-up, - : top-down
    uint16_t biPlanes;    // 1
    uint16_t biBitCount;  // 24
    uint32_t biCompression; // 0
    uint32_t biSizeImage;
    int32_t  biXPelsPerMeter;
    int32_t  biYPelsPerMeter;
    uint32_t biClrUsed;
    uint32_t biClrImportant;
} BMPInfoHeader;
#pragma pack(pop)

static void die(const char* msg){
    fprintf(stderr, "ERROR: %s\n", msg);
    exit(1);
}
static int row_size_24(int w){ return ((w*3 + 3)/4)*4; }

static void load_bmp_topdown_rgb(
    const char* path, int* W, int* H,
    uint8_t** R, uint8_t** G, uint8_t** B,
    uint8_t header54[54], int* has_header54
){
    FILE* f = fopen(path,"rb");
    if(!f) die("Failed to open BMP");

    BMPFileHeader fh;
    BMPInfoHeader ih;
    if(fread(&fh,sizeof(fh),1,f)!=1) die("BMP read header failed");
    if(fread(&ih,sizeof(ih),1,f)!=1) die("BMP read info failed");

    if(fh.bfType != 0x4D42) die("Not a BMP");
    if(ih.biBitCount != 24 || ih.biCompression != 0) die("Only 24-bit uncompressed BMP supported");

    // capture original 54B header for exact reproduction if needed
    // rebuild the 54 bytes by seeking 0 and reading 54
    long cur = ftell(f);
    fseek(f, 0, SEEK_SET);
    size_t n = fread(header54, 1, 54, f);
    *has_header54 = (n==54);
    fseek(f, cur, SEEK_SET);

    int w = ih.biWidth;
    int h_abs = (ih.biHeight>0) ? ih.biHeight : -ih.biHeight;
    int rs = row_size_24(w);

    uint8_t* r = (uint8_t*)malloc((size_t)w*h_abs);
    uint8_t* g = (uint8_t*)malloc((size_t)w*h_abs);
    uint8_t* b = (uint8_t*)malloc((size_t)w*h_abs);
    uint8_t* row = (uint8_t*)malloc((size_t)rs);
    if(!r||!g||!b||!row) die("OOM");

    fseek(f, fh.bfOffBits, SEEK_SET);

    // read file rows, convert to TOP-DOWN indexing
    for(int file_row=0; file_row<h_abs; file_row++){
        if(fread(row,1,(size_t)rs,f)!=(size_t)rs) die("BMP pixel read failed");
        int y = (ih.biHeight>0) ? (h_abs-1-file_row) : file_row; // top-down y
        for(int x=0;x<w;x++){
            uint8_t Bv = row[x*3 + 0];
            uint8_t Gv = row[x*3 + 1];
            uint8_t Rv = row[x*3 + 2];
            r[y*w + x] = Rv;
            g[y*w + x] = Gv;
            b[y*w + x] = Bv;
        }
    }

    free(row);
    fclose(f);

    *W = w; *H = h_abs;
    *R = r; *G = g; *B = b;
}

/* ========================== DCT/IDCT (separable) ========================== */
static double COS8[8][8]; // cos((2x+1)u*pi/16)
static double ALPHA8[8];  // alpha(u)

static void init_dct_table(void){
    for(int u=0;u<8;u++){
        ALPHA8[u] = (u==0)? (1.0/sqrt(2.0)) : 1.0;
        for(int x=0;x<8;x++){
            COS8[u][x] = cos(((2.0*x + 1.0)*u*M_PI)/16.0);
        }
    }
}

static void dct8x8(const double in[8][8], double out[8][8]){
    // temp[u][y] = sum_x in[x][y] * cos(u,x)
    double temp[8][8];
    for(int u=0;u<8;u++){
        for(int y=0;y<8;y++){
            double s=0.0;
            for(int x=0;x<8;x++) s += in[x][y]*COS8[u][x];
            temp[u][y]=s;
        }
    }
    // out[u][v] = 0.25 * alpha(u)alpha(v) * sum_y temp[u][y]*cos(v,y)
    for(int u=0;u<8;u++){
        for(int v=0;v<8;v++){
            double s=0.0;
            for(int y=0;y<8;y++) s += temp[u][y]*COS8[v][y];
            out[u][v] = 0.25 * ALPHA8[u]*ALPHA8[v] * s;
        }
    }
}

static void idct8x8(const double in[8][8], double out[8][8]){
    // temp[x][v] = sum_u alpha(u)*in[u][v]*cos(u,x)
    double temp[8][8];
    for(int x=0;x<8;x++){
        for(int v=0;v<8;v++){
            double s=0.0;
            for(int u=0;u<8;u++) s += ALPHA8[u]*in[u][v]*COS8[u][x];
            temp[x][v]=s;
        }
    }
    // out[x][y] = 0.25 * sum_v alpha(v)*temp[x][v]*cos(v,y)
    for(int x=0;x<8;x++){
        for(int y=0;y<8;y++){
            double s=0.0;
            for(int v=0;v<8;v++) s += ALPHA8[v]*temp[x][v]*COS8[v][y];
            out[x][y] = 0.25 * s;
        }
    }
}

/* ========================== Color ========================== */
static void rgb_to_ycbcr(uint8_t R, uint8_t G, uint8_t B, double* Y, double* Cb, double* Cr){
    // BT.601
    *Y  =  0.299   * R + 0.587   * G + 0.114   * B;
    *Cb = -0.168736* R - 0.331264* G + 0.5     * B + 128.0;
    *Cr =  0.5     * R - 0.418688* G - 0.081312* B + 128.0;
}

/* ========================== Quant tables ========================== */
static const int QT_Y[8][8] = {
    {16,11,10,16,24,40,51,61},
    {12,12,14,19,26,58,60,55},
    {14,13,16,24,40,57,69,56},
    {14,17,22,29,51,87,80,62},
    {18,22,37,56,68,109,103,77},
    {24,35,55,64,81,104,113,92},
    {49,64,78,87,103,121,120,101},
    {72,92,95,98,112,100,103,99}
};
static const int QT_C[8][8] = {
    {17,18,24,47,99,99,99,99},
    {18,21,26,66,99,99,99,99},
    {24,26,56,99,99,99,99,99},
    {47,66,99,99,99,99,99,99},
    {99,99,99,99,99,99,99,99},
    {99,99,99,99,99,99,99,99},
    {99,99,99,99,99,99,99,99},
    {99,99,99,99,99,99,99,99}
};

static void write_qt_txt(const char* path, const int qt[8][8]){
    FILE* f = fopen(path,"w");
    if(!f) die("open qt txt failed");
    for(int r=0;r<8;r++){
        for(int c=0;c<8;c++){
            fprintf(f,"%d%s", qt[r][c], (c==7)?"":" ");
        }
        fprintf(f,"\n");
    }
    fclose(f);
}

/* ========================== ZigZag ========================== */
static const int ZZU[64] = {
 0,0,1,2,1,0,0,1,2,3,4,3,2,1,0,0,
 1,2,3,4,5,6,5,4,3,2,1,0,1,2,3,4,
 5,6,7,7,6,5,4,3,2,3,4,5,6,7,7,6,
 5,4,5,6,7,7,6,7,7,7,7,7,7,7,7,7
};
static const int ZZV[64] = {
 0,1,0,0,1,2,3,2,1,0,0,1,2,3,4,5,
 4,3,2,1,0,0,1,2,3,4,5,6,7,6,5,4,
 3,2,1,0,1,2,3,4,5,6,7,7,6,5,4,3,
 4,5,6,7,7,6,5,6,7,7,6,5,4,3,2,1
};
// NOTE: 這份 ZZU/ZZV 是「能跑」的順序（與 JPEG 標準同型），
//       若你老師給的 zigzag 不同，你只要把 ZZU/ZZV 替換即可（decoder 也要同一份）。

/* ========================== Method-2 RLE binary format ========================== */
typedef struct { int16_t skip; int16_t val; } Pair;

static void usage(void){
    printf("Usage:\n");
    printf("  encoder 0 input.bmp R.txt G.txt B.txt dim.txt\n");
    printf("  encoder 1 input.bmp Qt_Y.txt Qt_Cb.txt Qt_Cr.txt dim.txt qF_Y.raw qF_Cb.raw qF_Cr.raw eF_Y.raw eF_Cb.raw eF_Cr.raw\n");
    printf("  encoder 2 input.bmp ascii  rle_code.txt\n");
    printf("  encoder 2 input.bmp binary rle_code.bin\n");
    printf("  encoder 3 input.bmp ascii  codebook.txt huffman_code.txt\n");
    printf("  encoder 3 input.bmp binary codebook.txt huffman_code.bin\n");
}

/* ========================== Huffman (Method-3) ========================== */
typedef struct HNode {
    int is_leaf;
    int sym;              // 0..255 if leaf
    uint64_t freq;
    struct HNode *l, *r;
} HNode;

static HNode* hn_new_leaf(int sym, uint64_t freq){
    HNode* n = (HNode*)calloc(1,sizeof(HNode));
    if(!n) die("OOM");
    n->is_leaf=1; n->sym=sym; n->freq=freq;
    return n;
}
static HNode* hn_new_internal(HNode* a, HNode* b){
    HNode* n = (HNode*)calloc(1,sizeof(HNode));
    if(!n) die("OOM");
    n->is_leaf=0;
    n->l=a; n->r=b;
    n->freq = (a?a->freq:0) + (b?b->freq:0);
    return n;
}
static void hn_free(HNode* n){
    if(!n) return;
    hn_free(n->l); hn_free(n->r);
    free(n);
}

// deterministic compare: (freq, is_leaf, sym/minSym)
typedef struct {
    HNode** a;
    int sz, cap;
} MinHeap;

static int min_sym(HNode* n){
    if(!n) return 999999;
    if(n->is_leaf) return n->sym;
    int ml = min_sym(n->l);
    int mr = min_sym(n->r);
    return (ml<mr)?ml:mr;
}

static int hn_less(HNode* x, HNode* y){
    if(x->freq != y->freq) return x->freq < y->freq;
    int mx = min_sym(x), my = min_sym(y);
    if(mx != my) return mx < my;
    // tie: leaf first
    if(x->is_leaf != y->is_leaf) return x->is_leaf > y->is_leaf;
    return 0;
}

static void heap_init(MinHeap* h, int cap){
    h->a = (HNode**)malloc(sizeof(HNode*)*cap);
    if(!h->a) die("OOM");
    h->sz=0; h->cap=cap;
}
static void heap_push(MinHeap* h, HNode* n){
    if(h->sz >= h->cap){
        h->cap *= 2;
        h->a = (HNode**)realloc(h->a, sizeof(HNode*)*h->cap);
        if(!h->a) die("OOM");
    }
    int i = h->sz++;
    h->a[i]=n;
    while(i>0){
        int p=(i-1)/2;
        if(!hn_less(h->a[i], h->a[p])) break;
        HNode* tmp=h->a[i]; h->a[i]=h->a[p]; h->a[p]=tmp;
        i=p;
    }
}
static HNode* heap_pop(MinHeap* h){
    if(h->sz==0) return NULL;
    HNode* ret=h->a[0];
    h->a[0]=h->a[--h->sz];
    int i=0;
    while(1){
        int l=i*2+1, r=i*2+2, m=i;
        if(l<h->sz && hn_less(h->a[l], h->a[m])) m=l;
        if(r<h->sz && hn_less(h->a[r], h->a[m])) m=r;
        if(m==i) break;
        HNode* tmp=h->a[i]; h->a[i]=h->a[m]; h->a[m]=tmp;
        i=m;
    }
    return ret;
}

static HNode* build_huffman(uint64_t freq[256], int* unique_out){
    MinHeap hp; heap_init(&hp, 256);
    int unique=0;
    for(int s=0;s<256;s++){
        if(freq[s]>0){
            heap_push(&hp, hn_new_leaf(s, freq[s]));
            unique++;
        }
    }
    if(unique==0) die("empty payload for Huffman");
    if(unique==1){
        // special: create dummy internal node
        HNode* only = heap_pop(&hp);
        HNode* dummy = hn_new_leaf((only->sym==0)?1:0, 0);
        HNode* root = hn_new_internal(dummy, only);
        *unique_out=unique;
        free(hp.a);
        return root;
    }
    while(hp.sz>1){
        HNode* a = heap_pop(&hp);
        HNode* b = heap_pop(&hp);
        // deterministic: ensure left is "smaller"
        if(hn_less(b,a)){ HNode* t=a; a=b; b=t; }
        heap_push(&hp, hn_new_internal(a,b));
    }
    HNode* root = heap_pop(&hp);
    free(hp.a);
    *unique_out=unique;
    return root;
}

static void gen_codes(HNode* n, char* buf, int depth, char* codes[256]){
    if(!n) return;
    if(n->is_leaf){
        buf[depth]='\0';
        codes[n->sym] = strdup(buf[0]?buf:"0"); // if only one symbol, code "0"
        return;
    }
    buf[depth]='0'; gen_codes(n->l, buf, depth+1, codes);
    buf[depth]='1'; gen_codes(n->r, buf, depth+1, codes);
}

/* pack bits MSB-first */
typedef struct {
    uint8_t* data;
    size_t bit_len;
    size_t cap;
} BitBuf;

static void bitbuf_init(BitBuf* b){
    b->cap = 1024;
    b->data = (uint8_t*)calloc(b->cap,1);
    if(!b->data) die("OOM");
    b->bit_len=0;
}
static void bitbuf_push_bit(BitBuf* b, int bit){
    size_t byte = b->bit_len / 8;
    size_t off  = b->bit_len % 8;
    if(byte >= b->cap){
        size_t old = b->cap;
        b->cap *= 2;
        b->data = (uint8_t*)realloc(b->data, b->cap);
        if(!b->data) die("OOM");
        memset(b->data+old, 0, b->cap-old);
    }
    if(bit) b->data[byte] |= (uint8_t)(1u << (7-off));
    b->bit_len++;
}
static void bitbuf_push_code(BitBuf* b, const char* code){
    for(const char* p=code; *p; p++){
        bitbuf_push_bit(b, (*p=='1'));
    }
}

/* ========================== MAIN ========================== */
int main(int argc, char** argv){
    if(argc < 2){ usage(); return 1; }
    int method = atoi(argv[1]);

    init_dct_table();

    /* ------------------ Method 0 ------------------ */
    if(method==0){
        if(argc!=7){
            printf("Usage: encoder 0 input.bmp R.txt G.txt B.txt dim.txt\n");
            return 1;
        }
        const char* bmp = argv[2];
        int W,H, has54=0; uint8_t hdr54[54];
        uint8_t *R,*G,*B;
        load_bmp_topdown_rgb(bmp,&W,&H,&R,&G,&B,hdr54,&has54);

        FILE* fr=fopen(argv[3],"w");
        FILE* fg=fopen(argv[4],"w");
        FILE* fb=fopen(argv[5],"w");
        FILE* fd=fopen(argv[6],"w");
        if(!fr||!fg||!fb||!fd) die("open output failed");

        // dim.txt: first line W H, second line optional HDR54 hex
        fprintf(fd,"%d %d\n",W,H);
        if(has54){
            fprintf(fd,"HDR54 ");
            for(int i=0;i<54;i++) fprintf(fd,"%02X", hdr54[i]);
            fprintf(fd,"\n");
        }

        for(int y=0;y<H;y++){
            for(int x=0;x<W;x++){
                fprintf(fr,"%u%s", R[y*W+x], (x==W-1)?"":" ");
                fprintf(fg,"%u%s", G[y*W+x], (x==W-1)?"":" ");
                fprintf(fb,"%u%s", B[y*W+x], (x==W-1)?"":" ");
            }
            fprintf(fr,"\n"); fprintf(fg,"\n"); fprintf(fb,"\n");
        }

        fclose(fr); fclose(fg); fclose(fb); fclose(fd);
        free(R); free(G); free(B);
        return 0;
    }

    /* ------------------ Method 1 ------------------ */
    if(method==1){
        if(argc!=13){
            printf("Usage: encoder 1 input.bmp Qt_Y.txt Qt_Cb.txt Qt_Cr.txt dim.txt qF_Y.raw qF_Cb.raw qF_Cr.raw eF_Y.raw eF_Cb.raw eF_Cr.raw\n");
            return 1;
        }
        const char* bmp=argv[2];
        const char* qtY=argv[3];
        const char* qtCb=argv[4];
        const char* qtCr=argv[5];
        const char* dim=argv[6];
        const char* qFY=argv[7];
        const char* qFCb=argv[8];
        const char* qFCr=argv[9];
        const char* eFY=argv[10];
        const char* eFCb=argv[11];
        const char* eFCr=argv[12];

        // write QTs
        write_qt_txt(qtY, QT_Y);
        write_qt_txt(qtCb, QT_C);
        write_qt_txt(qtCr, QT_C);

        int W,H, has54=0; uint8_t hdr54[54];
        uint8_t *R,*G,*B;
        load_bmp_topdown_rgb(bmp,&W,&H,&R,&G,&B,hdr54,&has54);

        FILE* fd=fopen(dim,"w"); if(!fd) die("open dim failed");
        fprintf(fd,"%d %d\n",W,H);
        if(has54){
            fprintf(fd,"HDR54 ");
            for(int i=0;i<54;i++) fprintf(fd,"%02X", hdr54[i]);
            fprintf(fd,"\n");
        }
        fclose(fd);

        FILE* fqY=fopen(qFY,"wb");
        FILE* fqCb=fopen(qFCb,"wb");
        FILE* fqCr=fopen(qFCr,"wb");
        FILE* feY=fopen(eFY,"wb");
        FILE* feCb=fopen(eFCb,"wb");
        FILE* feCr=fopen(eFCr,"wb");
        if(!fqY||!fqCb||!fqCr||!feY||!feCb||!feCr) die("open raw failed");

        int bw=(W+7)/8, bh=(H+7)/8;

        double sig[3][8][8]={0}, noi[3][8][8]={0};

        for(int by=0; by<bh; by++){
            for(int bx=0; bx<bw; bx++){
                double blk[3][8][8], F[3][8][8];

                for(int i=0;i<8;i++){
                    for(int j=0;j<8;j++){
                        int y = by*8+i; if(y>=H) y=H-1;
                        int x = bx*8+j; if(x>=W) x=W-1;
                        double Yv,Cbv,Crv;
                        rgb_to_ycbcr(R[y*W+x],G[y*W+x],B[y*W+x],&Yv,&Cbv,&Crv);
                        blk[0][i][j]=Yv-128.0;
                        blk[1][i][j]=Cbv-128.0;
                        blk[2][i][j]=Crv-128.0;
                    }
                }

                for(int c=0;c<3;c++) dct8x8(blk[c], F[c]);

                for(int u=0;u<8;u++){
                    for(int v=0;v<8;v++){
                        // Y
                        {
                            double q = (double)QT_Y[u][v];
                            double f = F[0][u][v];
                            int16_t qi = (int16_t)llround(f/q);
                            float   ei = (float)(f - (double)qi*q);
                            fwrite(&qi, sizeof(int16_t), 1, fqY);
                            fwrite(&ei, sizeof(float),   1, feY);
                            sig[0][u][v]+=f*f;
                            noi[0][u][v]+=(double)ei*(double)ei;
                        }
                        // Cb
                        {
                            double q = (double)QT_C[u][v];
                            double f = F[1][u][v];
                            int16_t qi = (int16_t)llround(f/q);
                            float   ei = (float)(f - (double)qi*q);
                            fwrite(&qi, sizeof(int16_t), 1, fqCb);
                            fwrite(&ei, sizeof(float),   1, feCb);
                            sig[1][u][v]+=f*f;
                            noi[1][u][v]+=(double)ei*(double)ei;
                        }
                        // Cr
                        {
                            double q = (double)QT_C[u][v];
                            double f = F[2][u][v];
                            int16_t qi = (int16_t)llround(f/q);
                            float   ei = (float)(f - (double)qi*q);
                            fwrite(&qi, sizeof(int16_t), 1, fqCr);
                            fwrite(&ei, sizeof(float),   1, feCr);
                            sig[2][u][v]+=f*f;
                            noi[2][u][v]+=(double)ei*(double)ei;
                        }
                    }
                }
            }
        }

        fclose(fqY); fclose(fqCb); fclose(fqCr);
        fclose(feY); fclose(feCb); fclose(feCr);
        free(R); free(G); free(B);

        // print SQNR_Freq 3x64
        printf("SQNR_Freq (dB) 3x64 (Y Cb Cr), order u=0..7 v=0..7\n");
        const char* names[3]={"Y","Cb","Cr"};
        for(int c=0;c<3;c++){
            printf("%s:\n", names[c]);
            for(int u=0;u<8;u++){
                for(int v=0;v<8;v++){
                    if(noi[c][u][v]<=0) printf("INF");
                    else printf("%.6f", 10.0*log10(sig[c][u][v]/noi[c][u][v]));
                    if(!(u==7 && v==7)) printf(" ");
                }
            }
            printf("\n");
        }
        return 0;
    }

    /* ------------------ Method 2 (RLE) ------------------ */
    if(method==2){
        if(argc!=5){
            printf("Usage: encoder 2 input.bmp ascii|binary rle_code\n");
            return 1;
        }
        const char* bmp=argv[2];
        const int is_ascii = (strcmp(argv[3],"ascii")==0);
        const int is_bin   = (strcmp(argv[3],"binary")==0);
        if(!is_ascii && !is_bin) die("Method-2: third arg must be ascii or binary");

        int W,H, has54=0; uint8_t hdr54[54];
        uint8_t *R,*G,*B;
        load_bmp_topdown_rgb(bmp,&W,&H,&R,&G,&B,hdr54,&has54);

        FILE* out = fopen(argv[4], is_ascii? "w":"wb");
        if(!out) die("open rle output failed");

        int bw=(W+7)/8, bh=(H+7)/8;

        if(is_ascii){
            fprintf(out,"%d %d\n", W, H);
        }else{
            // binary header: "M2B0" + W,H (int32) + bw,bh (int32)
            fwrite("M2B0",1,4,out);
            int32_t iW=W,iH=H, iBW=bw, iBH=bh;
            fwrite(&iW,4,1,out); fwrite(&iH,4,1,out);
            fwrite(&iBW,4,1,out); fwrite(&iBH,4,1,out);
        }

        int16_t prevDC[3]={0,0,0};

        for(int m=0;m<bh;m++){
            for(int n=0;n<bw;n++){
                double blk[3][8][8], F[3][8][8];
                int16_t q[3][8][8];

                // build block (top-down), level shift
                for(int i=0;i<8;i++){
                    for(int j=0;j<8;j++){
                        int y=m*8+i; if(y>=H) y=H-1;
                        int x=n*8+j; if(x>=W) x=W-1;
                        double Yv,Cbv,Crv;
                        rgb_to_ycbcr(R[y*W+x],G[y*W+x],B[y*W+x],&Yv,&Cbv,&Crv);
                        blk[0][i][j]=Yv-128.0;
                        blk[1][i][j]=Cbv-128.0;
                        blk[2][i][j]=Crv-128.0;
                    }
                }

                for(int c=0;c<3;c++){
                    dct8x8(blk[c], F[c]);
                    for(int u=0;u<8;u++){
                        for(int v=0;v<8;v++){
                            double Q = (c==0)? (double)QT_Y[u][v] : (double)QT_C[u][v];
                            q[c][u][v] = (int16_t)llround(F[c][u][v]/Q);
                        }
                    }
                }

                // per channel: DPCM on DC then ZigZag + RLE
                for(int c=0;c<3;c++){
                    // collect 64 coefficients in zigzag order
                    int16_t zz[64];
                    for(int k=0;k<64;k++){
                        int u=ZZU[k], v=ZZV[k];
                        zz[k]=q[c][u][v];
                    }
                    // DPCM DC
                    int16_t dc = zz[0];
                    int16_t diff = (int16_t)(dc - prevDC[c]);
                    prevDC[c] = dc;
                    zz[0] = diff;

                    // RLE pairs for NONZERO, store as (skip,val) with "skip:val" in ascii to match你現在的 rle_code.txt
                    Pair pairs[256];
                    int pc=0;
                    int zc=0;
                    for(int k=0;k<64;k++){
                        int16_t v = zz[k];
                        if(v==0) zc++;
                        else {
                            pairs[pc].skip = (int16_t)zc;
                            pairs[pc].val  = v;
                            pc++;
                            zc=0;
                        }
                    }

                    if(is_ascii){
                        const char* ch = (c==0)?"Y":(c==1)?"Cb":"Cr";
                        fprintf(out,"(%d,%d,%s)", m,n,ch);
                        for(int i=0;i<pc;i++){
                            fprintf(out," %d:%d", (int)pairs[i].skip, (int)pairs[i].val);
                        }
                        fprintf(out,"\n");
                    }else{
                        // binary record: uint16 pc, then pc*(int16 skip, int16 val)
                        uint16_t upc = (uint16_t)pc;
                        fwrite(&upc,2,1,out);
                        fwrite(pairs,sizeof(Pair),(size_t)pc,out);
                    }
                }
            }
        }

        fclose(out);
        free(R); free(G); free(B);
        return 0;
    }

    /* ------------------ Method 3 (Huffman over Method2-binary payload) ------------------ */
    if(method==3){
        if(argc!=6){
            printf("Usage: encoder 3 input.bmp ascii|binary codebook.txt huffman_code\n");
            return 1;
        }
        const char* bmp=argv[2];
        const int is_ascii = (strcmp(argv[3],"ascii")==0);
        const int is_bin   = (strcmp(argv[3],"binary")==0);
        if(!is_ascii && !is_bin) die("Method-3: third arg must be ascii or binary");
        const char* codebook_path = argv[4];
        const char* huf_path = argv[5];

        // Step1: Generate Method-2 binary payload in memory buffer by reusing Method-2 encode logic,
        //        but write into temp file then read back. (keeps encoder single-file; simplest)
        const char* tmp_m2 = "__m2_tmp_payload.bin";
        {
            char* argv2[] = { argv[0], "2", (char*)bmp, "binary", (char*)tmp_m2 };
            // call our method2 branch by simulating; easiest: spawn re-run not possible, so just re-enter:
            // We will just run the method2 code by calling main recursively is bad.
            // So we implement minimal: call system on Windows is allowed but not ideal.
            // For robustness, we require user to run method2 first if you really want no temp.
            // However assignment expects method3 standalone. We'll do a local re-run via system:
            char cmd[2048];
            snprintf(cmd,sizeof(cmd), "encoder 2 \"%s\" binary \"%s\"", bmp, tmp_m2);
            int rc = system(cmd);
            if(rc!=0) die("Method-3: failed to invoke method2 encoder (system call). Please run: encoder 2 input.bmp binary rle_code.bin first.");
        }

        // read payload bytes
        FILE* fp = fopen(tmp_m2,"rb");
        if(!fp) die("Method-3: open tmp payload failed");
        fseek(fp,0,SEEK_END);
        long sz = ftell(fp);
        if(sz<=0) die("Method-3: empty payload");
        fseek(fp,0,SEEK_SET);
        uint8_t* payload = (uint8_t*)malloc((size_t)sz);
        if(!payload) die("OOM");
        if(fread(payload,1,(size_t)sz,fp)!=(size_t)sz) die("payload read failed");
        fclose(fp);
        remove(tmp_m2);

        uint64_t freq[256]={0};
        for(long i=0;i<sz;i++) freq[payload[i]]++;

        int unique=0;
        HNode* root = build_huffman(freq, &unique);

        char* codes[256]={0};
        char buf[512];
        gen_codes(root, buf, 0, codes);

        // write codebook (your format)
        FILE* fc = fopen(codebook_path,"w");
        if(!fc) die("open codebook failed");
        fprintf(fc,"M3_BYTE_HUFFMAN\n");
        fprintf(fc,"payload_size %ld\n", sz);
        fprintf(fc,"unique %d\n", unique);
        for(int s=0;s<256;s++){
            if(freq[s]>0){
                fprintf(fc,"%d %llu %s\n", s, (unsigned long long)freq[s], codes[s]);
            }
        }
        fclose(fc);

        // encode bitstream
        BitBuf bb; bitbuf_init(&bb);
        for(long i=0;i<sz;i++){
            bitbuf_push_code(&bb, codes[payload[i]]);
        }
        int padbits = (int)((8 - (bb.bit_len % 8)) % 8);
        // (already 0-filled due to calloc/realloc memset)

        if(is_ascii){
            FILE* fh = fopen(huf_path,"w");
            if(!fh) die("open huffman_code.txt failed");
            fprintf(fh,"M3\n");
            fprintf(fh,"payload_size %ld\n", sz);
            fprintf(fh,"padbits %d\n", padbits);
            // print bits as lines (80 chars/line)
            size_t total_bits = bb.bit_len;
            size_t printed=0;
            while(printed<total_bits){
                size_t line = (total_bits-printed>80)?80:(total_bits-printed);
                for(size_t k=0;k<line;k++){
                    size_t idx = printed+k;
                    uint8_t byte = bb.data[idx/8];
                    int bit = (byte >> (7-(idx%8))) & 1;
                    fputc(bit?'1':'0', fh);
                }
                fputc('\n', fh);
                printed += line;
            }
            fclose(fh);
        }else{
            FILE* fh = fopen(huf_path,"wb");
            if(!fh) die("open huffman_code.bin failed");
            // binary header: "M3B0" + payload_size(u32) + padbits(u8) + bit_bytes(u32) + data
            fwrite("M3B0",1,4,fh);
            uint32_t psz = (uint32_t)sz;
            uint8_t  pb  = (uint8_t)padbits;
            uint32_t bit_bytes = (uint32_t)((bb.bit_len + 7)/8);
            fwrite(&psz,4,1,fh);
            fwrite(&pb,1,1,fh);
            fwrite(&bit_bytes,4,1,fh);
            fwrite(bb.data,1,bit_bytes,fh);
            fclose(fh);
        }

        // cleanup
        for(int s=0;s<256;s++) free(codes[s]);
        hn_free(root);
        free(bb.data);
        free(payload);

        return 0;
    }

    usage();
    return 1;
}
