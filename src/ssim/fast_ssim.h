#include "vidinput.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#if !defined(M_PI)
# define M_PI (3.141592653589793238462643)
#endif
#include <string.h>
/*Yes, yes, we're going to hell.*/
#if defined(_WIN32)
#include <io.h>
#include <fcntl.h>
#endif
#include "getopt.h"

/*Implements the FastSSIM algorithm, see:
  Chen, Ming-Jun, and Alan C. Bovik. "Fast structural similarity index
   algorithm." Journal of Real-Time Image Processing 6.4 (2011): 281-287.*/

const char *optstring = "cfrs";
const struct option options[]={
  {"show-chroma",no_argument,NULL,'c'},
  {"frame-type",no_argument,NULL,'f'},
  {"raw",no_argument,NULL,'r'},
  {"summary",no_argument,NULL,'s'},
  {NULL,0,NULL,0}
};

static int show_frame_type;
static int summary_only;
static int show_chroma;

typedef struct fs_level fs_level;
typedef struct fs_ctx   fs_ctx;

#define SSIM_K1 (0.01*0.01)
#define SSIM_K2 (0.03*0.03)

#define FS_MINI(_a,_b) ((_a)<(_b)?(_a):(_b))
#define FS_MAXI(_a,_b) ((_a)>(_b)?(_a):(_b))

struct fs_level{
  int32_t *im1;
  int32_t *im2;
  double       *ssim;
  int           w;
  int           h;
};

struct fs_ctx{
  fs_level *level;
  int       nlevels;
  int depth;
  unsigned *col_buf;
};

static void fs_ctx_init(fs_ctx *_ctx,int _w,int _h,int _depth,int _nlevels){
  unsigned char *data;
  size_t         data_size;
  int            lw;
  int            lh;
  int            l;
  lw = (_w + 1) >> 1;
  lh = (_h + 1) >> 1;
  data_size=_nlevels*sizeof(fs_level)+2*(lw+8)*8*sizeof(*_ctx->col_buf);
  for(l=0;l<_nlevels;l++){
    size_t im_size;
    size_t level_size;
    im_size=lw*(size_t)lh;
    level_size=2*im_size*sizeof(*_ctx->level[l].im1);
    level_size+=sizeof(*_ctx->level[l].ssim)-1;
    level_size/=sizeof(*_ctx->level[l].ssim);
    level_size+=im_size;
    level_size*=sizeof(*_ctx->level[l].ssim);
    data_size+=level_size;
    lw = (lw + 1) >> 1;
    lh = (lh + 1) >> 1;
  }
  data=(unsigned char *)malloc(data_size);
  _ctx->level=(fs_level *)data;
  _ctx->nlevels=_nlevels;
  _ctx->depth = _depth;
  data+=_nlevels*sizeof(*_ctx->level);
  lw = (_w + 1) >> 1;
  lh = (_h + 1) >> 1;
  for(l=0;l<_nlevels;l++){
    size_t im_size;
    size_t level_size;
    _ctx->level[l].w=lw;
    _ctx->level[l].h=lh;
    im_size=lw*(size_t)lh;
    level_size=2*im_size*sizeof(*_ctx->level[l].im1);
    level_size+=sizeof(*_ctx->level[l].ssim)-1;
    level_size/=sizeof(*_ctx->level[l].ssim);
    level_size*=sizeof(*_ctx->level[l].ssim);
    _ctx->level[l].im1=(int32_t *)data;
    _ctx->level[l].im2=_ctx->level[l].im1+im_size;
    data+=level_size;
    _ctx->level[l].ssim=(double *)data;
    data+=im_size*sizeof(*_ctx->level[l].ssim);
    lw = (lw + 1) >> 1;
    lh = (lh + 1) >> 1;
  }
  _ctx->col_buf=(unsigned *)data;
}

static void fs_ctx_clear(fs_ctx *_ctx){
  free(_ctx->level);
}

