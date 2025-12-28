// decoder.c — Methods 0/1/2/3 (FULL, produces BMP)
// This decoder is designed to match the encoder.c format I provided earlier.

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ================= BMP structs ================= */
#pragma pack(push,1)
typedef struct {
    uint16_t bfType;
    uint32_t bfSize;
    uint16_t r1, r2;
    uint32_t offBits;
} BMPFileHeader;

typedef struct {
    uint32_t size;
    int32_t  w, h;
    uint16_t planes;
    uint16_t bpp;
    uint32_t comp;
    uint32_t imgSize;
    int32_t  xppm, yppm;
    uint32_t clrUsed;
    uint32_t clrImp;
} BMPInfoHeader;
#pragma pack(pop)

/* ================= Utils ================= */
static void die(const char* msg){
    fprintf(stderr, "ERROR: %s\n", msg);
    exit(1);
}
static int row24(int w){ return ((w*3+3)/4)*4; }
static int clampi(int x,int lo,int hi){ return x<lo?lo:(x>hi?hi:x); }

/* ================= DCT/IDCT tables ================= */
static double COS8[8][8];
static double A8[8];

static void init_dct(void){
    for(int u=0;u<8;u++){
        A8[u] = (u==0)? 1.0/sqrt(2.0) : 1.0;
        for(int x=0;x<8;x++){
            COS8[u][x] = cos(((2.0*x+1.0)*u*M_PI)/16.0);
        }
    }
}

static void idct8x8(const double in[8][8], double out[8][8]){
    // out[x][y] = 0.25 * sum_u sum_v alpha(u)alpha(v) in[u][v] cos(u,x) cos(v,y)
    double tmp[8][8] = {{0}};
    for(int x=0;x<8;x++){
        for(int v=0;v<8;v++){
            double s=0.0;
            for(int u=0;u<8;u++){
                s += A8[u]*in[u][v]*COS8[u][x];
            }
            tmp[x][v]=s;
        }
    }
    for(int x=0;x<8;x++){
        for(int y=0;y<8;y++){
            double s=0.0;
            for(int v=0;v<8;v++){
                s += A8[v]*tmp[x][v]*COS8[v][y];
            }
            out[x][y] = 0.25*s;
        }
    }
}

/* ================= Color ================= */
static void ycbcr_to_rgb(double Y, double Cb, double Cr, uint8_t* R, uint8_t* G, uint8_t* B){
    double r = Y + 1.402*(Cr-128.0);
    double g = Y - 0.344136*(Cb-128.0) - 0.714136*(Cr-128.0);
    double b = Y + 1.772*(Cb-128.0);
    int ri = (int)llround(r);
    int gi = (int)llround(g);
    int bi = (int)llround(b);
    *R = (uint8_t)clampi(ri,0,255);
    *G = (uint8_t)clampi(gi,0,255);
    *B = (uint8_t)clampi(bi,0,255);
}

/* ================= Quant tables (must match encoder) ================= */
static const int QY[8][8]={
 {16,11,10,16,24,40,51,61},{12,12,14,19,26,58,60,55},
 {14,13,16,24,40,57,69,56},{14,17,22,29,51,87,80,62},
 {18,22,37,56,68,109,103,77},{24,35,55,64,81,104,113,92},
 {49,64,78,87,103,121,120,101},{72,92,95,98,112,100,103,99}
};
static const int QC[8][8]={
 {17,18,24,47,99,99,99,99},{18,21,26,66,99,99,99,99},
 {24,26,56,99,99,99,99,99},{47,66,99,99,99,99,99,99},
 {99,99,99,99,99,99,99,99},{99,99,99,99,99,99,99,99},
 {99,99,99,99,99,99,99,99},{99,99,99,99,99,99,99,99}
};

/* ================= ZigZag (must match encoder) ================= */
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

typedef struct { int16_t skip; int16_t val; } Pair;

