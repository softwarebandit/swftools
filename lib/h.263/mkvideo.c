/* mkvideo.c
   Create a video file.

   Part of the swftools package.

   Copyright (c) 2003 Matthias Kramm <kramm@quiss.org> */

#include <stdlib.h>
#include <stdio.h>
#include <assert.h>
#include <math.h>
#include "../rfxswf.h"
#include "h263tables.c"
#include "swfvideo.h"

#ifdef MAIN
U16 totalframes = 0;
#endif
void swf_SetVideoStreamDefine(TAG*tag, VIDEOSTREAM*stream, U16 frames, U16 width, U16 height)
{
    swf_SetU16(tag, frames);
    swf_SetU16(tag, width);
    swf_SetU16(tag, height);
    //swf_SetU8(tag, 1); /* smoothing on */
    swf_SetU8(tag, 0); /* smoothing off */
    swf_SetU8(tag, 2); /* codec = h.263 sorenson spark */

    totalframes = frames;

    memset(stream, 0, sizeof(VIDEOSTREAM));
    stream->olinex = width;
    width+=15;width&=~15;
    height+=15;height&=~15;
    stream->linex = width;
    stream->width = width;
    stream->height = height;
    stream->bbx = width/16;
    stream->bby = height/16;
    stream->current = (YUV*)malloc(width*height*sizeof(YUV));
    stream->oldpic = (YUV*)malloc(width*height*sizeof(YUV));
    stream->mvdx = (int*)malloc(stream->bbx*stream->bby*sizeof(int));
    stream->mvdy = (int*)malloc(stream->bbx*stream->bby*sizeof(int));
    stream->do_motion = 0;

    memset(stream->oldpic, 0, width*height*sizeof(YUV));
    memset(stream->current, 0, width*height*sizeof(YUV));
}
void swf_VideoStreamClear(VIDEOSTREAM*stream)
{
    free(stream->oldpic);stream->oldpic = 0;
    free(stream->current);stream->current = 0;
    free(stream->mvdx);stream->mvdx=0;
    free(stream->mvdy);stream->mvdy=0;
}

typedef struct _block_t
{
    int y1[64];
    int y2[64];
    int y3[64];
    int y4[64];
    int u[64];
    int v[64];
} block_t;

static int zigzagtable[64] = {
    0, 1, 5, 6, 14, 15, 27, 28,
    2, 4, 7, 13, 16, 26, 29, 42,
    3, 8, 12, 17, 25, 30, 41, 43,
    9, 11, 18, 24, 31, 40, 44, 53,
    10, 19, 23, 32, 39, 45, 52, 54,
    20, 22, 33, 38, 46, 51, 55, 60,
    21, 34, 37, 47, 50, 56, 59, 61,
    35, 36, 48, 49, 57, 58, 62, 63};

static void zigzag(int*src)
{
    int tmp[64];
    int t;
    for(t=0;t<64;t++) {
	tmp[zigzagtable[t]] = src[t];
    }
    memcpy(src, tmp, sizeof(int)*64);
}

#define PI 3.14159265358979
#define SQRT2 1.414214
#define RSQRT2 (1.0/1.414214)

static double table[8][8] =
{
{0.707106781186548,0.707106781186548,0.707106781186548,0.707106781186548,0.707106781186548,0.707106781186548,0.707106781186548,0.707106781186548},
{0.980785280403230,0.831469612302545,0.555570233019602,0.195090322016128,-0.195090322016128,-0.555570233019602,-0.831469612302545,-0.980785280403230},
{0.923879532511287,0.382683432365090,-0.382683432365090,-0.923879532511287,-0.923879532511287,-0.382683432365090,0.382683432365090,0.923879532511287},
{0.831469612302545,-0.195090322016128,-0.980785280403230,-0.555570233019602,0.555570233019602,0.980785280403230,0.195090322016129,-0.831469612302545},
{0.707106781186548,-0.707106781186547,-0.707106781186548,0.707106781186547,0.707106781186548,-0.707106781186547,-0.707106781186547,0.707106781186547},
{0.555570233019602,-0.980785280403230,0.195090322016128,0.831469612302545,-0.831469612302545,-0.195090322016128,0.980785280403231,-0.555570233019602},
{0.382683432365090,-0.923879532511287,0.923879532511287,-0.382683432365090,-0.382683432365091,0.923879532511287,-0.923879532511286,0.382683432365090},
{0.195090322016128,-0.555570233019602,0.831469612302545,-0.980785280403231,0.980785280403230,-0.831469612302545,0.555570233019602,-0.195090322016129}
};

static void dct(int*src)
{
    double tmp[64];
    int x,y,u,v,t;

    for(v=0;v<8;v++)
    for(u=0;u<8;u++)
    {
	double c = 0;
	for(x=0;x<8;x++)
	{
	    c+=table[u][x]*src[v*8+x];
	}
	tmp[v*8+u] = c;
    }
    for(u=0;u<8;u++)
    for(v=0;v<8;v++)
    {
	double c = 0;
	for(y=0;y<8;y++)
	{
	    c+=table[v][y]*tmp[y*8+u];
	}
	src[v*8+u] = (int)(c*0.25+0.5);
    }
}

static void idct(int*src)
{
    double tmp[64];
    int x,y,u,v;
    for(y=0;y<8;y++)
    for(x=0;x<8;x++)
    {
	double c = 0;
	for(u=0;u<8;u++)
	{
	    c+=table[u][x]*src[y*8+u];
	}
	tmp[y*8+x] = c;
    }
    for(y=0;y<8;y++)
    for(x=0;x<8;x++)
    {
	double c = 0;
	for(v=0;v<8;v++)
	{
	    c+=table[v][y]*tmp[v*8+x];
	}
	src[y*8+x] = (int)(c*0.25+0.5);
    }
}

static double c[8] = {1.0,
0.980785280403230, // cos(Pi*1/16), sin(Pi*7/16)
0.923879532511287, // cos(Pi*2/16), sin(Pi*6/16)
0.831469612302545, // cos(Pi*3/16), sin(Pi*5/16)
0.707106781186548, // cos(Pi*4/16), sin(Pi*4/16), 1/sqrt(2)
0.555570233019602, // cos(Pi*5/16), sin(Pi*3/16)
0.382683432365090, // cos(Pi*6/16), sin(Pi*2/16)
0.195090322016128 // cos(Pi*7/16), sin(Pi*1/16)
};

static double cc[8];
int ccquant = -1;

static void preparequant(int quant)
{
    if(ccquant == quant)
	return;
    cc[0] = c[0]/(quant*2*4);
    cc[1] = c[1]/(quant*2*4);
    cc[2] = c[2]/(quant*2*4);
    cc[3] = c[3]/(quant*2*4);
    cc[4] = c[4]/(quant*2*4);
    cc[5] = c[5]/(quant*2*4);
    cc[6] = c[6]/(quant*2*4);
    cc[7] = c[7]/(quant*2*4);
    ccquant = quant;
}