static void fs_downsample_level(fs_ctx *_ctx,int _l){
  const int32_t *src1;
  const int32_t *src2;
  int32_t       *dst1;
  int32_t       *dst2;
  int                 w2;
  int                 h2;
  int                 w;
  int                 h;
  int                 i;
  int                 j;
  w=_ctx->level[_l].w;
  h=_ctx->level[_l].h;
  dst1=_ctx->level[_l].im1;
  dst2=_ctx->level[_l].im2;
  w2=_ctx->level[_l-1].w;
  h2=_ctx->level[_l-1].h;
  src1=_ctx->level[_l-1].im1;
  src2=_ctx->level[_l-1].im2;
  for(j=0;j<h;j++){
    int j0offs;
    int j1offs;
    j0offs=2*j*w2;
    j1offs=FS_MINI(2*j+1,h2)*w2;
    for(i=0;i<w;i++){
      int i0;
      int i1;
      i0=2*i;
      i1=FS_MINI(i0+1,w2);
      dst1[j*w+i]=src1[j0offs+i0]+src1[j0offs+i1]
       +src1[j1offs+i0]+src1[j1offs+i1];
      dst2[j*w+i]=src2[j0offs+i0]+src2[j0offs+i1]
       +src2[j1offs+i0]+src2[j1offs+i1];
    }
  }
}

static void fs_downsample_level0(fs_ctx *_ctx,const unsigned char *_src1,
 int _s1ystride,const unsigned char *_src2,int _s2ystride,int _w,int _h,
 int depth){
  int32_t *dst1;
  int32_t *dst2;
  int           w;
  int           h;
  int           i;
  int           j;
  w=_ctx->level[0].w;
  h=_ctx->level[0].h;
  dst1=_ctx->level[0].im1;
  dst2=_ctx->level[0].im2;
  for(j=0;j<h;j++){
    int j0;
    int j1;
    j0=2*j;
    j1=FS_MINI(j0+1,_h-1);
    for(i=0;i<w;i++){
      int i0;
      int i1;
      i0=2*i;
      i1=FS_MINI(i0+1,_w-1);
      if (depth > 8) {
        dst1[j*w + i] =
         _src1[j0*_s1ystride + i0*2] + (_src1[j0*_s1ystride + i0*2 + 1] << 8) +
         _src1[j0*_s1ystride + i1*2] + (_src1[j0*_s1ystride + i1*2 + 1] << 8) +
         _src1[j1*_s1ystride + i0*2] + (_src1[j1*_s1ystride + i0*2 + 1] << 8) +
         _src1[j1*_s1ystride + i1*2] + (_src1[j1*_s1ystride + i1*2 + 1] << 8);
        dst2[j*w + i] =
         _src2[j0*_s2ystride + i0*2] + (_src2[j0*_s2ystride + i0*2 + 1] << 8) +
         _src2[j0*_s2ystride + i1*2] + (_src2[j0*_s2ystride + i1*2 + 1] << 8) +
         _src2[j1*_s2ystride + i0*2] + (_src2[j1*_s2ystride + i0*2 + 1] << 8) +
         _src2[j1*_s2ystride + i1*2] + (_src2[j1*_s2ystride + i1*2 + 1] << 8);
      } else {
        dst1[j*w+i]=_src1[j0*_s1ystride+i0]+_src1[j0*_s1ystride+i1]
         +_src1[j1*_s1ystride+i0]+_src1[j1*_s1ystride+i1];
        dst2[j*w+i]=_src2[j0*_s2ystride+i0]+_src2[j0*_s2ystride+i1]
         +_src2[j1*_s2ystride+i0]+_src2[j1*_s2ystride+i1];
      }
    }
  }
}