/* ================= BMP writer using HDR54 ================= */
static void write_bmp_from_topdown_rgb(const char* outPath, int W, int H,
                                      const uint8_t* R, const uint8_t* G, const uint8_t* B,
                                      const uint8_t hdr54[54]){
    FILE* f = fopen(outPath,"wb");
    if(!f) die("open out bmp failed");

    // write original 54-byte header
    fwrite(hdr54,1,54,f);

    int rs = row24(W);
    uint8_t* row = (uint8_t*)calloc((size_t)rs,1);
    if(!row) die("OOM");

    // BMP pixel array is bottom-up
    for(int y=H-1;y>=0;y--){
        for(int x=0;x<W;x++){
            row[x*3+0] = B[y*W+x];
            row[x*3+1] = G[y*W+x];
            row[x*3+2] = R[y*W+x];
        }
        fwrite(row,1,(size_t)rs,f);
        memset(row,0,(size_t)rs);
    }

    free(row);
    fclose(f);
}

/* ================= dim.txt reader (W H + HDR54 line) ================= */
static void read_dim_and_hdr54(const char* dimPath, int* W, int* H, uint8_t hdr54[54]){
    FILE* fd = fopen(dimPath,"r");
    if(!fd) die("open dim.txt failed");

    if(fscanf(fd,"%d %d", W, H)!=2) die("dim.txt missing W H");

    char tag[16]={0};
    if(fscanf(fd,"%15s", tag)!=1) die("dim.txt missing HDR54 tag");
    if(strcmp(tag,"HDR54")!=0) die("dim.txt HDR54 missing");

    for(int i=0;i<54;i++){
        unsigned int byte;
        if(fscanf(fd,"%2x",&byte)!=1) die("dim.txt HDR54 hex parse failed");
        hdr54[i]=(uint8_t)byte;
    }
    fclose(fd);
}

/* =========================================================
   Method 0 decoder
   decoder 0 out.bmp R.txt G.txt B.txt dim.txt
========================================================= */
static void decode_method0(int argc, char** argv){
    if(argc!=7) die("Usage: decoder 0 out.bmp R.txt G.txt B.txt dim.txt");
    const char* outbmp = argv[2];
    const char* rtxt   = argv[3];
    const char* gtxt   = argv[4];
    const char* btxt   = argv[5];
    const char* dim    = argv[6];

    int W,H;
    uint8_t hdr54[54];
    read_dim_and_hdr54(dim,&W,&H,hdr54);

    FILE* fr=fopen(rtxt,"r");
    FILE* fg=fopen(gtxt,"r");
    FILE* fb=fopen(btxt,"r");
    if(!fr||!fg||!fb) die("open R/G/B txt failed");

    uint8_t* R=(uint8_t*)malloc((size_t)W*H);
    uint8_t* G=(uint8_t*)malloc((size_t)W*H);
    uint8_t* B=(uint8_t*)malloc((size_t)W*H);
    if(!R||!G||!B) die("OOM");

    for(int y=0;y<H;y++){
        for(int x=0;x<W;x++){
            unsigned int v;
            if(fscanf(fr,"%u",&v)!=1) die("R.txt parse failed");
            R[y*W+x]=(uint8_t)v;
            if(fscanf(fg,"%u",&v)!=1) die("G.txt parse failed");
            G[y*W+x]=(uint8_t)v;
            if(fscanf(fb,"%u",&v)!=1) die("B.txt parse failed");
            B[y*W+x]=(uint8_t)v;
        }
    }

    fclose(fr); fclose(fg); fclose(fb);

    write_bmp_from_topdown_rgb(outbmp,W,H,R,G,B,hdr54);
    free(R); free(G); free(B);
}