inline static void innerdct(double*a,double*b, double*c)
{
    // c1*c7*2 = c6
    // c2*c6*2 = c4
    // c3*c5*2 = c2
    // c4*c4*2 = 1

     //{  1,  3,  5,  7, -7, -5, -3, -1},
     //{  3, -7, -1, -5,  5,  1,  7, -3},
     //{  5, -1,  7,  3, -3, -7,  1, -5},
     //{  7, -5,  3, -1,  1, -3,  5, -7}
    double b0,b1,b2,b3,b4,b5;
    b2 = (a[0]+a[7]);
    b3 = (a[1]+a[6]);
    b4 = (a[2]+a[5]);
    b5 = (a[3]+a[4]);

    b0 = (b2+b5)*c[4];
    b1 = (b3+b4)*c[4];
    b[0*8] = b0 + b1;
    b[4*8] = b0 - b1;
    b[2*8] = (b2-b5)*c[2] + (b3-b4)*c[6];
    b[6*8] = (b2-b5)*c[6] + (b4-b3)*c[2];

    b0 = (a[0]-a[7]);
    b1 = (a[1]-a[6]);
    b2 = (a[2]-a[5]);
    b3 = (a[3]-a[4]);

    b[1*8] = b0*c[1] + b1*c[3] + b2*c[5] + b3*c[7];
    b[3*8] = b0*c[3] - b1*c[7] - b2*c[1] - b3*c[5];
    b[5*8] = b0*c[5] - b1*c[1] + b2*c[7] + b3*c[3];
    b[7*8] = b0*c[7] - b1*c[5] + b2*c[3] - b3*c[1];
}

static void dct2(int*src, int*dest)
{
    double tmp[64], tmp2[64];
    double*p;
    int u,x,v,t;

    for(t=0;t<64;t++)
	tmp2[t] = src[t];

    for(v=0;v<8;v++)
    {
	double* a=&tmp2[v*8];
	double* b=&tmp[v];
	innerdct(a,b,c);
    }
    for(v=0;v<8;v++)
    {
	double* a=&tmp[v*8];
	double* b=&tmp2[v];
	innerdct(a,b,cc);
    }
    for(t=0;t<64;t++) {
	dest[zigzagtable[t]] = (int)(tmp2[t]);
    }
}


static inline int truncate256(int a)
{
    if(a>255) return 255;
    if(a<0) return 0;
    return a;
}

static void getregion(block_t* bb, YUV*pic, int posx, int posy, int linex)
{
    YUV*p1;
    YUV*p2;
    int y1=0, y2=0, y3=0, y4=0;
    int u=0,v=0;
    int x,y;
    posx*=16;
    posy*=16;
    p1 = &pic[posy*linex+posx];
    p2 = p1;
    for(y=0;y<8;y++) {
	for(x=0;x<8;x++) {
	    bb->u[u++] = (p2[x*2].u + p2[x*2+1].u + p2[linex+x*2].u + p2[linex+x*2+1].u)/4;
	    bb->v[v++] = (p2[x*2].v + p2[x*2+1].v + p2[linex+x*2].v + p2[linex+x*2+1].v)/4;
	    bb->y1[y1++] = p1[x].y;
	    bb->y2[y2++] = p1[x+8].y;
	    bb->y3[y3++] = p1[linex*8+x].y;
	    bb->y4[y4++] = p1[linex*8+x+8].y;
	}
	p1+=linex;
	p2+=linex*2;
    }
}

static void getmvdregion(block_t* bb, YUV*pic, int posx, int posy, int mvdx, int mvdy, int linex)
{
    YUV*p1;
    YUV*p2;
    int yy=0,uv=0;
    int x,y;
    int yhp = 0, uvhp=0;
    int uvposx, uvposy;
    posx = posx*16 + ((mvdx&~1)/2);
    posy = posy*16 + ((mvdy&~1)/2);
    p1 = &pic[posy*linex+posx];
    p2 = &pic[(posy&~1)*linex+(posx&~1)];
    uvhp = ((mvdx&1)|((mvdx>>1)&1))|((mvdy&2)|((mvdy&1)<<1));
    yhp = ((mvdy&1)<<1|(mvdx&1));

    /* y */
    if(yhp==0 || yhp==2) {
	for(y=0;y<8;y++) {
	    for(x=0;x<8;x++) {
		bb->y1[yy] = p1[x].y;
		bb->y2[yy] = p1[x+8].y;
		bb->y3[yy] = p1[linex*8+x].y;
		bb->y4[yy] = p1[linex*8+x+8].y;
		yy++;
	    }
	    p1+=linex;

	    if(yhp==2) {
		yy-=8;
		for(x=0;x<8;x++) {
		    bb->y1[yy] += p1[x].y; bb->y1[yy] /= 2;
		    bb->y2[yy] += p1[x+8].y; bb->y2[yy] /= 2;
		    bb->y3[yy] += p1[linex*8+x].y; bb->y3[yy] /= 2;
		    bb->y4[yy] += p1[linex*8+x+8].y; bb->y4[yy] /= 2;
		    yy++;
		}
	    }
	}
    } else if(yhp==1 || yhp==3) {
	for(y=0;y<8;y++) {
	    for(x=0;x<8;x++) {
		bb->y1[yy] = (p1[x].y + p1[x+1].y);
		bb->y2[yy] = (p1[x+8].y + p1[x+8+1].y);
		bb->y3[yy] = (p1[linex*8+x].y + p1[linex*8+x+1].y);
		bb->y4[yy] = (p1[linex*8+x+8].y + p1[linex*8+x+8+1].y);
		yy++;
	    }
	    yy-=8;
	    p1+=linex;
	    if(yhp==3) {
		for(x=0;x<8;x++) {
		    bb->y1[yy] += (p1[x].y + p1[x+1].y); bb->y1[yy]/=4;
		    bb->y2[yy] += (p1[x+8].y + p1[x+8+1].y); bb->y2[yy]/=4;
		    bb->y3[yy] += (p1[linex*8+x].y + p1[linex*8+x+1].y); bb->y3[yy]/=4;
		    bb->y4[yy] += (p1[linex*8+x+8].y + p1[linex*8+x+8+1].y); bb->y4[yy]/=4;
		    yy++;
		}
	    } else {
		for(x=0;x<8;x++) {
		    bb->y1[yy]/=2; bb->y2[yy]/=2; bb->y3[yy]/=2; bb->y4[yy]/=2;
		    yy++;
		}
	    }
	}
    }

    /* u,v */
    if(uvhp==0 || uvhp==2) {
	for(y=0;y<8;y++) {
	    for(x=0;x<8;x++) {
		bb->u[uv] = (p2[x*2].u + p2[x*2+1].u + p2[linex+x*2].u + p2[linex+x*2+1].u)/4;
		bb->v[uv] = (p2[x*2].v + p2[x*2+1].v + p2[linex+x*2].v + p2[linex+x*2+1].v)/4;
		uv++;
	    }
	    p2+=linex*2;
	    if(uvhp==2) {
		uv-=8;
		for(x=0;x<8;x++) {
		    bb->u[uv] += (p2[x*2].u + p2[x*2+1].u + p2[linex+x*2].u + p2[linex+x*2+1].u)/4;
		    bb->v[uv] += (p2[x*2].v + p2[x*2+1].v + p2[linex+x*2].v + p2[linex+x*2+1].v)/4;
		    bb->u[uv] /= 2;
		    bb->v[uv] /= 2;
		    uv++;
		}
	    }
	}
    } else /* uvhp==1 || uvhp==3 */ {
	for(y=0;y<8;y++) {
	    for(x=0;x<8;x++) {
		bb->u[uv] = ((p2[x*2].u + p2[x*2+1].u + p2[linex+x*2].u + p2[linex+x*2+1].u)/4+
		             (p2[x*2+2].u + p2[x*2+1+2].u + p2[linex+x*2+2].u + p2[linex+x*2+1+2].u)/4);
		bb->v[uv] = ((p2[x*2].v + p2[x*2+1].v + p2[linex+x*2].v + p2[linex+x*2+1].v)/4+
			     (p2[x*2+2].v + p2[x*2+1+2].v + p2[linex+x*2+2].v + p2[linex+x*2+1+2].v)/4);
		uv++;
	    }
	    uv-=8;
	    p2+=linex*2;
	    if(uvhp==3) {
		for(x=0;x<8;x++) {
		    bb->u[uv] += ((p2[x*2].u + p2[x*2+1].u + p2[linex+x*2].u + p2[linex+x*2+1].u)/4+
			 	  (p2[x*2+2].u + p2[x*2+1+2].u + p2[linex+x*2+2].u + p2[linex+x*2+1+2].u)/4);
		    bb->v[uv] += ((p2[x*2].v + p2[x*2+1].v + p2[linex+x*2].v + p2[linex+x*2+1].v)/4+
				  (p2[x*2+2].v + p2[x*2+1+2].v + p2[linex+x*2+2].v + p2[linex+x*2+1+2].v)/4);
		    bb->u[uv] /= 4;
		    bb->v[uv] /= 4;
		    uv++;
		}
	    } else {
		for(x=0;x<8;x++) {
		    bb->u[uv] /= 2;
		    bb->v[uv] /= 2;
		    uv++;
		}
	    }
	}
    }
}