static void fs_apply_luminance(fs_ctx *_ctx,int _l){
  unsigned     *col_sums_x;
  unsigned     *col_sums_y;
  int32_t *im1;
  int32_t *im2;
  double       *ssim;
  double        c1;
  int           w;
  int           h;
  int           j0offs;
  int           j1offs;
  int           i;
  int           j;
  int samplemax;
  w=_ctx->level[_l].w;
  h=_ctx->level[_l].h;
  col_sums_x=_ctx->col_buf;
  col_sums_y=col_sums_x+w;
  im1=_ctx->level[_l].im1;
  im2=_ctx->level[_l].im2;
  for(i=0;i<w;i++)col_sums_x[i]=5*im1[i];
  for(i=0;i<w;i++)col_sums_y[i]=5*im2[i];
  for(j=1;j<4;j++){
    j1offs=FS_MINI(j,h-1)*w;
    for(i=0;i<w;i++)col_sums_x[i]+=im1[j1offs+i];
    for(i=0;i<w;i++)col_sums_y[i]+=im2[j1offs+i];
  }
  ssim=_ctx->level[_l].ssim;
  samplemax = (1 << _ctx->depth) - 1;
  /*4096 is a normalization constant for the luminance term.
    See Section 3 of the FastSSIM paper, "The luminance term in Fast SSIM
     utilizes an 8x8 square window."
    mux and muy are the sum of 64 values: 64*64 == 4096.*/
  c1=(double)(samplemax*samplemax*SSIM_K1*4096*(1<<4*_l));
  for(j=0;j<h;j++){
    unsigned mux;
    unsigned muy;
    int      i0;
    int      i1;
    mux=5*col_sums_x[0];
    muy=5*col_sums_y[0];
    for(i=1;i<4;i++){
      i1=FS_MINI(i,w-1);
      mux+=col_sums_x[i1];
      muy+=col_sums_y[i1];
    }
    for(i=0;i<w;i++){
      ssim[j*w+i]*=(2*mux*(double)muy+c1)/(mux*(double)mux+muy*(double)muy+c1);
      if(i+1<w){
        i0=FS_MAXI(0,i-4);
        i1=FS_MINI(i+4,w-1);
        mux+=col_sums_x[i1]-col_sums_x[i0];
        muy+=col_sums_x[i1]-col_sums_x[i0];
      }
    }
    if(j+1<h){
      j0offs=FS_MAXI(0,j-4)*w;
      for(i=0;i<w;i++)col_sums_x[i]-=im1[j0offs+i];
      for(i=0;i<w;i++)col_sums_y[i]-=im2[j0offs+i];
      j1offs=FS_MINI(j+4,h-1)*w;
      for(i=0;i<w;i++)col_sums_x[i]+=im1[j1offs+i];
      for(i=0;i<w;i++)col_sums_y[i]+=im2[j1offs+i];
    }
  }
}

#define FS_COL_SET(_col,_joffs,_ioffs) \
  do{ \
    unsigned gx; \
    unsigned gy; \
    gx = gx_buf[((j + (_joffs)) & 7)*stride + i + (_ioffs)]; \
    gy = gy_buf[((j + (_joffs)) & 7)*stride + i + (_ioffs)]; \
    col_sums_gx2[(_col)]=gx*(double)gx; \
    col_sums_gy2[(_col)]=gy*(double)gy; \
    col_sums_gxgy[(_col)]=gx*(double)gy; \
  } \
  while(0)

#define FS_COL_ADD(_col,_joffs,_ioffs) \
  do{ \
    unsigned gx; \
    unsigned gy; \
    gx = gx_buf[((j + (_joffs)) & 7)*stride + i + (_ioffs)]; \
    gy = gy_buf[((j + (_joffs)) & 7)*stride + i + (_ioffs)]; \
    col_sums_gx2[(_col)]+=gx*(double)gx; \
    col_sums_gy2[(_col)]+=gy*(double)gy; \
    col_sums_gxgy[(_col)]+=gx*(double)gy; \
  } \
  while(0)

#define FS_COL_SUB(_col,_joffs,_ioffs) \
  do{ \
    unsigned gx; \
    unsigned gy; \
    gx = gx_buf[((j + (_joffs)) & 7)*stride + i + (_ioffs)]; \
    gy = gy_buf[((j + (_joffs)) & 7)*stride + i + (_ioffs)]; \
    col_sums_gx2[(_col)]-=gx*(double)gx; \
    col_sums_gy2[(_col)]-=gy*(double)gy; \
    col_sums_gxgy[(_col)]-=gx*(double)gy; \
  } \
  while(0)

#define FS_COL_COPY(_col1,_col2) \
  do{ \
    col_sums_gx2[(_col1)]=col_sums_gx2[(_col2)]; \
    col_sums_gy2[(_col1)]=col_sums_gy2[(_col2)]; \
    col_sums_gxgy[(_col1)]=col_sums_gxgy[(_col2)]; \
  } \
  while(0)

#define FS_COL_HALVE(_col1,_col2) \
  do{ \
    col_sums_gx2[(_col1)]=col_sums_gx2[(_col2)]*0.5; \
    col_sums_gy2[(_col1)]=col_sums_gy2[(_col2)]*0.5; \
    col_sums_gxgy[(_col1)]=col_sums_gxgy[(_col2)]*0.5; \
  } \
  while(0)

#define FS_COL_DOUBLE(_col1,_col2) \
  do{ \
    col_sums_gx2[(_col1)]=col_sums_gx2[(_col2)]*2; \
    col_sums_gy2[(_col1)]=col_sums_gy2[(_col2)]*2; \
    col_sums_gxgy[(_col1)]=col_sums_gxgy[(_col2)]*2; \
  } \
  while(0)