/* =========================================================
   Method 1 decoder
   1(a): decoder 1 out.bmp original.bmp Qt_Y Qt_Cb Qt_Cr dim qF_Y qF_Cb qF_Cr
   1(b): decoder 1 out.bmp Qt_Y Qt_Cb Qt_Cr dim qF_Y qF_Cb qF_Cr eF_Y eF_Cb eF_Cr
   (flexible: if an arg looks like *.bmp, treat as original.bmp)
========================================================= */
static int ends_with_bmp(const char* s){
    size_t n=strlen(s);
    return (n>=4 && (s[n-4]=='.' || s[n-4]=='.') &&
            (s[n-3]=='b'||s[n-3]=='B') &&
            (s[n-2]=='m'||s[n-2]=='M') &&
            (s[n-1]=='p'||s[n-1]=='P'));
}

static void decode_method1(int argc, char** argv){
    // detect form
    // possible argc: 11 (a), 13 (b), 14 (b with original)
    if(!(argc==11 || argc==13 || argc==14)){
        die("Usage:\n"
            "  decoder 1 out.bmp original.bmp Qt_Y Qt_Cb Qt_Cr dim qF_Y qF_Cb qF_Cr\n"
            "  decoder 1 out.bmp Qt_Y Qt_Cb Qt_Cr dim qF_Y qF_Cb qF_Cr eF_Y eF_Cb eF_Cr");
    }

    int idx = 2;
    const char* outbmp = argv[idx++];

    uint8_t hdr54[54]={0};
    int has_orig = 0;

    // if next arg is bmp, read hdr54 from it
    if(idx < argc && ends_with_bmp(argv[idx])){
        has_orig = 1;
        FILE* fo = fopen(argv[idx], "rb");
        if(!fo) die("open original.bmp failed");
        if(fread(hdr54,1,54,fo)!=54) die("read original header failed");
        fclose(fo);
        idx++;
    }

    const char* qtY  = argv[idx++];
    const char* qtCb = argv[idx++];
    const char* qtCr = argv[idx++];
    const char* dim  = argv[idx++];

    int W,H;
    if(!has_orig){
        read_dim_and_hdr54(dim,&W,&H,hdr54);
    }else{
        // still need W,H from dim
        FILE* fd=fopen(dim,"r");
        if(!fd) die("open dim.txt failed");
        if(fscanf(fd,"%d %d",&W,&H)!=2) die("dim W H parse failed");
        fclose(fd);
    }

    const char* qFY  = argv[idx++];
    const char* qFCb = argv[idx++];
    const char* qFCr = argv[idx++];

    const char* eFY=NULL; const char* eFCb=NULL; const char* eFCr=NULL;
    int has_e = 0;
    if(idx < argc){
        // must be 3 files
        if((argc - idx) != 3) die("method1: eF args count mismatch");
        has_e = 1;
        eFY  = argv[idx++];
        eFCb = argv[idx++];
        eFCr = argv[idx++];
    }

    FILE* fqy  = fopen(qFY,"rb");
    FILE* fqcb = fopen(qFCb,"rb");
    FILE* fqcr = fopen(qFCr,"rb");
    if(!fqy||!fqcb||!fqcr) die("open qF raw failed");

    FILE* fey=NULL; FILE* fecb=NULL; FILE* fecr=NULL;
    if(has_e){
        fey  = fopen(eFY,"rb");
        fecb = fopen(eFCb,"rb");
        fecr = fopen(eFCr,"rb");
        if(!fey||!fecb||!fecr) die("open eF raw failed");
    }

    uint8_t* R=(uint8_t*)malloc((size_t)W*H);
    uint8_t* G=(uint8_t*)malloc((size_t)W*H);
    uint8_t* B=(uint8_t*)malloc((size_t)W*H);
    if(!R||!G||!B) die("OOM");

    int bw=(W+7)/8, bh=(H+7)/8;

    for(int by=0; by<bh; by++){
        for(int bx=0; bx<bw; bx++){
            double F[3][8][8] = {{{0}}};
            // read qF and optional eF for each channel in the same order as encoder wrote:
            // for each (u,v): Y then Cb then Cr (each file separate) in block order
            for(int u=0;u<8;u++){
                for(int v=0;v<8;v++){
                    int16_t q;
                    float e;

                    // Y
                    if(fread(&q,sizeof(int16_t),1,fqy)!=1) die("qF_Y short read");
                    double fy = (double)q * (double)QY[u][v];
                    if(has_e){
                        if(fread(&e,sizeof(float),1,fey)!=1) die("eF_Y short read");
                        fy += (double)e;
                    }
                    F[0][u][v]=fy;

                    // Cb
                    if(fread(&q,sizeof(int16_t),1,fqcb)!=1) die("qF_Cb short read");
                    double fcb = (double)q * (double)QC[u][v];
                    if(has_e){
                        if(fread(&e,sizeof(float),1,fecb)!=1) die("eF_Cb short read");
                        fcb += (double)e;
                    }
                    F[1][u][v]=fcb;

                    // Cr
                    if(fread(&q,sizeof(int16_t),1,fqcr)!=1) die("qF_Cr short read");
                    double fcr = (double)q * (double)QC[u][v];
                    if(has_e){
                        if(fread(&e,sizeof(float),1,fecr)!=1) die("eF_Cr short read");
                        fcr += (double)e;
                    }
                    F[2][u][v]=fcr;
                }
            }

            double blkY[8][8], blkCb[8][8], blkCr[8][8];
            idct8x8(F[0], blkY);
            idct8x8(F[1], blkCb);
            idct8x8(F[2], blkCr);

            for(int i=0;i<8;i++){
                for(int j=0;j<8;j++){
                    int y = by*8+i;
                    int x = bx*8+j;
                    if(y>=H || x>=W) continue;
                    double Yv  = blkY[i][j]  + 128.0;
                    double Cbv = blkCb[i][j] + 128.0;
                    double Crv = blkCr[i][j] + 128.0;
                    ycbcr_to_rgb(Yv, Cbv, Crv, &R[y*W+x], &G[y*W+x], &B[y*W+x]);
                }
            }
        }
    }

    fclose(fqy); fclose(fqcb); fclose(fqcr);
    if(has_e){ fclose(fey); fclose(fecb); fclose(fecr); }

    write_bmp_from_topdown_rgb(outbmp,W,H,R,G,B,hdr54);
    free(R); free(G); free(B);
}