static void rgb2yuv(YUV*dest, RGBA*src, int dlinex, int slinex, int width, int height)
{
    int x,y;
    for(y=0;y<height;y++) {
	for(x=0;x<width;x++) {
	    int r,g,b;
	    r = src[y*slinex+x].r;
	    g = src[y*slinex+x].g;
	    b = src[y*slinex+x].b;
	    /*dest[y*dlinex+x].y = (r*0.299 + g*0.587 + b*0.114);
	    dest[y*dlinex+x].u = (r*-0.169 + g*-0.332 + b*0.500 + 128.0);
	    dest[y*dlinex+x].v = (r*0.500 + g*-0.419 + b*-0.0813 + 128.0);*/

	    //dest[y*dlinex+x].y = 128;//(r*((int)( 0.299*256)) + g*((int)( 0.587*256)) + b*((int)( 0.114 *256)))>>8;
	    dest[y*dlinex+x].y = (r*((int)( 0.299*256)) + g*((int)( 0.587*256)) + b*((int)( 0.114 *256)))>>8;
	    dest[y*dlinex+x].u = (r*((int)(-0.169*256)) + g*((int)(-0.332*256)) + b*((int)( 0.500 *256))+ 128*256)>>8;
	    dest[y*dlinex+x].v = (r*((int)( 0.500*256)) + g*((int)(-0.419*256)) + b*((int)(-0.0813*256))+ 128*256)>>8;
	}
    }
}
static void copyregion(VIDEOSTREAM*s, YUV*dest, YUV*src, int bx, int by)
{
    YUV*p1 = &src[by*s->linex*16+bx*16];
    YUV*p2 = &dest[by*s->linex*16+bx*16];
    int y;
    for(y=0;y<16;y++) {
	memcpy(p1, p2, 16*sizeof(YUV));
	p1+=s->linex;p2+=s->linex;
    }
}

static void yuv2rgb(RGBA*dest, YUV*src, int linex, int width, int height)
{
    int x,y;
    for(y=0;y<height;y++) {
	for(x=0;x<width;x++) {
	    int u,v,yy;
	    u = src[y*linex+x].u;
	    v = src[y*linex+x].v;
	    yy = src[y*linex+x].y;
	    dest[y*linex+x].r = truncate256(yy + ((360*(v-128))>>8));
	    dest[y*linex+x].g = truncate256(yy - ((88*(u-128)+183*(v-128))>>8));
	    dest[y*linex+x].b = truncate256(yy + ((455 * (u-128))>>8));
	}
    }
}
static void copyblock(VIDEOSTREAM*s, YUV*dest, block_t*b, int bx, int by)
{
    YUV*p1 = &dest[(by*16)*s->linex+bx*16];
    YUV*p2 = &dest[(by*16+8)*s->linex+bx*16];
    int x,y;
    for(y=0;y<8;y++) {
	for(x=0;x<8;x++) {
	    int u,v,yy;
	    p1[x+0].u = b->u[(y/2)*8+(x/2)];
	    p1[x+0].v = b->v[(y/2)*8+(x/2)];
	    p1[x+0].y = b->y1[y*8+x];
	    p1[x+8].u = b->u[(y/2)*8+(x/2)+4];
	    p1[x+8].v = b->v[(y/2)*8+(x/2)+4];
	    p1[x+8].y = b->y2[y*8+x];
	    p2[x+0].u = b->u[(y/2+4)*8+(x/2)];
	    p2[x+0].v = b->v[(y/2+4)*8+(x/2)];
	    p2[x+0].y = b->y3[y*8+x];
	    p2[x+8].u = b->u[(y/2+4)*8+(x/2)+4];
	    p2[x+8].v = b->v[(y/2+4)*8+(x/2)+4];
	    p2[x+8].y = b->y4[y*8+x];
	}
	p1+=s->linex;
	p2+=s->linex;
    }
}

static int compareregions(VIDEOSTREAM*s, int bx, int by)
{
    int linex = s->width;
    YUV*p1 = &s->current[by*linex*16+bx*16];
    YUV*p2 = &s->oldpic[by*linex*16+bx*16];
    int diff = 0;
    int x,y;
    for(y=0;y<16;y++) {
	for(x=0;x<16;x++) {
	    YUV*m = &p1[x];
	    YUV*n = &p2[x];
	    int y = m->y - n->y;
	    int u = m->u - n->u;
	    int v = m->v - n->v;
	    diff += y*y+(u*u+v*v)/4;
	}
	p1+=linex;
	p2+=linex;
    }
    return diff/256;
}

static inline int valtodc(int val)
{
    assert(val>=0);

    /* table 12/h.263 */

    //val+=4; //round
    val/=8;
    /* TODO: what to do for zero values? skip the block? */
    if(val==0)
	return 1;
    if(val==128)
	return 255;
    if(val>254)
	return 254;
    return val;
}
static int dctoval(int dc)
{
    int val;
    assert(dc>0);
    assert(dc!=128);
    assert(dc<256);
    /* table 12/h.263 */
    val = dc*8;
    if(val == 255*8)
	val = 128*8;
    return val;
}