static void fs_calc_structure(fs_ctx *_ctx,int _l){
  int32_t *im1;
  int32_t *im2;
  unsigned     *gx_buf;
  unsigned     *gy_buf;
  double       *ssim;
  double        col_sums_gx2[8];
  double        col_sums_gy2[8];
  double        col_sums_gxgy[8];
  double        c2;
  int           stride;
  int           w;
  int           h;
  int           i;
  int           j;
  int samplemax;
  w=_ctx->level[_l].w;
  h=_ctx->level[_l].h;
  im1=_ctx->level[_l].im1;
  im2=_ctx->level[_l].im2;
  ssim=_ctx->level[_l].ssim;
  gx_buf=_ctx->col_buf;
  stride=w+8;
  gy_buf=gx_buf+8*stride;
  memset(gx_buf,0,2*8*stride*sizeof(*gx_buf));
  samplemax = (1 << _ctx->depth) - 1;
  /*104 is the sum of the "8x8 integer approximation to Gaussian window" in
     Fig. 3 of the FastSSIM paper.*/
  c2=samplemax*samplemax*SSIM_K2*(1<<4*_l)*16*104;
  for(j=0;j<h+4;j++){
    if(j<h-1){
      for(i=0;i<w-1;i++){
        unsigned g1;
        unsigned g2;
        unsigned gx;
        unsigned gy;
        g1=abs(im1[(j+1)*w+i+1]-im1[j*w+i]);
        g2=abs(im1[(j+1)*w+i]-im1[j*w+i+1]);
        gx=4*FS_MAXI(g1,g2)+FS_MINI(g1,g2);
        g1=abs(im2[(j+1)*w+i+1]-im2[j*w+i]);
        g2=abs(im2[(j+1)*w+i]-im2[j*w+i+1]);
        gy=4*FS_MAXI(g1,g2)+FS_MINI(g1,g2);
        gx_buf[(j&7)*stride+i+4]=gx;
        gy_buf[(j&7)*stride+i+4]=gy;
      }
    }
    else{
      memset(gx_buf+(j&7)*stride,0,stride*sizeof(*gx_buf));
      memset(gy_buf+(j&7)*stride,0,stride*sizeof(*gy_buf));
    }
    if(j>=4){
      int k;
      col_sums_gx2[3]=col_sums_gx2[2]=col_sums_gx2[1]=col_sums_gx2[0]=0;
      col_sums_gy2[3]=col_sums_gy2[2]=col_sums_gy2[1]=col_sums_gy2[0]=0;
      col_sums_gxgy[3]=col_sums_gxgy[2]=col_sums_gxgy[1]=col_sums_gxgy[0]=0;
      for(i=4;i<8;i++){
        FS_COL_SET(i,-1,0);
        FS_COL_ADD(i,0,0);
        for(k=1;k<8-i;k++){
          FS_COL_DOUBLE(i,i);
          FS_COL_ADD(i,-k-1,0);
          FS_COL_ADD(i,k,0);
        }
      }
      for(i=0;i<w;i++){
        double   mugx2;
        double   mugy2;
        double   mugxgy;
        mugx2=col_sums_gx2[0];
        for(k=1;k<8;k++)mugx2+=col_sums_gx2[k];
        mugy2=col_sums_gy2[0];
        for(k=1;k<8;k++)mugy2+=col_sums_gy2[k];
        mugxgy=col_sums_gxgy[0];
        for(k=1;k<8;k++)mugxgy+=col_sums_gxgy[k];
        ssim[(j-4)*w+i]=(2*mugxgy+c2)/(mugx2+mugy2+c2);
        if(i+1<w){
          FS_COL_SET(0,-1,1);
          FS_COL_ADD(0,0,1);
          FS_COL_SUB(2,-3,2);
          FS_COL_SUB(2,2,2);
          FS_COL_HALVE(1,2);
          FS_COL_SUB(3,-4,3);
          FS_COL_SUB(3,3,3);
          FS_COL_HALVE(2,3);
          FS_COL_COPY(3,4);
          FS_COL_DOUBLE(4,5);
          FS_COL_ADD(4,-4,5);
          FS_COL_ADD(4,3,5);
          FS_COL_DOUBLE(5,6);
          FS_COL_ADD(5,-3,6);
          FS_COL_ADD(5,2,6);
          FS_COL_DOUBLE(6,7);
          FS_COL_ADD(6,-2,7);
          FS_COL_ADD(6,1,7);
          FS_COL_SET(7,-1,8);
          FS_COL_ADD(7,0,8);
        }
      }
    }
  }
}