/* =========================================================
   Method 2 decoder
   ascii: first line W H, then lines: (m,n,Y|Cb|Cr) skip:val skip:val ...
   binary: "M2B0" + W,H,bw,bh (int32), then for each block and channel:
           uint16 pc + pc*(int16 skip,int16 val)
========================================================= */
static int parse_line_header(const char* line, int* m, int* n, char ch[4], const char** rest){
    // expects "(m,n,CH)" where CH is Y or Cb or Cr
    // return 1 ok, 0 fail
    int mm, nn;
    char cc[4]={0};
    // find closing ')'
    const char* rp = strchr(line, ')');
    if(!rp) return 0;

    // copy header part
    char head[64]={0};
    size_t len = (size_t)(rp - line + 1);
    if(len >= sizeof(head)) return 0;
    memcpy(head, line, len);
    head[len]='\0';

    if(sscanf(head,"(%d,%d,%3[^)])",&mm,&nn,cc)!=3) return 0;
    *m=mm; *n=nn;
    strncpy(ch, cc, 3);
    ch[3]='\0';
    *rest = rp + 1;
    return 1;
}

static void decode_method2_from_file(const char* outbmp, const char* mode, const char* rlePath,
                                    const uint8_t hdr54[54], int W_from_dim, int H_from_dim, int has_dim_WH){
    int is_ascii = (strcmp(mode,"ascii")==0);
    int is_bin   = (strcmp(mode,"binary")==0);
    if(!is_ascii && !is_bin) die("method2: mode must be ascii or binary");

    FILE* f = fopen(rlePath, is_ascii?"r":"rb");
    if(!f) die("open rle_code failed");

    int W=0,H=0;
    int bw=0,bh=0;

    if(is_ascii){
        if(fscanf(f,"%d %d",&W,&H)!=2) die("method2 ascii: missing W H");
        // consume endline after header
        int c;
        while((c=fgetc(f))!=EOF && c!='\n'){}
    }else{
        char magic[4];
        if(fread(magic,1,4,f)!=4) die("method2 bin: short read magic");
        if(memcmp(magic,"M2B0",4)!=0) die("method2 bin: bad magic");
        int32_t iW,iH,iBW,iBH;
        if(fread(&iW,4,1,f)!=1) die("method2 bin: read W fail");
        if(fread(&iH,4,1,f)!=1) die("method2 bin: read H fail");
        if(fread(&iBW,4,1,f)!=1) die("method2 bin: read bw fail");
        if(fread(&iBH,4,1,f)!=1) die("method2 bin: read bh fail");
        W=iW; H=iH; bw=iBW; bh=iBH;
    }

    // If caller provides dim W/H, ensure consistent (helps catch mismatch)
    if(has_dim_WH){
        if(W!=W_from_dim || H!=H_from_dim){
            // still allow but warn
            fprintf(stderr,"WARN: rle header W/H (%d,%d) != dim W/H (%d,%d)\n", W,H, W_from_dim,H_from_dim);
        }
    }

    if(!is_bin){
        bw = (W+7)/8;
        bh = (H+7)/8;
    }

    uint8_t* R=(uint8_t*)malloc((size_t)W*H);
    uint8_t* G=(uint8_t*)malloc((size_t)W*H);
    uint8_t* B=(uint8_t*)malloc((size_t)W*H);
    if(!R||!G||!B) die("OOM");

    int16_t prevDC[3]={0,0,0};

    // For each block, we must read 3 channels in order Y, Cb, Cr (as encoder writes).
    for(int m=0;m<bh;m++){
        for(int n=0;n<bw;n++){
            double blk[3][8][8]; // spatial (level-shifted)
            for(int c=0;c<3;c++){
                int16_t zz[64]={0};

                if(is_ascii){
                    char line[8192];
                    if(!fgets(line,sizeof(line),f)) die("method2 ascii: unexpected EOF line");

                    int mm, nn; char ch[4]; const char* rest=NULL;
                    if(!parse_line_header(line,&mm,&nn,ch,&rest)) die("method2 ascii: bad line header");
                    // sanity: block index should match expected traversal
                    // allow mismatch but warn
                    if(mm!=m || nn!=n){
                        fprintf(stderr,"WARN: ascii block index mismatch: got (%d,%d) expected (%d,%d)\n", mm,nn,m,n);
                    }

                    // channel order check
                    const char* expect = (c==0)?"Y":(c==1)?"Cb":"Cr";
                    if(strcmp(ch,expect)!=0){
                        fprintf(stderr,"WARN: ascii channel mismatch: got %s expect %s at block(%d,%d)\n", ch,expect,m,n);
                    }

                    // parse tokens "skip:val"
                    int k=0;
                    const char* p = rest;
                    while(*p){
                        while(*p==' '||*p=='\t'||*p=='\r'||*p=='\n') p++;
                        if(!*p) break;
                        int skip=0, val=0;
                        if(sscanf(p,"%d:%d",&skip,&val)==2){
                            k += skip;
                            if(k>=64) break;
                            zz[k++] = (int16_t)val;
                        }else{
                            break;
                        }
                        // advance p to next space
                        const char* sp = strchr(p,' ');
                        if(!sp) break;
                        p = sp+1;
                    }
                }else{
                    uint16_t pc=0;
                    if(fread(&pc,2,1,f)!=1) die("method2 bin: read pc fail");
                    int k=0;
                    for(uint16_t i=0;i<pc;i++){
                        Pair pr;
                        if(fread(&pr,sizeof(Pair),1,f)!=1) die("method2 bin: read pair fail");
                        k += pr.skip;
                        if(k>=64) die("method2 bin: RLE overflow");
                        zz[k++] = pr.val;
                    }
                }

                // DC inverse DPCM: zz[0] is diff; actual_dc = prevDC + diff
                int16_t diff = zz[0];
                int16_t dc = (int16_t)(prevDC[c] + diff);
                prevDC[c] = dc;
                zz[0] = dc;

                // De-zigzag into F[u][v], then dequant, then IDCT
                double F[8][8]={{0}};
                for(int t=0;t<64;t++){
                    int u=ZZU[t], v=ZZV[t];
                    double q = (double)zz[t];
                    double Q = (c==0)? (double)QY[u][v] : (double)QC[u][v];
                    F[u][v] = q * Q;
                }
                idct8x8(F, blk[c]);
            }

            // combine channels -> RGB
            for(int i=0;i<8;i++){
                for(int j=0;j<8;j++){
                    int y=m*8+i, x=n*8+j;
                    if(y>=H || x>=W) continue;
                    double Yv  = blk[0][i][j] + 128.0;
                    double Cbv = blk[1][i][j] + 128.0;
                    double Crv = blk[2][i][j] + 128.0;
                    ycbcr_to_rgb(Yv,Cbv,Crv,&R[y*W+x],&G[y*W+x],&B[y*W+x]);
                }
            }
        }
    }

    fclose(f);
    write_bmp_from_topdown_rgb(outbmp,W,H,R,G,B,hdr54);
    free(R); free(G); free(B);
}