static int codehuffman(TAG*tag, struct huffcode*table, int index)
{
    /* TODO: !optimize! */
    int i=0;
    while(table[index].code[i]) {
	if(table[index].code[i]=='0')
	    swf_SetBits(tag, 0, 1);
	else
	    swf_SetBits(tag, 1, 1);
	i++;
    }
    return i;
}

static void quantize8x8(int*src, int*dest, int has_dc, int quant)
{
    int t,pos=0;
    double q = 1.0/(quant*2);
    if(has_dc) {
	dest[0] = valtodc((int)src[0]); /*DC*/
	pos++;
    }
    for(t=pos;t<64;t++)
    {
	//dest[t] = (int)src[t];
    /* exact: if(quant&1){dest[t] = (dest[t]/quant - 1)/2;}else{dest[t] = ((dest[t]+1)/quant - 1)/2;} */
        //if(quant&1){dest[t] = (dest[t]/quant - 1)/2;}else{dest[t] = ((dest[t]+1)/quant - 1)/2;}
	//dest[t] = dest[t]/(quant*2);
	dest[t] = (int)(src[t]*q);
	/* TODO: warn if this happens- the video will be buggy */
	if(dest[t]>127) dest[t]=127;
	if(dest[t]<-127) dest[t]=-127;
    }
}

static void dequantize8x8(int*b, int has_dc, int quant)
{
    int t,pos=0;
    if(has_dc) {
	b[0] = dctoval(b[0]); //DC
	pos++;
    }
    for(t=pos;t<64;t++) {
	if(b[t]) {
	    int sign = 0;
	    if(b[t]<0) {
		b[t] = -b[t];
		sign = 1;
	    }

	    if(quant&1) {
		b[t] = quant*(2*b[t]+1); //-7,8,24,40
	    } else {
		b[t] = quant*(2*b[t]+1)-1; //-8,7,23,39
	    }

	    if(sign)
		b[t] = -b[t];
	}

	/* paragraph 6.2.2, "clipping of reconstruction levels": */
	if(b[t]>2047) b[t]=2047;
	if(b[t]<-2048) b[t]=-2048;
    }
}

static int hascoef(int*b, int has_dc)
{
    int t;
    int pos=0;
    if(has_dc)
	pos++;
    for(t=pos;t<64;t++) {
	if(b[t])
	    return 1;
    }
    return 0;
}

static int coefbits8x8(int*bb, int has_dc)
{
    int t;
    int pos=0;
    int bits=0;
    int last;

    if(has_dc) {
	bits+=8;
	pos++;
    }
    for(last=63;last>=pos;last--) {
	if(bb[last])
	    break;
    }
    if(last < pos)
	return bits;
    while(1) {
	int run=0, level=0, islast=0,t;
	while(!bb[pos] && pos<last) {
	    pos++;
	    run++;
	}
	if(pos==last)
	    islast=1;
	level=bb[pos];
	if(level<0) level=-level;
	assert(level);
	for(t=0;t<RLE_ESCAPE;t++) {
	    if(rle_params[t].run == run &&
	       rle_params[t].level == level &&
	       rle_params[t].last == islast) {
		bits += rle[t].len + 1;
		break;
	    }
	}
	if(t==RLE_ESCAPE) {
	    bits += rle[RLE_ESCAPE].len + 1 + 6 + 8;
	}
	if(islast)
	    break;
	pos++;
    }
    return bits;
}

static void encode8x8(TAG*tag, int*bb, int has_dc, int has_tcoef)
{
    int t;
    int pos=0;
    int bits=0;

    if(has_dc) {
	swf_SetBits(tag, bb[0], 8);
	pos++;
    }

    if(has_tcoef) {
	int last;
	/* determine last non-null coefficient */
	for(last=63;last>=pos;last--) {
	    /* TODO: we could leave out small coefficients
	             after a certain point (32?) */
	    if(bb[last])
		break;
	}
	/* blocks without coefficients should not be included
	   in the cbpy/cbpc patterns: */
	assert(bb[last]);

	while(1) {
	    int run=0;
	    int level=0;
	    int islast=0;
	    int sign=0;
	    int t;
	    while(!bb[pos] && pos<last) {
		pos++;
		run++;
	    }
	    if(pos==last)
		islast=1;
	    level=bb[pos];
	    assert(level);
	    if(level<0) {
		level = -level;
		sign = 1;
	    }
	    for(t=0;t<RLE_ESCAPE;t++) {
		/* TODO: lookup table */
		if(rle_params[t].run == run &&
		   rle_params[t].level == level &&
		   rle_params[t].last == islast) {
		    codehuffman(tag, rle, t);
		    swf_SetBits(tag, sign, 1);
		    break;
		}
	    }
	    if(t==RLE_ESCAPE) {
		codehuffman(tag, rle, RLE_ESCAPE);
		level=bb[pos];
		/* table 14/h.263 */
		assert(level);
		assert(level>=-127);
		assert(level<=127);

		swf_SetBits(tag, islast, 1);
		swf_SetBits(tag, run, 6);
		swf_SetBits(tag, level, 8); //FIXME: fixme??
	    }

	    if(islast)
		break;
	    pos++;
	}
    }
}

static void quantize(block_t*fb, block_t*b, int has_dc, int quant)
{
    quantize8x8(fb->y1, b->y1, has_dc, quant);
    quantize8x8(fb->y2, b->y2, has_dc, quant);
    quantize8x8(fb->y3, b->y3, has_dc, quant);
    quantize8x8(fb->y4, b->y4, has_dc, quant);
    quantize8x8(fb->u, b->u, has_dc, quant);
    quantize8x8(fb->v, b->v, has_dc, quant);
}

static void dodct(block_t*fb)
{
    dct(fb->y1); dct(fb->y2); dct(fb->y3); dct(fb->y4);
    dct(fb->u);  dct(fb->v);
    zigzag(fb->y1);
    zigzag(fb->y2);
    zigzag(fb->y3);
    zigzag(fb->y4);
    zigzag(fb->u);
    zigzag(fb->v);
}
static void dodctandquant(block_t*fb, block_t*b, int has_dc, int quant)
{
    int t;
    if(has_dc) {
	dodct(fb);
	quantize(fb,b,has_dc,quant);
	return;
    }
    preparequant(quant);
    dct2(fb->y1,b->y1); dct2(fb->y2,b->y2); dct2(fb->y3,b->y3); dct2(fb->y4,b->y4);
    dct2(fb->u,b->u);  dct2(fb->v,b->v);
}