#define FS_NLEVELS (4)

/*These weights were derived from the default weights found in Wang's original
   Matlab implementation: {0.0448, 0.2856, 0.2363, 0.1333}.
  We drop the finest scale and renormalize the rest to sum to 1.*/

static const double FS_WEIGHTS[FS_NLEVELS]={
  0.2989654541015625,0.3141326904296875,0.2473602294921875,0.1395416259765625
};

static double fs_average(fs_ctx *_ctx,int _l){
  double *ssim;
  double  ret;
  int     w;
  int     h;
  int     i;
  int     j;
  w=_ctx->level[_l].w;
  h=_ctx->level[_l].h;
  ssim=_ctx->level[_l].ssim;
  ret=0;
  for(j=0;j<h;j++)for(i=0;i<w;i++)ret+=ssim[j*w+i];
  return pow(ret/(w*h),FS_WEIGHTS[_l]);
}

double calc_ssim(const unsigned char *_src,int _systride,
 const unsigned char *_dst,int _dystride,int depth,int _w,int _h){
  fs_ctx ctx;
  double ret;
  int    l;
  ret=1;
  fs_ctx_init(&ctx,_w,_h,depth,FS_NLEVELS);
  fs_downsample_level0(&ctx,_src,_systride,_dst,_dystride,_w,_h, depth);
  for(l=0;l<FS_NLEVELS-1;l++){
    fs_calc_structure(&ctx,l);
    ret*=fs_average(&ctx,l);
    fs_downsample_level(&ctx,l+1);
  }
  fs_calc_structure(&ctx,l);
  fs_apply_luminance(&ctx,l);
  ret*=fs_average(&ctx,l);
  fs_ctx_clear(&ctx);
  return ret;
}

typedef double (*convert_ssim_func)(double _ssim,double _weight);

/*
static double convert_ssim_raw(double _ssim,double _weight){
  return _ssim/_weight;
}
*/

static double convert_ssim_db(double _ssim,double _weight){
  return 10*(log10(_weight)-log10(_weight-_ssim));
}