static void decode_method2(int argc, char** argv){
    if(argc!=5) die("Usage: decoder 2 out.bmp ascii|binary rle_code");
    const char* outbmp = argv[2];
    const char* mode   = argv[3];
    const char* rle    = argv[4];

    // We need HDR54 for output; prefer dim.txt if exists? method2 CLI doesn't include dim
    // So we build a standard 24-bit BMP header if no HDR54 is given:
    // BUT your assignment expects same header, so we try to load from "dim.txt" if present in cwd.
    // Best effort: if dim.txt exists, use it.
    uint8_t hdr54[54]={0};
    int W=0,H=0, has_dim=0;

    FILE* fd = fopen("dim.txt","r");
    if(fd){
        fclose(fd);
        read_dim_and_hdr54("dim.txt",&W,&H,hdr54);
        has_dim=1;
    }else{
        // fallback: minimal valid 24-bit BMP header; W/H will be taken from rle header
        // We'll fill later after reading rle; easiest is to set a basic template now:
        uint8_t tmp[54]={
            0x42,0x4D,0,0,0,0,0,0,0,0,54,0,0,0,
            40,0,0,0,0,0,0,0,0,0,0,0,1,0,24,0,
            0,0,0,0,0,0,0,0,0xC4,0x0E,0,0,0xC4,0x0E,0,0,0,0,0,0,0,0,0,0
        };
        memcpy(hdr54,tmp,54);
    }

    decode_method2_from_file(outbmp,mode,rle,hdr54,W,H,has_dim);
}