static void doidct(block_t*b)
{
    block_t fb;
    int t;
    for(t=0;t<64;t++) {
	fb.y1[t] = b->y1[zigzagtable[t]];
	fb.y2[t] = b->y2[zigzagtable[t]];
	fb.y3[t] = b->y3[zigzagtable[t]];
	fb.y4[t] = b->y4[zigzagtable[t]];
	fb.u[t] = b->u[zigzagtable[t]];
	fb.v[t] = b->v[zigzagtable[t]];
    }
    idct(fb.y1); idct(fb.y2); idct(fb.y3); idct(fb.y4);
    idct(fb.u);  idct(fb.v);
    for(t=0;t<64;t++) {
	b->y1[t] = fb.y1[t];
	b->y2[t] = fb.y2[t];
	b->y3[t] = fb.y3[t];
	b->y4[t] = fb.y4[t];
	b->u[t] = fb.u[t];
	b->v[t] = fb.v[t];
    }
}
static void truncateblock(block_t*b)
{
    int t;
    for(t=0;t<64;t++) {
	b->y1[t] = truncate256(b->y1[t]);
	b->y2[t] = truncate256(b->y2[t]);
	b->y3[t] = truncate256(b->y3[t]);
	b->y4[t] = truncate256(b->y4[t]);
	b->u[t] = truncate256(b->u[t]);
	b->v[t] = truncate256(b->v[t]);
    }
}

static void dequantize(block_t*b, int has_dc, int quant)
{
    dequantize8x8(b->y1, has_dc, quant);
    dequantize8x8(b->y2, has_dc, quant);
    dequantize8x8(b->y3, has_dc, quant);
    dequantize8x8(b->y4, has_dc, quant);
    dequantize8x8(b->u, has_dc, quant);
    dequantize8x8(b->v, has_dc, quant);
}

static void getblockpatterns(block_t*b, int*cbpybits,int*cbpcbits, int has_dc)
{
    *cbpybits = 0;
    *cbpcbits = 0;

    *cbpybits|=hascoef(b->y1, has_dc)*8;
    *cbpybits|=hascoef(b->y2, has_dc)*4;
    *cbpybits|=hascoef(b->y3, has_dc)*2;
    *cbpybits|=hascoef(b->y4, has_dc)*1;

    *cbpcbits|=hascoef(b->u, has_dc)*2;
    *cbpcbits|=hascoef(b->v, has_dc)*1;
}

static void setQuant(TAG*tag, int dquant)
{
    int code = 0;
    /* 00 01 10 11
       -1 -2 +1 +2
    */
    if(dquant == -1) {
	swf_SetBits(tag, 0x0, 2);
    } else if(dquant == -2) {
	swf_SetBits(tag, 0x1, 2);
    } else if(dquant == +1) {
	swf_SetBits(tag, 0x2, 2);
    } else if(dquant == +2) {
	swf_SetBits(tag, 0x3, 2);
    } else {
	assert(0*strlen("invalid dquant"));
    }
}

static void change_quant(int quant, int*dquant)
{
    /* TODO */
    *dquant = 0;
}

static void encode_blockI(TAG*tag, VIDEOSTREAM*s, int bx, int by, int*quant)
{
    block_t fb;
    block_t b;
    int dquant=0;
    int cbpcbits = 0, cbpybits=0;

    getregion(&fb, s->current, bx, by, s->width);

    change_quant(*quant, &dquant);
    *quant+=dquant;

    dodctandquant(&fb, &b, 1, *quant);
    //quantize(&fb, &b, 1, *quant);

    //decode_blockI(s, &b, bx, by);

    getblockpatterns(&b, &cbpybits, &cbpcbits, 1);

    if(dquant) {
	codehuffman(tag, mcbpc_intra, 4+cbpcbits);
    } else {
	codehuffman(tag, mcbpc_intra, 0+cbpcbits);
    }

    codehuffman(tag, cbpy, cbpybits);

    if(dquant) {
	setQuant(tag, dquant);
    }

    /* luminance */
    encode8x8(tag, b.y1, 1, cbpybits&8);
    encode8x8(tag, b.y2, 1, cbpybits&4);
    encode8x8(tag, b.y3, 1, cbpybits&2);
    encode8x8(tag, b.y4, 1, cbpybits&1);

    /* chrominance */
    encode8x8(tag, b.u, 1, cbpcbits&2);
    encode8x8(tag, b.v, 1, cbpcbits&1);

    /* reconstruct */
    dequantize(&b, 1, *quant);
    doidct(&b);
    truncateblock(&b);
    copyblock(s, s->current, &b, bx, by);
}

static void yuvdiff(block_t*a, block_t*b)
{
    int t;
    for(t=0;t<64;t++) {
	a->y1[t] = (a->y1[t] - b->y1[t]);
	a->y2[t] = (a->y2[t] - b->y2[t]);
	a->y3[t] = (a->y3[t] - b->y3[t]);
	a->y4[t] = (a->y4[t] - b->y4[t]);
	a->u[t]  = (a->u[t] - b->u[t]);
	a->v[t]  = (a->v[t] - b->v[t]);
    }
}

static void predictmvd(VIDEOSTREAM*s, int bx, int by, int*px, int*py)
{
    int i1,i2;
    int x1,y1,x2,y2,x3,y3;
    int x4,y4,p;
    if(bx) {x1=s->mvdx[by*s->bbx+bx-1];
	    y1=s->mvdy[by*s->bbx+bx-1];
    } else {x1=y1=0;}

    if(by) {x2=s->mvdx[(by-1)*s->bbx+bx];
	    y2=s->mvdy[(by-1)*s->bbx+bx];
	    if(bx<s->bbx-1) {
		x3=s->mvdx[(by-1)*s->bbx+bx+1];
		y3=s->mvdy[(by-1)*s->bbx+bx+1];
	    } else {
		x3=y3=0;
	    }
           }
    else   {x2=x3=x1;y2=y3=y1;}

	   if((x1 <= x2 && x2 <= x3) ||
	      (x3 <= x2 && x2 <= x1)) {
	x4=x2;
    } else if((x2 <= x1 && x1 <= x3) ||
	      (x3 <= x1 && x1 <= x2)) {
	x4=x1;
    } else if((x1 <= x3 && x3 <= x2) ||
	      (x2 <= x3 && x3 <= x1)) {
	x4=x3;
    }

	   if((y1 <= y2 && y2 <= y3) ||
	      (y3 <= y2 && y2 <= y1)) {
	y4=y2;
    } else if((y2 <= y1 && y1 <= y3) ||
	      (y3 <= y1 && y1 <= y2)) {
	y4=y1;
    } else if((y1 <= y3 && y3 <= y2) ||
	      (y2 <= y3 && y3 <= y1)) {
	y4=y3;
    }

    *px = x4;
    *py = y4;
    assert((x4>=-32 && x4<=31) && (y4>=-32 && y4<=31));
}

static inline int mvd2index(int px, int py, int x, int y, int xy)
{
    assert((x>=-32 && x<=31) && (y>=-32 && y<=31));
    //assert((x&1)==0 && (y&1)==0);//for now
    //assert((x&2)==0 && (y&2)==0);//for now(2)

    x-=px;
    y-=py;

    if(xy)
	x=y;
    x+=32;

    /* (x&63) */
    if(x>63)
	x-=64;
    if(x<0)
	x+=64;

    assert(x>=0 && x<64);
    return x;
}