int fast_ssim(const char * video_path1, const char * video_path2, FILE* file){
  video_input        vid1;
  video_input_info   info1;
  video_input        vid2;
  video_input_info   info2;
  convert_ssim_func  convert;
  double             gssim[3];
  double             cweight;
  int                frameno;
  FILE              *fin;
#ifdef _WIN32
  /*We need to set stdin/stdout to binary mode on windows.
    Beware the evil ifdef.
    We avoid these where we can, but this one we cannot.
    Don't add any more, you'll probably go to hell if you do.*/
  _setmode(_fileno(stdin),_O_BINARY);
#endif
  /*Process option arguments.*/
  convert=convert_ssim_db;
  fin=strcmp(video_path1,"-")==0?stdin:fopen(video_path1,"rb");
  if(fin==NULL){
    fprintf(stderr,"Unable to open '%s' for extraction.\n", video_path1);
    exit(EXIT_FAILURE);
  }
  fprintf(stderr,"Opening %s...\n",video_path1);
  if(video_input_open(&vid1,fin)<0)exit(EXIT_FAILURE);
  video_input_get_info(&vid1,&info1);
  fin=strcmp(video_path2,"-")==0?stdin:fopen(video_path2,"rb");
  if(fin==NULL){
    fprintf(stderr,"Unable to open '%s' for extraction.\n",video_path2);
    exit(EXIT_FAILURE);
  }
  fprintf(stderr,"Opening %s...\n", video_path2);
  if(video_input_open(&vid2,fin)<0)exit(EXIT_FAILURE);
  video_input_get_info(&vid2,&info2);
  /*Check to make sure these videos are compatible.*/
  if(info1.pic_w!=info2.pic_w||info1.pic_h!=info2.pic_h){
    fprintf(stderr,"Video resolution does not match.\n");
    exit(EXIT_FAILURE);
  }
  if(info1.pixel_fmt!=info2.pixel_fmt){
    fprintf(stderr,"Pixel formats do not match.\n");
    exit(EXIT_FAILURE);
  }
  if((info1.pic_x&!(info1.pixel_fmt&1))!=(info2.pic_x&!(info2.pixel_fmt&1))||
   (info1.pic_y&!(info1.pixel_fmt&2))!=(info2.pic_y&!(info2.pixel_fmt&2))){
    fprintf(stderr,"Chroma subsampling offsets do not match.\n");
    exit(EXIT_FAILURE);
  }
  if(info1.depth!=info2.depth){
    fprintf(stderr,"Depth does not match.\n");
    exit(EXIT_FAILURE);
  }
  if(info1.depth > 16){
    fprintf(stderr,"Sample depths above 16 are not supported.\n");
    exit(EXIT_FAILURE);
  }
  if(info1.fps_n*(int64_t)info2.fps_d!=
   info2.fps_n*(int64_t)info1.fps_d){
    fprintf(stderr,"Warning: framerates do not match.\n");
  }
  if(info1.par_n*(int64_t)info2.par_d!=
   info2.par_n*(int64_t)info1.par_d){
    fprintf(stderr,"Warning: aspect ratios do not match.\n");
  }
  gssim[0]=gssim[1]=gssim[2]=0;
  /*We just use a simple weighting to get a single full-color score.
    In reality the CSF for chroma is not the same as luma.*/
  cweight = 0.25*(4 >> (!(info1.pixel_fmt & 1) + !(info1.pixel_fmt & 2)));
  for(frameno=0;;frameno++){
    video_input_ycbcr f1;
    video_input_ycbcr f2;
    double          ssim[3];
    char            tag1[5];
    char            tag2[5];
    int             ret1;
    int             ret2;
    int             pli;
    int             nplanes;
    ret1=video_input_fetch_frame(&vid1,f1,tag1);
    ret2=video_input_fetch_frame(&vid2,f2,tag2);
    if(ret1==0&&ret2==0)break;
    else if(ret1<0||ret2<0)break;
    else if(ret1==0){
      fprintf(stderr,"%s ended before %s.\n",
       video_path1, video_path2);
      break;
    }
    else if(ret2==0){
      fprintf(stderr,"%s ended before %s.\n",
       video_path2,video_path1);
      break;
    }
    /*Okay, we got one frame from each.*/
    nplanes = show_chroma ? 3 : 1;
    for(pli=0;pli<nplanes;pli++){
      int xdec;
      int ydec;
      int xstride;
      xdec=pli&&!(info1.pixel_fmt&1);
      ydec=pli&&!(info1.pixel_fmt&2);
      xstride = info1.depth > 8 ? 2 : 1;
      ssim[pli]=calc_ssim(
       f1[pli].data+(info1.pic_y>>ydec)*f1[pli].stride +
        (info1.pic_x>>xdec)*xstride,
       f1[pli].stride,
       f2[pli].data+(info2.pic_y>>ydec)*f2[pli].stride +
        (info2.pic_x>>xdec)*xstride,
       f2[pli].stride,
       info1.depth,
       ((info1.pic_x + info1.pic_w + xdec) >> xdec) - (info1.pic_x >> xdec),
       ((info1.pic_y + info1.pic_h + ydec) >> ydec) - (info1.pic_y >> ydec));
      gssim[pli]+=ssim[pli];
    }
    if(!summary_only){
      if(show_frame_type)printf("%s%s",tag1,tag2);
      if(show_chroma){
        fprintf(file, "%08i: %-8G  (Y': %-8G  Cb: %-8G  Cr: %-8G)\n",frameno,
         convert(ssim[0]+cweight*(ssim[1]+ssim[2]),1+2*cweight),
         convert(ssim[0],1),convert(ssim[1],1),convert(ssim[2],1));
      }
      else fprintf(file, "%08i: %-8G\n",frameno,convert(ssim[0],1));
    }
  }
  if(show_chroma){
     fprintf(file, "Total: %-8G  (Y': %-8G  Cb: %-8G  Cr: %-8G)\n",
     convert(gssim[0]+cweight*(gssim[1]+gssim[2]),(1+2*cweight)*frameno),
     convert(gssim[0],frameno),convert(gssim[1],frameno),
     convert(gssim[2],frameno));
  }
  else fprintf(file, "Total: %-8G\n",convert(gssim[0],frameno));
  video_input_close(&vid1);
  video_input_close(&vid2);
  return EXIT_SUCCESS;
}