/* =========================================================
   Method 3 Huffman decode
   - Decode Huffman -> payload bytes (this payload is Method-2 binary file bytes)
   - Then call Method-2 binary decoder on that temp payload file.
   decoder 3 out.bmp ascii|binary codebook.txt huffman_code.(txt|bin)
========================================================= */
typedef struct HNode {
    int is_leaf;
    int sym;
    struct HNode* zero;
    struct HNode* one;
} HNode;

static HNode* hn_new(void){
    HNode* n=(HNode*)calloc(1,sizeof(HNode));
    if(!n) die("OOM");
    return n;
}
static void hn_free(HNode* n){
    if(!n) return;
    hn_free(n->zero);
    hn_free(n->one);
    free(n);
}

static HNode* load_codebook_build_trie(const char* codebook_path, size_t* payload_size_out){
    FILE* f = fopen(codebook_path,"r");
    if(!f) die("open codebook.txt failed");

    char line[4096];

    // line1: M3_BYTE_HUFFMAN
    if(!fgets(line,sizeof(line),f)) die("codebook: empty");
    // line2: payload_size N
    if(!fgets(line,sizeof(line),f)) die("codebook: missing payload_size");
    size_t payload_size=0;
    if(sscanf(line,"payload_size %zu",&payload_size)!=1) die("codebook: parse payload_size failed");

    // line3: unique K
    if(!fgets(line,sizeof(line),f)) die("codebook: missing unique");

    HNode* root = hn_new();

    // remaining: sym freq code
    while(fgets(line,sizeof(line),f)){
        int sym=0;
        unsigned long long freq=0;
        char code[2048]={0};
        if(sscanf(line,"%d %llu %2047s",&sym,&freq,code)==3){
            if(sym<0 || sym>255) die("codebook: sym out of range");
            HNode* cur=root;
            for(char* p=code; *p; p++){
                if(*p=='0'){
                    if(!cur->zero) cur->zero=hn_new();
                    cur=cur->zero;
                }else if(*p=='1'){
                    if(!cur->one) cur->one=hn_new();
                    cur=cur->one;
                }else{
                    die("codebook: code contains non 0/1");
                }
            }
            cur->is_leaf=1;
            cur->sym=sym;
        }
    }

    fclose(f);
    *payload_size_out = payload_size;
    return root;
}