static int encode_blockP(TAG*tag, VIDEOSTREAM*s, int bx, int by, int*quant)
{
    block_t fb;
    block_t b;
    int dquant=0;
    int has_mvd=0;
    int has_mvd24=0;
    int has_dc=1;
    int mode = 0;
    int cbpcbits = 0, cbpybits=0;
    int diff;
    int predictmvdx;
    int predictmvdy;

    block_t b_i;
    int bits_i;

    block_t fbold_v00;
    block_t b_v00;
    int bits_v00 = 65535;
    int x_v00=0;
    int y_v00=0;

    diff = compareregions(s, bx, by);

    if(diff < 20 /*TODO: should be a parameter- good values are between 32 and 48 */) {
	/* TODO: measure the error an I-block encoding would do, and base the decision
	   on that */
	swf_SetBits(tag, 1,1); /* cod=1, block skipped */
	/* copy the region from the last frame so that we have a complete reconstruction */
	copyregion(s, s->current, s->oldpic, bx, by);
	return 1;
    }

    predictmvd(s,bx,by,&predictmvdx,&predictmvdy);
    getregion(&fb, s->current, bx, by, s->width);

    { /* consider I-block */
	block_t fb_i;
	int y,c;
	memcpy(&fb_i, &fb, sizeof(block_t));
	dodctandquant(&fb_i, &b_i, 1, *quant);
	//quantize(&fb_i, &b_i, 1, *quant);
	getblockpatterns(&b_i, &y, &c, 1);
	bits_i = 1; //cod
	bits_i += mcbpc_inter[3*4+c].len;
	bits_i += cbpy[y].len;
	bits_i += coefbits8x8(b_i.y1, 1);
	bits_i += coefbits8x8(b_i.y2, 1);
	bits_i += coefbits8x8(b_i.y3, 1);
	bits_i += coefbits8x8(b_i.y4, 1);
	bits_i += coefbits8x8(b_i.u, 1);
	bits_i += coefbits8x8(b_i.v, 1);
    }

    { /* consider mvd(x,y)-block */
	block_t fbdiff;
	int y,c;

	x_v00=0;
	y_v00=0;

	if(s->do_motion) {
	    int hx,hy;
	    int bestx=0,besty=0,bestbits=65536;
	    int startx=-2,endx=2;
	    int starty=-2,endy=2;

	    if(!bx) startx=0;
	    if(!by) starty=0;
	    if(bx==s->bbx-1) endx=0;
	    if(by==s->bby-1) endy=0;

	    for(hx=startx;hx<=endx;hx+=1)
	    for(hy=starty;hy<=endy;hy+=1)
	    {
		block_t b;
		block_t fbold;
		int bits = 0;
		memcpy(&fbdiff, &fb, sizeof(block_t));
		getmvdregion(&fbold, s->oldpic, bx, by, hx, hy, s->linex);
		yuvdiff(&fbdiff, &fbold);
		dodctandquant(&fbdiff, &b, 0, *quant);
		//quantize(&fbdiff, &b, 0, *quant);
		bits += coefbits8x8(b.y1, 0);
		bits += coefbits8x8(b.y2, 0);
		bits += coefbits8x8(b.y3, 0);
		bits += coefbits8x8(b.y4, 0);
		bits += coefbits8x8(b.u, 0);
		bits += coefbits8x8(b.v, 0);
		if(bits<bestbits) {
		    bestbits = bits;
		    bestx = hx;
		    besty = hy;
		}
	    }
	    x_v00 = bestx;
	    y_v00 = besty;
	}

	memcpy(&fbdiff, &fb, sizeof(block_t));
	getmvdregion(&fbold_v00, s->oldpic, bx, by, x_v00, y_v00, s->linex);
	yuvdiff(&fbdiff, &fbold_v00);
	dodctandquant(&fbdiff, &b_v00, 0, *quant);
	//quantize(&fbdiff, &b_v00, 0, *quant);
	getblockpatterns(&b_v00, &y, &c, 0);

	bits_v00 = 1; //cod
	bits_v00 += mcbpc_inter[0*4+c].len;
	bits_v00 += cbpy[y^15].len;
	bits_v00 += mvd[mvd2index(predictmvdx, predictmvdy, x_v00, y_v00, 0)].len; // (0,0)
	bits_v00 += mvd[mvd2index(predictmvdx, predictmvdy, x_v00, y_v00, 1)].len;
	bits_v00 += coefbits8x8(b_v00.y1, 0);
	bits_v00 += coefbits8x8(b_v00.y2, 0);
	bits_v00 += coefbits8x8(b_v00.y3, 0);
	bits_v00 += coefbits8x8(b_v00.y4, 0);
	bits_v00 += coefbits8x8(b_v00.u, 0);
	bits_v00 += coefbits8x8(b_v00.v, 0);
    }

    if(bits_i > bits_v00)
    {
	/* mvd (0,0) block (mode=0) */
	int t;
	mode = 0; // mvd w/o mvd24
	has_dc = 0;
	memcpy(&b, &b_v00, sizeof(block_t));

	getblockpatterns(&b, &cbpybits, &cbpcbits, has_dc);
	swf_SetBits(tag,0,1); // COD
	codehuffman(tag, mcbpc_inter, mode*4+cbpcbits);
	codehuffman(tag, cbpy, cbpybits^15);

	/* vector */
	codehuffman(tag, mvd, mvd2index(predictmvdx, predictmvdy, x_v00, y_v00, 0));
	codehuffman(tag, mvd, mvd2index(predictmvdx, predictmvdy, x_v00, y_v00, 1));
	s->mvdx[by*s->bbx+bx] = x_v00;
	s->mvdy[by*s->bbx+bx] = y_v00;

	/* luminance */
	encode8x8(tag, b.y1, has_dc, cbpybits&8);
	encode8x8(tag, b.y2, has_dc, cbpybits&4);
	encode8x8(tag, b.y3, has_dc, cbpybits&2);
	encode8x8(tag, b.y4, has_dc, cbpybits&1);

	/* chrominance */
	encode8x8(tag, b.u, has_dc, cbpcbits&2);
	encode8x8(tag, b.v, has_dc, cbpcbits&1);

	/* -- reconstruction -- */
	dequantize(&b, 0, *quant);
	doidct(&b);
	for(t=0;t<64;t++) {
	    b.y1[t] = truncate256(b.y1[t] + (int)fbold_v00.y1[t]);
	    b.y2[t] = truncate256(b.y2[t] + (int)fbold_v00.y2[t]);
	    b.y3[t] = truncate256(b.y3[t] + (int)fbold_v00.y3[t]);
	    b.y4[t] = truncate256(b.y4[t] + (int)fbold_v00.y4[t]);
	    b.u[t] = truncate256(b.u[t] + (int)fbold_v00.u[t]);
	    b.v[t] = truncate256(b.v[t] + (int)fbold_v00.v[t]);
	}
	copyblock(s, s->current, &b, bx, by);
	return bits_v00;
    } else {
	/* i block (mode=3) */
	mode = 3;
	has_dc = 1;
	memcpy(&b, &b_i, sizeof(block_t));
	getblockpatterns(&b, &cbpybits, &cbpcbits, has_dc);
	swf_SetBits(tag,0,1); // COD
	codehuffman(tag, mcbpc_inter, mode*4+cbpcbits);
	codehuffman(tag, cbpy, cbpybits);

	/* luminance */
	encode8x8(tag, b.y1, has_dc, cbpybits&8);
	encode8x8(tag, b.y2, has_dc, cbpybits&4);
	encode8x8(tag, b.y3, has_dc, cbpybits&2);
	encode8x8(tag, b.y4, has_dc, cbpybits&1);

	/* chrominance */
	encode8x8(tag, b.u, has_dc, cbpcbits&2);
	encode8x8(tag, b.v, has_dc, cbpcbits&1);

	/* -- reconstruction -- */
	dequantize(&b, 1, *quant);
	doidct(&b);
	truncateblock(&b);
	copyblock(s, s->current, &b, bx, by);
	return bits_i;
    }

    exit(1);
#if 0
    dodct(&fb);
    quantize(&fb, &b, has_dc, *quant);
    getblockpatterns(&b, &cbpybits, &cbpcbits, has_dc);

    if(!dquant && has_mvd && !has_mvd24 && !has_dc) mode = 0;
    else if(dquant && has_mvd && !has_mvd24 && !has_dc) mode = 1;
    else if(!dquant && has_mvd && has_mvd24 && !has_dc) mode = 2;
    else if(!dquant && !has_mvd && !has_mvd24 && has_dc) mode = 3;
    else if(dquant && !has_mvd && !has_mvd24 && has_dc) mode = 4;
    else exit(1);

    swf_SetBits(tag,0,1); /* cod - 1 if we're not going to code this block*/

    codehuffman(tag, mcbpc_inter, mode*4+cbpcbits);
    codehuffman(tag, cbpy, (mode==3 || mode==4)?cbpybits:cbpybits^15);

    if(dquant) {
	setQuant(tag, dquant);
    }

    if(has_mvd) {
	/* 0,0 */
	codehuffman(tag, mvd, 32);
	codehuffman(tag, mvd, 32);
    }
    if(has_mvd24) {
    }

    /* luminance */
    encode8x8(tag, b.y1, has_dc, cbpybits&8);
    encode8x8(tag, b.y2, has_dc, cbpybits&4);
    encode8x8(tag, b.y3, has_dc, cbpybits&2);
    encode8x8(tag, b.y4, has_dc, cbpybits&1);

    /* chrominance */
    encode8x8(tag, b.u, has_dc, cbpcbits&2);
    encode8x8(tag, b.v, has_dc, cbpcbits&1);
#endif
}

static int bmid = 0;
void setdbgpic(TAG*tag, RGBA*pic, int width, int height)
{
    MATRIX m;
    tag = tag->prev;

    tag = swf_InsertTag(tag,ST_REMOVEOBJECT2);
    swf_SetU16(tag, 133);

    tag = swf_InsertTag(tag, ST_DEFINEBITSLOSSLESS);
    swf_SetU16(tag, 1000+bmid);
    swf_SetLosslessBits(tag, width, height, (void*)pic, BMF_32BIT);

    tag = swf_InsertTag(tag, ST_DEFINESHAPE);
    swf_SetU16(tag, 2000+bmid);
    swf_ShapeSetBitmapRect(tag, 1000+bmid, width, height);

    tag = swf_InsertTag(tag,ST_PLACEOBJECT2);
    swf_GetMatrix(0,&m);
    m.tx = width*20;
    swf_ObjectPlace(tag, 2000+bmid, 133, &m, 0, 0);

    bmid++;
}

#define TYPE_IFRAME 0
#define TYPE_PFRAME 1

static void writeHeader(TAG*tag, int width, int height, int frame, int quant, int type)
{
    U32 i32;
    swf_SetU16(tag, frame);
    swf_SetBits(tag, 1, 17); /* picture start code*/
    swf_SetBits(tag, 0, 5); /* version=0, version 1 would optimize rle behaviour*/
    swf_SetBits(tag, frame, 8); /* time reference */

    /* write dimensions, taking advantage of some predefined sizes
       if the opportunity presents itself */
    i32 = width<<16|height;
    switch(i32)
    {
	case 352<<16|288: swf_SetBits(tag, 2, 3);break;
	case 176<<16|144: swf_SetBits(tag, 3, 3);break;
	case 128<<16|96: swf_SetBits(tag, 4, 3);break;
	case 320<<16|240: swf_SetBits(tag, 5, 3);break;
	case 160<<16|120: swf_SetBits(tag, 6, 3);break;
	default:
	    if(width>255 || height>255) {
		swf_SetBits(tag, 1, 3);
		swf_SetBits(tag, width, 16);
		swf_SetBits(tag, height, 16);
	    } else {
		swf_SetBits(tag, 0, 3);
		swf_SetBits(tag, width, 8);
		swf_SetBits(tag, height, 8);
	    }
    }

    swf_SetBits(tag, type, 2); /* I-Frame or P-Frame */
    swf_SetBits(tag, 0, 1); /* No deblock filter */
    assert(quant>0);
    swf_SetBits(tag, quant, 5); /* quantizer (1-31), may be updated later on*/
    swf_SetBits(tag, 0, 1); /* No extra info */
}

void swf_SetVideoStreamIFrame(TAG*tag, VIDEOSTREAM*s, RGBA*pic, int quant)
{
    int bx, by;

    if(quant<1) quant=1;
    if(quant>31) quant=31;

    writeHeader(tag, s->width, s->height, s->frame, quant, TYPE_IFRAME);

    rgb2yuv(s->current, pic, s->linex, s->olinex, s->width, s->height);

    //dostat(s);

    for(by=0;by<s->bby;by++)
    {
	for(bx=0;bx<s->bbx;bx++)
	{
	    encode_blockI(tag, s, bx, by, &quant);
	}
    }
    s->frame++;
    memcpy(s->oldpic, s->current, s->width*s->height*sizeof(YUV));
}

void swf_SetVideoStreamPFrame(TAG*tag, VIDEOSTREAM*s, RGBA*pic, int quant)
{
    int bx, by;

    if(quant<1) quant=1;
    if(quant>31) quant=31;

    writeHeader(tag, s->width, s->height, s->frame, quant, TYPE_PFRAME);

    rgb2yuv(s->current, pic, s->linex, s->olinex, s->width, s->height);
    memset(s->mvdx, 0, s->bbx*s->bby*sizeof(int));
    memset(s->mvdy, 0, s->bbx*s->bby*sizeof(int));

    for(by=0;by<s->bby;by++)
    {
	for(bx=0;bx<s->bbx;bx++)
	{
	    encode_blockP(tag, s, bx, by, &quant);
	}
    }
    s->frame++;
    memcpy(s->oldpic, s->current, s->width*s->height*sizeof(YUV));

//#define PNG
#ifdef MAIN
#ifdef PNG
    yuv2rgb(pic, s->current, s->linex, s->width, s->height);
    setdbgpic(tag, pic, s->width, s->height);
#endif
    if(s->frame == (int)totalframes-1)
    {
	int t;
	FILE*fi = fopen("test.ppm", "wb");
	fprintf(fi, "P6\n%d %d\n255\n", s->width, s->height);
	for(t=0;t<s->width*s->height;t++)
	{
	    fwrite(&pic[t].r, 1, 1, fi);
	    fwrite(&pic[t].g, 1, 1, fi);
	    fwrite(&pic[t].b, 1, 1, fi);
	}
	fclose(fi);
    }
#endif
}