static uint8_t* huffman_decode_ascii_bits(FILE* f, HNode* root, size_t want_bytes){
    uint8_t* out=(uint8_t*)malloc(want_bytes);
    if(!out) die("OOM");
    size_t outLen=0;

    HNode* cur=root;
    int ch;
    while(outLen < want_bytes && (ch=fgetc(f))!=EOF){
        if(ch!='0' && ch!='1') continue;
        cur = (ch=='0')? cur->zero : cur->one;
        if(!cur) die("method3: invalid bitstream (hit NULL)");
        if(cur->is_leaf){
            out[outLen++] = (uint8_t)cur->sym;
            cur = root;
        }
    }
    if(outLen != want_bytes) die("method3: decoded bytes != payload_size");
    return out;
}

static uint8_t* huffman_decode_binary(FILE* f, HNode* root, size_t want_bytes){
    // binary header: "M3B0" + payload_size(u32)+padbits(u8)+bit_bytes(u32)+data
    char magic[4];
    if(fread(magic,1,4,f)!=4) die("m3 bin: read magic fail");
    if(memcmp(magic,"M3B0",4)!=0) die("m3 bin: bad magic");
    uint32_t psz=0, bit_bytes=0;
    uint8_t padbits=0;
    if(fread(&psz,4,1,f)!=1) die("m3 bin: read payload_size fail");
    if(fread(&padbits,1,1,f)!=1) die("m3 bin: read padbits fail");
    if(fread(&bit_bytes,4,1,f)!=1) die("m3 bin: read bit_bytes fail");

    (void)psz; // we trust codebook payload_size as truth
    uint8_t* data=(uint8_t*)malloc(bit_bytes);
    if(!data) die("OOM");
    if(fread(data,1,bit_bytes,f)!=bit_bytes) die("m3 bin: read data short");

    uint8_t* out=(uint8_t*)malloc(want_bytes);
    if(!out) die("OOM");
    size_t outLen=0;

    size_t total_bits = (size_t)bit_bytes*8;
    if(padbits>7) die("m3 bin: bad padbits");
    if(total_bits < padbits) die("m3 bin: bit length bad");
    size_t valid_bits = total_bits - padbits;

    HNode* cur=root;
    for(size_t i=0;i<valid_bits && outLen<want_bytes;i++){
        uint8_t byte = data[i/8];
        int bit = (byte >> (7-(i%8))) & 1;
        cur = bit? cur->one : cur->zero;
        if(!cur) die("method3: invalid bitstream (hit NULL)");
        if(cur->is_leaf){
            out[outLen++] = (uint8_t)cur->sym;
            cur=root;
        }
    }

    free(data);
    if(outLen != want_bytes) die("method3: decoded bytes != payload_size");
    return out;
}