int uline[64],vline[64],yline[64];
void swf_SetVideoStreamMover(TAG*tag, VIDEOSTREAM*s, int quant)
{
    int bx, by;

    if(quant<1) quant=1;
    if(quant>31) quant=31;

    writeHeader(tag, s->width, s->height, s->frame, quant, TYPE_PFRAME);

    memset(s->mvdx, 0, s->bbx*s->bby*sizeof(int));
    memset(s->mvdy, 0, s->bbx*s->bby*sizeof(int));

    for(by=0;by<s->bby;by++)
    {
	for(bx=0;bx<s->bbx;bx++)
	{
	    //if((lrand48()&255) || !(bx>8 && bx<24 && by>8 && by<24)) {
	    if(!(by==31)) {
		/* mvd (0,0) block (mode=0) */
		int t;
		int mode = 0; // mvd w/o mvd24
		int has_dc = 0;
		int cbpybits=0,cbpcbits=0;
		int predictmvdx, predictmvdy;
		//int mvx=-1+(2*(s->frame&1));
		//int mvy=-1+((s->frame&2));
		int mvx=(lrand48()%4)-2;
		int mvy=3;

		swf_SetBits(tag,0,1); // COD
		codehuffman(tag, mcbpc_inter, mode*4+cbpcbits);
		codehuffman(tag, cbpy, cbpybits^15);

		/* vector */
		predictmvd(s,bx,by,&predictmvdx,&predictmvdy);
		codehuffman(tag, mvd, mvd2index(predictmvdx, predictmvdy, mvx, mvy, 0));
		codehuffman(tag, mvd, mvd2index(predictmvdx, predictmvdy, mvx, mvy, 1));
		s->mvdx[by*s->bbx+bx] = mvx;
		s->mvdy[by*s->bbx+bx] = mvy;
	    } else {
		/* i block (mode=3) */
		int mode = 3;
		int has_dc = 1;
		int cbpybits,cbpcbits;
		int t;
		block_t b;
		memset(&b, 0, sizeof(block_t));
		b.y1[0] = b.y2[0] = b.y3[0] = b.y4[0] = yline[bx];
		b.u[0] = uline[bx];
		b.v[0] = vline[bx];

		getblockpatterns(&b, &cbpybits, &cbpcbits, has_dc);
		swf_SetBits(tag,0,1); // COD
		codehuffman(tag, mcbpc_inter, mode*4+cbpcbits);
		codehuffman(tag, cbpy, cbpybits);

		/* luminance */
		encode8x8(tag, b.y1, has_dc, cbpybits&8);
		encode8x8(tag, b.y2, has_dc, cbpybits&4);
		encode8x8(tag, b.y3, has_dc, cbpybits&2);
		encode8x8(tag, b.y4, has_dc, cbpybits&1);

		/* chrominance */
		encode8x8(tag, b.u, has_dc, cbpcbits&2);
		encode8x8(tag, b.v, has_dc, cbpcbits&1);
	    }
	}
    }
}

#ifdef MAIN
#include "png.h"
int main(int argn, char*argv[])
{
    int fi;
    int t;
    SWF swf;
    TAG * tag;
    RGBA* pic, *pic2, rgb;
    SWFPLACEOBJECT obj;
    int width = 0;
    int height = 0;
    int frames = 20;
    int framerate = 29;
    unsigned char*data;
    char* fname = "/home/kramm/pics/peppers.png";
    VIDEOSTREAM stream;
    double d = 1.0;

    memset(&stream, 0, sizeof(stream));

    getPNG(fname, &width, &height, &data);
    pic = (RGBA*)malloc(width*height*sizeof(RGBA));
    pic2 = (RGBA*)malloc(width*height*sizeof(RGBA));
    memcpy(pic, data, width*height*sizeof(RGBA));
    free(data);

    printf("Compressing %s, size %dx%d\n", fname, width, height);

    memset(&swf,0,sizeof(SWF));
    memset(&obj,0,sizeof(obj));

    swf.fileVersion    = 6;
    swf.frameRate      = framerate*256;
    swf.movieSize.xmax = 20*width*2;
    swf.movieSize.ymax = 20*height-20*64;

    swf.firstTag = swf_InsertTag(NULL,ST_SETBACKGROUNDCOLOR);
    tag = swf.firstTag;
    rgb.r = 0x00;rgb.g = 0x00;rgb.b = 0x00;
    swf_SetRGB(tag,&rgb);

    tag = swf_InsertTag(tag, ST_DEFINEVIDEOSTREAM);
    swf_SetU16(tag, 33);
    swf_SetVideoStreamDefine(tag, &stream, frames, width, height);
    stream.do_motion = 1;

    for(t=0;t<frames;t++)
    {
	int x,y;
	double xx,yy;
	for(y=0,yy=0;y<height;y++,yy+=d)  {
	    RGBA*line = &pic[((int)yy)*width];
	    for(x=0,xx=0;x<width;x++,xx+=d) {
		pic2[y*width+x] = line[((int)xx)];
	    }
	}
	printf("frame:%d\n", t);fflush(stdout);

	tag = swf_InsertTag(tag, ST_VIDEOFRAME);
	swf_SetU16(tag, 33);
	if(t==0)
	    swf_SetVideoStreamIFrame(tag, &stream, pic2, 9);
	else {
	    swf_SetVideoStreamPFrame(tag, &stream, pic2, 9);
	}

	tag = swf_InsertTag(tag, ST_PLACEOBJECT2);
	swf_GetPlaceObject(0, &obj);
	if(t==0) {
	    obj.depth = 1;
	    obj.id = 33;
	} else {
	    obj.move = 1;
	    obj.depth = 1;
	    obj.ratio = t;
	}
	swf_SetPlaceObject(tag,&obj);

	tag = swf_InsertTag(tag, ST_SHOWFRAME);
	d-=0.015;
    }
    swf_VideoStreamClear(&stream);

    tag = swf_InsertTag(tag, ST_END);

    fi = open("video3.swf", O_WRONLY|O_CREAT|O_TRUNC, 0644);
    if(swf_WriteSWC(fi,&swf)<0) {
	fprintf(stderr,"WriteSWF() failed.\n");
    }
    close(fi);
    swf_FreeTags(&swf);
}
#undef MAIN
#endif