static void decode_method3(int argc, char** argv){
    if(argc!=6) die("Usage: decoder 3 out.bmp ascii|binary codebook.txt huffman_code.(txt|bin)");
    const char* outbmp = argv[2];
    const char* mode   = argv[3]; // ascii|binary
    const char* codebook = argv[4];
    const char* huf = argv[5];

    size_t payload_size=0;
    HNode* root = load_codebook_build_trie(codebook, &payload_size);

    FILE* f = fopen(huf, (strcmp(mode,"ascii")==0)?"r":"rb");
    if(!f) die("open huffman_code failed");

    uint8_t* payload=NULL;

    if(strcmp(mode,"ascii")==0){
        // skip header lines: M3, payload_size, padbits
        char line[4096];
        if(!fgets(line,sizeof(line),f)) die("m3 ascii: missing line1");
        if(!fgets(line,sizeof(line),f)) die("m3 ascii: missing line2");
        if(!fgets(line,sizeof(line),f)) die("m3 ascii: missing line3");
        payload = huffman_decode_ascii_bits(f, root, payload_size);
    }else if(strcmp(mode,"binary")==0){
        payload = huffman_decode_binary(f, root, payload_size);
    }else{
        die("method3: mode must be ascii or binary");
    }

    fclose(f);
    hn_free(root);

    // payload is the entire Method-2 binary file bytes; write to temp and call method2 binary decoder
    const char* tmp = "__m3_payload_m2.bin";
    FILE* ft = fopen(tmp,"wb");
    if(!ft) die("method3: create temp payload file fail");
    if(fwrite(payload,1,payload_size,ft)!=payload_size) die("method3: write temp payload short");
    fclose(ft);
    free(payload);

    // For output BMP header, prefer dim.txt in cwd (same behavior as method2 decoder)
    // We'll decode using method2(binary) path:
    char* argv2[] = { argv[0], "2", (char*)outbmp, "binary", (char*)tmp };
    decode_method2(5, argv2);

    remove(tmp);
}

/* ================= main ================= */
static void usage(void){
    printf("Usage:\n");
    printf("  decoder 0 out.bmp R.txt G.txt B.txt dim.txt\n");
    printf("  decoder 1 out.bmp original.bmp Qt_Y.txt Qt_Cb.txt Qt_Cr.txt dim.txt qF_Y.raw qF_Cb.raw qF_Cr.raw\n");
    printf("  decoder 1 out.bmp Qt_Y.txt Qt_Cb.txt Qt_Cr.txt dim.txt qF_Y.raw qF_Cb.raw qF_Cr.raw eF_Y.raw eF_Cb.raw eF_Cr.raw\n");
    printf("  decoder 2 out.bmp ascii|binary rle_code.(txt|bin)\n");
    printf("  decoder 3 out.bmp ascii|binary codebook.txt huffman_code.(txt|bin)\n");
}

int main(int argc, char** argv){
    if(argc < 2){ usage(); return 1; }
    init_dct();
    int method = atoi(argv[1]);

    if(method==0){ decode_method0(argc,argv); return 0; }
    if(method==1){ decode_method1(argc,argv); return 0; }
    if(method==2){ decode_method2(argc,argv); return 0; }
    if(method==3){ decode_method3(argc,argv); return 0; }

    usage();
    return 1;
}
