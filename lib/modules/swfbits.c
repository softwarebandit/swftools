/* swfbits.c

   Bitmap functions (needs libjpeg) 

   Extension module for the rfxswf library.
   Part of the swftools package.

   Copyright (c) 2000, 2001 Rainer B�hme <rfxswf@reflex-studio.de>
 
   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA */

#define OUTBUFFER_SIZE 0x8000

int swf_ImageHasAlpha(RGBA*img, int width, int height)
{
    int len = width*height;
    int t;
    int hasalpha=0;
    for(t=0;t<len;t++) {
	if(img[t].a >= 4 && img[t].a < 0xfc)
	    return 2;
	if(img[t].a < 4)
	    hasalpha=1;
    }
    return hasalpha;
}

int swf_ImageGetNumberOfPaletteEntries2(RGBA*_img, int width, int height)
{
    int len = width*height;
    int t;
    U32* img = (U32*)_img;
    U32 color1 = img[0];
    U32 color2 = 0;
    for(t=1;t<len;t++) {
	if(img[t] != color1) {
	    color2 = img[t];
	    break;
	}
    }
    if(t==len)
	return 1;

    for(;t<len;t++) {
	if(img[t] != color1 && img[t] != color2) {
	    return width*height;
	}
    }
    return 2;
}

/*int swf_ImageGetNumberOfPaletteEntries(RGBA*img, int width, int height, RGBA*palette)
{
    int len = width*height;
    int t;
    int palsize = 0;
    RGBA pal[512];
    U32*pal32=(U32*)pal;
    int palette_overflow = 0;
    U32 lastcol32 = 0;

    if(sizeof(RGBA)!=sizeof(U32))
	fprintf(stderr, "rfxswf: sizeof(RGBA)!=sizeof(U32))");

    lastcol32 = pal32[palsize++] = *(U32*)&img[0];

    for(t=1;t<len;t++) {
	RGBA col = img[t];
	U32 col32 = *(U32*)&img[t];
	int i;
	if(col32==lastcol32)
	    continue;
	for(i=0;i<palsize;i++) {
	    if(col32 == pal32[i])
		break;
	}
	if(i==palsize) {
	    if(palsize==512) {
		palette_overflow = 1;
		break;
	    }
	    pal32[palsize++] = col32;
	}
	lastcol32 = col32;
    }
    if(palette_overflow)
	return width*height;
    if(palette)
	memcpy(palette, pal, palsize*sizeof(RGBA));
    return palsize;
}*/

int swf_ImageGetNumberOfPaletteEntries(RGBA*img, int width, int height, RGBA*palette)
{
    int len = width*height;
    int t;
    int palsize = 0;
    U32* pal;
    int size[256];
    int palette_overflow = 0;
    U32 lastcol32 = 0;

    pal = malloc(65536*sizeof(U32));

    memset(size, 0, sizeof(size));

    if(sizeof(RGBA)!=sizeof(U32))
	fprintf(stderr, "rfxswf: sizeof(RGBA)!=sizeof(U32))");

    lastcol32 = (*(U32*)&img[0])^0xffffffff; // don't match

    for(t=0;t<len;t++) {
	RGBA col = img[t];
	U32 col32 = *(U32*)&img[t];
	int i;
	int csize;
	U32 hash;
	U32* cpal;
	if(col32 == lastcol32)
	    continue;
	hash = (col32 >> 17) ^ col32;
	hash ^= ((hash>>8) + 1) ^ hash;
	hash &= 255;

	csize = size[hash];
	cpal = &pal[hash*256];
	for(i=0;i<csize;i++) {
	    if(col32 == cpal[i])
		break;
	}
	if(i==csize) {
	    if(palsize==256) {
		palette_overflow = 1;
		break;
	    }
	    cpal[size[hash]++] = col32;
	    palsize++;
	}
	lastcol32 = col32;
    }
    if(palette_overflow) {
	free(pal);
	return width*height;
    }
    if(palette) {
	int i = 0;
	for(t=0;t<256;t++) {
	    int s;
	    int csize = size[t];
	    U32* cpal = &pal[t*256];
	    for(s=0;s<csize;s++)
		palette[i++] = *(RGBA*)(&cpal[s]);
	}
    }
    free(pal);
    return palsize;
}



#ifdef HAVE_JPEGLIB

typedef struct _JPEGDESTMGR {
    struct jpeg_destination_mgr mgr;
    TAG *t;
    JOCTET *buffer;
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
} JPEGDESTMGR, *LPJPEGDESTMGR;

// Destination manager callbacks

static void RFXSWF_init_destination(j_compress_ptr cinfo)
{
    JPEGDESTMGR *dmgr = (JPEGDESTMGR *) cinfo->dest;
    dmgr->buffer = (JOCTET *) rfx_alloc(OUTBUFFER_SIZE);
    dmgr->mgr.next_output_byte = dmgr->buffer;
    dmgr->mgr.free_in_buffer = OUTBUFFER_SIZE;
}

static boolean RFXSWF_empty_output_buffer(j_compress_ptr cinfo)
{
    JPEGDESTMGR *dmgr = (JPEGDESTMGR *) cinfo->dest;
    swf_SetBlock(dmgr->t, (U8 *) dmgr->buffer, OUTBUFFER_SIZE);
    dmgr->mgr.next_output_byte = dmgr->buffer;
    dmgr->mgr.free_in_buffer = OUTBUFFER_SIZE;
    return TRUE;
}

static void RFXSWF_term_destination(j_compress_ptr cinfo)
{
    JPEGDESTMGR *dmgr = (JPEGDESTMGR *) cinfo->dest;
    swf_SetBlock(dmgr->t, (U8 *) dmgr->buffer,
		 OUTBUFFER_SIZE - dmgr->mgr.free_in_buffer);
    rfx_free(dmgr->buffer);
    dmgr->mgr.free_in_buffer = 0;
}

JPEGBITS *swf_SetJPEGBitsStart(TAG * t, int width, int height, int quality)
{
    JPEGDESTMGR *jpeg;

    // redirect compression lib output to local SWF Tag structure

    jpeg = (JPEGDESTMGR *) rfx_calloc(sizeof(JPEGDESTMGR));

    jpeg->cinfo.err = jpeg_std_error(&jpeg->jerr);

    jpeg_create_compress(&jpeg->cinfo);

    jpeg->mgr.init_destination = RFXSWF_init_destination;
    jpeg->mgr.empty_output_buffer = RFXSWF_empty_output_buffer;
    jpeg->mgr.term_destination = RFXSWF_term_destination;

    jpeg->t = t;

    jpeg->cinfo.dest = (struct jpeg_destination_mgr *) jpeg;

    // init compression

    jpeg->cinfo.image_width = width;
    jpeg->cinfo.image_height = height;
    jpeg->cinfo.input_components = 3;
    jpeg->cinfo.in_color_space = JCS_RGB;

    jpeg_set_defaults(&jpeg->cinfo);
    jpeg_set_quality(&jpeg->cinfo, quality, TRUE);

    // write tables to SWF

    jpeg_write_tables(&jpeg->cinfo);

    // compess image to SWF

    jpeg_suppress_tables(&jpeg->cinfo, TRUE);
    jpeg_start_compress(&jpeg->cinfo, FALSE);

    return (JPEGBITS *) jpeg;
}

int swf_SetJPEGBitsLines(JPEGBITS * jpegbits, U8 ** data, int n)
{
    JPEGDESTMGR *jpeg = (JPEGDESTMGR *) jpegbits;
    if (!jpeg)
	return -1;
    jpeg_write_scanlines(&jpeg->cinfo, data, n);
    return 0;
}

int swf_SetJPEGBitsLine(JPEGBITS * jpegbits, U8 * data)
{
    return swf_SetJPEGBitsLines(jpegbits, &data, 1);
}

int swf_SetJPEGBitsFinish(JPEGBITS * jpegbits)
{
    JPEGDESTMGR *jpeg = (JPEGDESTMGR *) jpegbits;
    if (!jpeg)
	return -1;
    jpeg_finish_compress(&jpeg->cinfo);
    rfx_free(jpeg);
    return 0;
}

void swf_SetJPEGBits2(TAG * tag, U16 width, U16 height, RGBA * bitmap,
		      int quality)
{
    JPEGBITS *jpeg;
    int y;
    jpeg = swf_SetJPEGBitsStart(tag, width, height, quality);
    for (y = 0; y < height; y++) {
	U8 scanline[3 * width];
	int x, p = 0;
	for (x = 0; x < width; x++) {
	    scanline[p++] = bitmap[width * y + x].r;
	    scanline[p++] = bitmap[width * y + x].g;
	    scanline[p++] = bitmap[width * y + x].b;
	}
	swf_SetJPEGBitsLine(jpeg, scanline);
    }
    swf_SetJPEGBitsFinish(jpeg);
}

void swf_GetJPEGSize(char *fname, int *width, int *height)
{
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    FILE *fi;
    *width = 0;
    *height = 0;
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);
    if ((fi = fopen(fname, "rb")) == NULL) {
	fprintf(stderr, "rfxswf: file open error\n");
	return;
    }
    jpeg_stdio_src(&cinfo, fi);
    jpeg_read_header(&cinfo, TRUE);
    *width = cinfo.image_width;
    *height = cinfo.image_height;
    jpeg_destroy_decompress(&cinfo);
    fclose(fi);
}

int swf_SetJPEGBits(TAG * t, char *fname, int quality)
{
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    JPEGBITS *out;
    FILE *f;
    U8 *scanline;

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    if ((f = fopen(fname, "rb")) == NULL) {
	fprintf(stderr, "rfxswf: file open error\n");
	return -1;
    }

    jpeg_stdio_src(&cinfo, f);
    jpeg_read_header(&cinfo, TRUE);
    jpeg_start_decompress(&cinfo);

    out =
	swf_SetJPEGBitsStart(t, cinfo.output_width, cinfo.output_height,
			     quality);
    scanline = (U8 *) rfx_alloc(4 * cinfo.output_width);

    if (scanline) {
	int y;
	U8 *js = scanline;
	if (cinfo.out_color_space == JCS_GRAYSCALE) {
	    for (y = 0; y < cinfo.output_height; y++) {
		int x;
		jpeg_read_scanlines(&cinfo, &js, 1);
		for (x = cinfo.output_width - 1; x >= 0; x--) {
		    js[x * 3] = js[x * 3 + 1] = js[x * 3 + 2] = js[x];
		}
		swf_SetJPEGBitsLines(out, (U8 **) & js, 1);
	    }
	} else if (cinfo.out_color_space == JCS_RGB) {
	    for (y = 0; y < cinfo.output_height; y++) {
		jpeg_read_scanlines(&cinfo, &js, 1);
		swf_SetJPEGBitsLines(out, (U8 **) & js, 1);
	    }
	} else if (cinfo.out_color_space == JCS_YCCK) {
	    //FIXME
	    fprintf(stderr, "Error: Can't convert YCCK to RGB.\n");
	    return -1;
	} else if (cinfo.out_color_space == JCS_YCbCr) {
	    for (y = 0; y < cinfo.output_height; y++) {
		int x;
		for (x = 0; x < cinfo.output_width; x++) {
		    int y = js[x * 3 + 0];
		    int u = js[x * 3 + 1];
		    int v = js[x * 3 + 1];
		    js[x * 3 + 0] = y + ((360 * (v - 128)) >> 8);
		    js[x * 3 + 1] =
			y - ((88 * (u - 128) + 183 * (v - 128)) >> 8);
		    js[x * 3 + 2] = y + ((455 * (u - 128)) >> 8);
		}
	    }
	} else if (cinfo.out_color_space == JCS_CMYK) {
	    for (y = 0; y < cinfo.output_height; y++) {
		int x;
		jpeg_read_scanlines(&cinfo, &js, 1);
		/* This routine seems to work for now-
		   It's a mixture of 3 different
		   CMYK->RGB conversion routines I found in the
		   web. (which all produced garbage)
		   I'm happily accepting suggestions. (mk) */
		for (x = 0; x < cinfo.output_width; x++) {
		    int white = 255 - js[x * 4 + 3];
		    js[x * 3 + 0] = white - ((js[x * 4] * white) >> 8);
		    js[x * 3 + 1] = white - ((js[x * 4 + 1] * white) >> 8);
		    js[x * 3 + 2] = white - ((js[x * 4 + 2] * white) >> 8);
		}
		swf_SetJPEGBitsLines(out, (U8 **) & js, 1);
	    }
	}
    }

    rfx_free(scanline);
    swf_SetJPEGBitsFinish(out);
    jpeg_finish_decompress(&cinfo);
    fclose(f);

    return 0;
}

typedef struct _JPEGFILEMGR {
    struct jpeg_destination_mgr mgr;
    JOCTET *buffer;
    struct jpeg_compress_struct* cinfo;
    struct jpeg_error_mgr* jerr;
    FILE*fi;
} JPEGFILEMGR;

static void file_init_destination(j_compress_ptr cinfo) 
{ 
    JPEGFILEMGR*fmgr = (JPEGFILEMGR*)(cinfo->dest);
    struct jpeg_destination_mgr*dmgr = &fmgr->mgr;

    fmgr->buffer = (JOCTET*)rfx_alloc(OUTBUFFER_SIZE);
    if(!fmgr->buffer) {
	perror("malloc");
	fprintf(stderr, "Out of memory!\n");
	exit(1);
    }

    dmgr->next_output_byte = fmgr->buffer;
    dmgr->free_in_buffer = OUTBUFFER_SIZE;
}

static boolean file_empty_output_buffer(j_compress_ptr cinfo)
{ 
    JPEGFILEMGR*fmgr = (JPEGFILEMGR*)(cinfo->dest);
    struct jpeg_destination_mgr*dmgr = &fmgr->mgr;

    if(fmgr->fi)
	fwrite(fmgr->buffer, OUTBUFFER_SIZE, 1, fmgr->fi);

    dmgr->next_output_byte = fmgr->buffer;
    dmgr->free_in_buffer = OUTBUFFER_SIZE;
    return 1;
}

static void file_term_destination(j_compress_ptr cinfo) 
{ 
    JPEGFILEMGR*fmgr = (JPEGFILEMGR*)(cinfo->dest);
    struct jpeg_destination_mgr*dmgr = &fmgr->mgr;

    if(fmgr->fi)
        fwrite(fmgr->buffer, OUTBUFFER_SIZE-dmgr->free_in_buffer, 1, fmgr->fi);

    rfx_free(fmgr->buffer);
    fmgr->buffer = 0;
    dmgr->free_in_buffer = 0;
    dmgr->next_output_byte = 0;
}

void swf_SaveJPEG(char*filename, RGBA*pixels, int width, int height, int quality)
{
    JPEGFILEMGR fmgr;
    struct jpeg_compress_struct cinfo;
    struct jpeg_error_mgr jerr;
    unsigned char*data2 = 0;
    int y;

    FILE*fi = fopen(filename, "wb");
    if(!fi) {
	char buf[256];
	sprintf(buf, "rfxswf: Couldn't create %s", filename);
	perror(buf);
	return;
    }
    data2 = rfx_calloc(width*3);

    memset(&cinfo, 0, sizeof(cinfo));
    memset(&jerr, 0, sizeof(jerr));
    memset(&fmgr, 0, sizeof(fmgr));
    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_compress(&cinfo);

    fmgr.mgr.init_destination = file_init_destination;
    fmgr.mgr.empty_output_buffer = file_empty_output_buffer;
    fmgr.mgr.term_destination = file_term_destination;
    fmgr.fi = fi;
    fmgr.cinfo = &cinfo;
    fmgr.jerr = &jerr;
    cinfo.dest = (struct jpeg_destination_mgr*)&fmgr;

    // init compression

    cinfo.image_width  = width;
    cinfo.image_height = height;
    cinfo.input_components = 3;
    cinfo.in_color_space = JCS_RGB;
    jpeg_set_defaults(&cinfo);
    cinfo.dct_method = JDCT_IFAST;
    jpeg_set_quality(&cinfo,quality,TRUE);

    //jpeg_write_tables(&cinfo);
    //jpeg_suppress_tables(&cinfo, TRUE);
    jpeg_start_compress(&cinfo, FALSE);

    for(y=0;y<height;y++) {
	int x;
	RGBA*src = &pixels[y*width];
	for(x=0;x<width;x++) {
	    data2[x*3+0] = src[x].r;
	    data2[x*3+1] = src[x].g;
	    data2[x*3+2] = src[x].b;
	}
        jpeg_write_scanlines(&cinfo, &data2, 1);
    }
    rfx_free(data2);
    jpeg_finish_compress(&cinfo);
    jpeg_destroy_compress(&cinfo);

    fclose(fi);
}

/*  jpeg_source_mgr functions */
static void tag_init_source(struct jpeg_decompress_struct *cinfo)
{
    TAG *tag = (TAG *) cinfo->client_data;
    if (tag->id == ST_DEFINEBITSJPEG3) {
	swf_SetTagPos(tag, 6);
    } else {
	swf_SetTagPos(tag, 2);
    }
    cinfo->src->bytes_in_buffer = 0;
}
static boolean tag_fill_input_buffer(struct jpeg_decompress_struct *cinfo)
{
    TAG *tag = (TAG *) cinfo->client_data;
    if (tag->data[tag->pos + 0] == 0xff &&
	tag->data[tag->pos + 1] == 0xd9 &&
	tag->data[tag->pos + 2] == 0xff &&
	tag->data[tag->pos + 3] == 0xd8) {
	tag->pos += 4;
    }
    if (tag->pos >= tag->len) {
	cinfo->src->next_input_byte = 0;
	cinfo->src->bytes_in_buffer = 0;
	return 0;
    }
    cinfo->src->next_input_byte = &tag->data[tag->pos];
    cinfo->src->bytes_in_buffer = 1;	//tag->len - tag->pos;
    tag->pos += 1;
    return 1;
}
static void tag_skip_input_data(struct jpeg_decompress_struct *cinfo, long count)
{
    TAG *tag = (TAG *) cinfo->client_data;
    cinfo->src->next_input_byte = 0;
    cinfo->src->bytes_in_buffer = 0;
    tag->pos += count;
}
static boolean tag_resync_to_restart(struct jpeg_decompress_struct *cinfo, int desired)
{
    return jpeg_resync_to_restart(cinfo, desired);
}
static void tag_term_source(struct jpeg_decompress_struct *cinfo)
{
    TAG *tag = (TAG *) cinfo->client_data;
}
RGBA *swf_JPEG2TagToImage(TAG * tag, int *width, int *height)
{
    struct jpeg_decompress_struct cinfo;
    struct jpeg_error_mgr jerr;
    struct jpeg_source_mgr mgr;
    RGBA *dest;
    int y;
    int offset = 0;
    int oldtaglen = 0;
    *width = 0;
    *height = 0;

    if (tag->id == ST_DEFINEBITSJPEG) {
	fprintf(stderr, "rfxswf: extracting from definebitsjpeg not yet supported\n");
	return 0;
    }
    if (tag->id == ST_DEFINEBITSJPEG3) {
#ifdef HAVE_ZLIB
	offset = swf_GetU32(tag);
	oldtaglen = tag->len;
	tag->len = offset+6;
#else
	fprintf(stderr, "rfxswf: extracting from definebitsjpeg3 not possible: no zlib\n");
	return 0;
#endif
    }

    cinfo.err = jpeg_std_error(&jerr);
    jpeg_create_decompress(&cinfo);

    cinfo.client_data = (void *) tag;
    cinfo.src = &mgr;
    cinfo.src->init_source = tag_init_source;
    cinfo.src->fill_input_buffer = tag_fill_input_buffer;
    cinfo.src->skip_input_data = tag_skip_input_data;
    cinfo.src->resync_to_restart = jpeg_resync_to_restart;
    cinfo.src->term_source = tag_term_source;
    cinfo.out_color_space = JCS_RGB;

    jpeg_read_header(&cinfo, TRUE);
    *width = cinfo.image_width;
    *height = cinfo.image_height;
    dest =
	rfx_alloc(sizeof(RGBA) * cinfo.image_width * cinfo.image_height);

    jpeg_start_decompress(&cinfo);
    for (y = 0; y < cinfo.output_height; y++) {
	RGBA *line = &dest[y * cinfo.image_width];
	U8 *to = (U8 *) line;
	int x;
	jpeg_read_scanlines(&cinfo, &to, 1);
	for (x = cinfo.output_width - 1; x >= 0; --x) {
	    int r = to[x * 3 + 0];
	    int g = to[x * 3 + 1];
	    int b = to[x * 3 + 2];
	    line[x].r = r;
	    line[x].g = g;
	    line[x].b = b;
	    line[x].a = 255;
	}
    }

    jpeg_finish_decompress(&cinfo);

    jpeg_destroy_decompress(&cinfo);

#ifdef HAVE_ZLIB
    if(offset) {
	uLongf datalen = cinfo.output_width*cinfo.output_height;
	U8* alphadata = (U8*)rfx_alloc(datalen);
	int error;
	tag->len = oldtaglen;
	swf_SetTagPos(tag, 6+offset);
	error = uncompress(alphadata, &datalen, &tag->data[tag->pos], tag->len - tag->pos);
	if (error != Z_OK) {
	    fprintf(stderr, "rfxswf: Zlib error %d while extracting definejpeg3\n", error);
	    return 0;
	}
	for(y=0;y<cinfo.output_height;y++) {
	    RGBA*line = &dest[y*cinfo.output_width];
	    U8*aline = &alphadata[y*cinfo.output_width];
	    int x;
	    for(x=0;x<cinfo.output_width;x++) {
		line[x].a = aline[x];
	    }
	}
	free(alphadata);
    }
#endif
    return dest;
}

#endif				// HAVE_JPEGLIB

// Lossless compression texture based on zlib

#ifdef HAVE_ZLIB

int RFXSWF_deflate_wraper(TAG * t, z_stream * zs, boolean finish)
{
    U8 *data = rfx_alloc(OUTBUFFER_SIZE);
    zs->next_out = data;
    zs->avail_out = OUTBUFFER_SIZE;
    while (1) {
	int status = deflate(zs, Z_NO_FLUSH);

	if (status != Z_OK) {
	    fprintf(stderr, "rfxswf: zlib compression error (%i)\n", status);
	    rfx_free(data);
	    return status;
	}

	if (zs->next_out != data) {
	    swf_SetBlock(t, data, zs->next_out - data);
	    zs->next_out = data;
	    zs->avail_out = OUTBUFFER_SIZE;
	}

	if (zs->avail_in == 0)
	    break;
    }

    if (!finish) {
	rfx_free(data);
	return 0;
    }

    while (1) {
	int status = deflate(zs, Z_FINISH);
	if (status != Z_OK && status != Z_STREAM_END) {
	    fprintf(stderr, "rfxswf: zlib compression error (%i)\n", status);
	    rfx_free(data);
	    return status;
	}

	if (zs->next_out != data) {
	    swf_SetBlock(t, data, zs->next_out - data);
	    zs->next_out = data;
	    zs->avail_out = OUTBUFFER_SIZE;
	}

	if (status == Z_STREAM_END)
	    break;
    }
    rfx_free(data);
    return 0;
}


int swf_SetLosslessBits(TAG * t, U16 width, U16 height, void *bitmap, U8 bitmap_flags)
{
    int res = 0;
    int bps;

    switch (bitmap_flags) {
    case BMF_8BIT:
	return swf_SetLosslessBitsIndexed(t, width, height, bitmap, NULL, 256);
    case BMF_16BIT:
	bps = BYTES_PER_SCANLINE(sizeof(U16) * width);
	break;
    case BMF_32BIT:
	bps = width * 4;
	break;
    default:
	fprintf(stderr, "rfxswf: unknown bitmap type %d\n", bitmap_flags);
	return -1;
    }

    swf_SetU8(t, bitmap_flags);
    swf_SetU16(t, width);
    swf_SetU16(t, height);

    {
	z_stream zs;

	memset(&zs, 0x00, sizeof(z_stream));
	zs.zalloc = Z_NULL;
	zs.zfree = Z_NULL;

	if (deflateInit(&zs, Z_DEFAULT_COMPRESSION) == Z_OK) {
	    zs.avail_in = bps * height;
	    zs.next_in = bitmap;

	    if (RFXSWF_deflate_wraper(t, &zs, TRUE) < 0)
		res = -3;
	    deflateEnd(&zs);

	} else
	    res = -3;		// zlib error
    }
    return res;
}

int swf_SetLosslessBitsIndexed(TAG * t, U16 width, U16 height, U8 * bitmap, RGBA * palette, U16 ncolors)
{
    RGBA *pal = palette;
    int bps = BYTES_PER_SCANLINE(width);
    int res = 0;

    if (!pal)			// create default palette for grayscale images
    {
	int i;
	pal = rfx_alloc(256 * sizeof(RGBA));
	for (i = 0; i < 256; i++) {
	    pal[i].r = pal[i].g = pal[i].b = i;
	    pal[i].a = 0xff;
	}
	ncolors = 256;
    }

    if ((ncolors < 2) || (ncolors > 256) || (!t)) {
	fprintf(stderr, "rfxswf: unsupported number of colors: %d\n",
		ncolors);
	return -1;		// parameter error
    }

    swf_SetU8(t, BMF_8BIT);
    swf_SetU16(t, width);
    swf_SetU16(t, height);
    swf_SetU8(t, ncolors - 1);	// number of pal entries

    {
	z_stream zs;

	memset(&zs, 0x00, sizeof(z_stream));
	zs.zalloc = Z_NULL;
	zs.zfree = Z_NULL;

	if (deflateInit(&zs, Z_DEFAULT_COMPRESSION) == Z_OK) {
	    U8 *zpal;		// compress palette
	    if ((zpal = rfx_alloc(ncolors * 4))) {
		U8 *pp = zpal;
		int i;

		/* be careful with ST_DEFINEBITSLOSSLESS2, because
		   the Flash player produces great bugs if you use too many
		   alpha colors in your palette. The only sensible result that
		   can be archeived is setting one color to r=0,b=0,g=0,a=0 to
		   make transparent parts in sprites. That's the cause why alpha
		   handling is implemented in lossless routines of rfxswf.

		   Indeed: I haven't understood yet how flash player handles
		   alpha values different from 0 and 0xff in lossless bitmaps...
		 */

		if (swf_GetTagID(t) == ST_DEFINEBITSLOSSLESS2)	// have alpha channel?
		{
		    for (i = 0; i < ncolors; i++) {
			pp[0] = pal[i].r;
			pp[1] = pal[i].g;
			pp[2] = pal[i].b;
			pp[3] = pal[i].a;
			pp += 4;
		    }
		    zs.avail_in = 4 * ncolors;
		} else {
		    for (i = 0; i < ncolors; i++)	// pack RGBA structures to RGB 
		    {
			pp[0] = pal[i].r;
			pp[1] = pal[i].g;
			pp[2] = pal[i].b;
			pp += 3;
		    }
		    zs.avail_in = 3 * ncolors;
		}

		zs.next_in = zpal;

		if (RFXSWF_deflate_wraper(t, &zs, FALSE) < 0)
		    res = -3;

		// compress bitmap
		zs.next_in = bitmap;
		zs.avail_in = (bps * height * sizeof(U8));

		if (RFXSWF_deflate_wraper(t, &zs, TRUE) < 0)
		    res = -3;

		deflateEnd(&zs);

		rfx_free(zpal);
	    } else
		res = -2;	// memory error
	} else
	    res = -3;		// zlib error
    }

    if (!palette)
	rfx_free(pal);

    return res;
}

int swf_SetLosslessBitsGrayscale(TAG * t, U16 width, U16 height, U8 * bitmap)
{
    return swf_SetLosslessBitsIndexed(t, width, height, bitmap, NULL, 256);
}

void swf_PreMultiplyAlpha(RGBA*data, int width, int height)
{
    int num = width*height;
    int t;
    for(t=0;t<num;t++) {
	data[t].r = ((int)data[t].r*data[t].a)/255;
	data[t].g = ((int)data[t].g*data[t].a)/255;
	data[t].b = ((int)data[t].b*data[t].a)/255;
    }
}

void swf_SetLosslessImage(TAG*tag, RGBA*data, int width, int height)
{
    int hasalpha = swf_ImageHasAlpha(data, width, height);
    int num;
    if(!hasalpha) {
	tag->id = ST_DEFINEBITSLOSSLESS;
    } else {
	tag->id = ST_DEFINEBITSLOSSLESS2;
	swf_PreMultiplyAlpha(data, width, height);
    }
    num = swf_ImageGetNumberOfPaletteEntries(data, width, height, 0);
    if(num>1 && num<=256) {
	RGBA*palette = (RGBA*)malloc(sizeof(RGBA)*num);
	int width2 = BYTES_PER_SCANLINE(width);
	U8*data2 = (U8*)malloc(width2*height);
	int len = width*height;
	int x,y;
	int r;
	swf_ImageGetNumberOfPaletteEntries(data, width, height, palette);
	for(y=0;y<height;y++) {
	    RGBA*src = &data[width*y];
	    U8*dest = &data2[width2*y];
	    for(x=0;x<width;x++) {
		RGBA col = src[x];
		for(r=0;r<num;r++) {
		    if(*(U32*)&col == *(U32*)&palette[r]) {
			dest[x] = r;
			break;
		    }
		}
		if(r==num) {
		    fprintf(stderr, "Internal error: Couldn't find color %02x%02x%02x%02x in palette (%d entries)\n",
			    col.r, col.g, col.b, col.a, num);
		    dest[x] = 0;
		}
	    }
	}
	swf_SetLosslessBitsIndexed(tag, width, height, data2, palette, num);
	free(data2);
	free(palette);
    } else {
	swf_SetLosslessBits(tag, width, height, data, BMF_32BIT);
    }
}

RGBA *swf_DefineLosslessBitsTagToImage(TAG * tag, int *dwidth, int *dheight)
{
    int id, format, height, width, pos;
    uLongf datalen, datalen2;
    int error;
    int bpp = 1;
    int cols = 0;
    int pos2 = 0;
    char alpha = tag->id == ST_DEFINEBITSLOSSLESS2;
    int t, x, y;
    RGBA *palette = 0;
    U8 *data, *data2;
    RGBA *dest;
    if (tag->id != ST_DEFINEBITSLOSSLESS &&
	tag->id != ST_DEFINEBITSLOSSLESS2) {
	fprintf(stderr, "rfxswf: Object %d is not a PNG picture!\n",
		GET16(tag->data));
	return 0;
    }
    swf_SetTagPos(tag, 0);
    id = swf_GetU16(tag);
    format = swf_GetU8(tag);
    if (format == 3)
	bpp = 8;
    if (format == 4)
	bpp = 16;
    if (format == 5)
	bpp = 32;
    if (format != 3 && format != 5) {
	if (format == 4)
	    fprintf(stderr,
		    "rfxswf: Can't handle 16-bit palette images yet (image %d)\n",
		    id);
	else
	    fprintf(stderr, "rfxswf: Unknown image type %d in image %d\n",
		    format, id);
	return 0;
    }
    *dwidth = width = swf_GetU16(tag);
    *dheight = height = swf_GetU16(tag);

    dest = rfx_alloc(sizeof(RGBA) * width * height);

    if (format == 3)
	cols = swf_GetU8(tag) + 1;
    else
	cols = 0;

    data = 0;
    datalen = (width * height * bpp / 8 + cols * 8);
    do {
	if (data)
	    rfx_free(data);
	datalen += 4096;
	data = rfx_alloc(datalen);
	error =
	    uncompress(data, &datalen, &tag->data[tag->pos],
		       tag->len - tag->pos);
    } while (error == Z_BUF_ERROR);
    if (error != Z_OK) {
	fprintf(stderr, "rfxswf: Zlib error %d (image %d)\n", error, id);
	return 0;
    }
    pos = 0;

    if (cols) {
	palette = (RGBA *) rfx_alloc(cols * sizeof(RGBA));
	for (t = 0; t < cols; t++) {
	    palette[t].r = data[pos++];
	    palette[t].g = data[pos++];
	    palette[t].b = data[pos++];
	    if (alpha) {
		palette[t].a = data[pos++];
	    } else {
		palette[t].a = 255;
	    }
	}
    }

    for (y = 0; y < height; y++) {
	int srcwidth = width * (bpp / 8);
	if (bpp == 32) {
	    if (!alpha) {
		// 32 bit to 24 bit "conversion"
		for (x = 0; x < width; x++) {
		    dest[pos2].r = data[pos + 1];
		    dest[pos2].g = data[pos + 2];
		    dest[pos2].b = data[pos + 3];
		    dest[pos2].a = 255;
		    pos2++;
		    pos += 4;	//ignore padding byte
		}
	    } else {
		for (x = 0; x < width; x++) {
		    /* TODO: is un-premultiplying alpha the right thing to do?
		       dest[pos2].r = data[pos + 1];
		       dest[pos2].g = data[pos + 2];
		       dest[pos2].b = data[pos + 3];*/
		    int alpha = data[pos+0];
		    if(alpha) {
			dest[pos2].r = ((int)data[pos + 1]*255)/alpha;
			dest[pos2].g = ((int)data[pos + 2]*255)/alpha;
			dest[pos2].b = ((int)data[pos + 3]*255)/alpha;
		    } else {
			dest[pos2].r = data[pos + 1];
			dest[pos2].g = data[pos + 2];
			dest[pos2].b = data[pos + 3];
		    }
		    dest[pos2].a = data[pos + 0];	//alpha
		    pos2++;
		    pos += 4;
		}
	    }
	} else {
	    for (x = 0; x < srcwidth; x++) {
		dest[pos2] = palette[data[pos++]];
		pos2++;
	    }
	}
	pos += ((srcwidth + 3) & ~3) - srcwidth;	//align
    }
    if (palette)
	rfx_free(palette);
    rfx_free(data);
    return dest;
}

#endif				// HAVE_ZLIB

#if defined(HAVE_ZLIB) && defined(HAVE_JPEGLIB)
int swf_SetJPEGBits3(TAG * tag, U16 width, U16 height, RGBA * bitmap, int quality)
{
    JPEGBITS *jpeg;
    int y;
    int pos;
    int res = 0;
    U8 *data;
    z_stream zs;

    pos = tag->len;
    swf_SetU32(tag, 0);		//placeholder
    jpeg = swf_SetJPEGBitsStart(tag, width, height, quality);
    for (y = 0; y < height; y++) {
	U8 scanline[3 * width];
	int x, p = 0;
	for (x = 0; x < width; x++) {
	    scanline[p++] = bitmap[width * y + x].r;
	    scanline[p++] = bitmap[width * y + x].g;
	    scanline[p++] = bitmap[width * y + x].b;
	}
	swf_SetJPEGBitsLine(jpeg, scanline);
    }
    swf_SetJPEGBitsFinish(jpeg);
    PUT32(&tag->data[pos], tag->len - pos - 4);

    data = rfx_alloc(OUTBUFFER_SIZE);
    memset(&zs, 0x00, sizeof(z_stream));

    if (deflateInit(&zs, Z_DEFAULT_COMPRESSION) != Z_OK) {
	fprintf(stderr, "rfxswf: zlib compression failed");
	return -3;
    }

    zs.next_out = data;
    zs.avail_out = OUTBUFFER_SIZE;

    for (y = 0; y < height; y++) {
	U8 scanline[width];
	int x, p = 0;
	for (x = 0; x < width; x++) {
	    scanline[p++] = bitmap[width * y + x].a;
	}
	zs.avail_in = width;
	zs.next_in = scanline;

	while (1) {
	    if (deflate(&zs, Z_NO_FLUSH) != Z_OK) {
		fprintf(stderr, "rfxswf: zlib compression failed");
		return -4;
	    }
	    if (zs.next_out != data) {
		swf_SetBlock(tag, data, zs.next_out - data);
		zs.next_out = data;
		zs.avail_out = OUTBUFFER_SIZE;
	    }
	    if (!zs.avail_in) {
		break;
	    }
	}
    }

    while (1) {
	int ret = deflate(&zs, Z_FINISH);
	if (ret != Z_OK && ret != Z_STREAM_END) {
	    fprintf(stderr, "rfxswf: zlib compression failed");
	    return -5;
	}
	if (zs.next_out != data) {
	    swf_SetBlock(tag, data, zs.next_out - data);
	    zs.next_out = data;
	    zs.avail_out = OUTBUFFER_SIZE;
	}
	if (ret == Z_STREAM_END) {
	    break;
	}
    }

    deflateEnd(&zs);
    rfx_free(data);
    return 0;
}

TAG* swf_AddImage(TAG*tag, int bitid, RGBA*mem, int width, int height, int quality)
{
    TAG *tag1 = 0, *tag2 = 0;
    int has_alpha = swf_ImageHasAlpha(mem,width,height);

    /* try lossless image */
    tag1 = swf_InsertTag(0, /*ST_DEFINEBITSLOSSLESS1/2*/0);
    swf_SetU16(tag1, bitid);
    swf_SetLosslessImage(tag1, mem, width, height);

    /* try jpeg image */
    if(has_alpha) {
	tag2 = swf_InsertTag(0, ST_DEFINEBITSJPEG3);
	swf_SetU16(tag2, bitid);
	swf_SetJPEGBits3(tag2, width, height, mem, quality);
    } else {
	tag2 = swf_InsertTag(0, ST_DEFINEBITSJPEG2);
	swf_SetU16(tag2, bitid);
	swf_SetJPEGBits2(tag2, width, height, mem, quality);
    }

    if(quality>100 || (tag1 && tag1->len < tag2->len)) {
	/* use the zlib version- it's smaller */
	tag1->prev = tag;
	if(tag) tag->next = tag1;
	tag = tag1;
	swf_DeleteTag(tag2);
    } else {
	/* use the jpeg version- it's smaller */
	tag2->prev = tag;
	if(tag) tag->next = tag2;
	tag = tag2;
	swf_DeleteTag(tag1);
    }
    return tag;
}

#endif

RGBA *swf_ExtractImage(TAG * tag, int *dwidth, int *dheight)
{
    RGBA *img;
    
    swf_SetTagPos(tag, 2); // id is 2 bytes

    if (tag->id == ST_DEFINEBITSJPEG ||
	tag->id == ST_DEFINEBITSJPEG2 || tag->id == ST_DEFINEBITSJPEG3) {
#ifdef HAVE_JPEGLIB
	return swf_JPEG2TagToImage(tag, dwidth, dheight);
#else
	fprintf(stderr, "rfxswf: Error: No JPEG library compiled in");
	return 0;
#endif
    }
    if (tag->id == ST_DEFINEBITSLOSSLESS ||
	tag->id == ST_DEFINEBITSLOSSLESS2) {
#ifdef HAVE_ZLIB
	return swf_DefineLosslessBitsTagToImage(tag, dwidth, dheight);
#else
	fprintf(stderr, "rfxswf: Error: No JPEG library compiled in");
	return 0;
#endif
    }
    fprintf(stderr, "rfxswf: Error: Invalid tag (%d, %s)", tag->id,
	    swf_TagGetName(tag));
    return 0;
}

#undef OUTBUFFER_SIZE


void swf_RemoveJPEGTables(SWF * swf)
{
    TAG *tag = swf->firstTag;
    TAG *tables_tag = 0;
    while (tag) {
	if (tag->id == ST_JPEGTABLES) {
	    tables_tag = tag;
	}
	tag = tag->next;
    }

    if (!tables_tag)
	return;

    tag = swf->firstTag;
    while (tag) {
	if (tag->id == ST_DEFINEBITSJPEG) {
	    int len = tag->len;
	    void *data = rfx_alloc(len);
	    swf_GetBlock(tag, data, tag->len);
	    swf_ResetTag(tag, ST_DEFINEBITSJPEG2);
	    swf_SetBlock(tag, &((U8*)data)[0], 2); //id
	    swf_SetBlock(tag, tables_tag->data, tables_tag->len);
	    swf_SetBlock(tag, &((U8*)data)[2], len-2);
	    free(data);
	}
	tag = tag->next;
    }
    if (swf->firstTag == tables_tag)
	swf->firstTag = tables_tag->next;
    swf_DeleteTag(tables_tag);
}

typedef struct scale_lookup {
    int pos;
    unsigned int weight;
} scale_lookup_t;

typedef struct rgba_int {
    unsigned int r,g,b,a;
} rgba_int_t;

static int bicubic = 0;

static scale_lookup_t**make_scale_lookup(int width, int newwidth)
{
    scale_lookup_t*lookupx = malloc((width>newwidth?width:newwidth)*2*sizeof(scale_lookup_t));
    scale_lookup_t**lblockx = (scale_lookup_t**)malloc((newwidth+1)*sizeof(scale_lookup_t**));
    double fx = ((double)width)/((double)newwidth);
    double px = 0;
    int x;
    scale_lookup_t*p_x = lookupx;

    if(newwidth<=width) {
	for(x=0;x<newwidth;x++) {
	    double ex = px + fx;
	    int fromx = (int)px;
	    int tox = (int)ex;
	    double rem = fromx+1-px;
	    int i = (int)(256/fx);
	    int xweight = (int)(rem*256/fx);
	    int xx;
	    int w = 0;
	    lblockx[x] = p_x;
	    if(tox>=width) tox = width-1;
	    for(xx=fromx;xx<=tox;xx++) {
		if(xx==fromx && xx==tox) p_x->weight = 256;
		else if(xx==fromx) p_x->weight = xweight;
		else if(xx==tox) p_x->weight = 256-w;
		else p_x->weight = i;
		w+=p_x->weight;
		p_x->pos = xx;
		p_x++;
	    }
	    px = ex;
	}
    } else {
	for(x=0;x<newwidth;x++) {
	    int ix1 = (int)px;
	    int ix2 = ((int)px)+1;
	    double r = px-ix1;
	    if(ix2>=width) ix2=width-1;
	    lblockx[x] = p_x;
	    if(bicubic)
		r = -2*r*r*r+3*r*r;
	    p_x[0].weight = (int)(256*(1-r));
	    p_x[0].pos = ix1;
	    p_x[1].weight = 256-p_x[0].weight;
	    p_x[1].pos = ix2;
	    p_x+=2;
	    px += fx;
	}
    }
    lblockx[newwidth] = p_x;
    return lblockx;
}

static void encodeMonochromeImage(RGBA*data, int width, int height, RGBA*colors)
{
    int t;
    int len = width*height;

    U32* img = (U32*)data;
    U32 color1 = img[0];
    U32 color2 = 0;
    for(t=1;t<len;t++) {
	if(img[t] != color1) {
	    color2 = img[t];
	    break;
	}
    }
    *(U32*)&colors[0] = color1;
    *(U32*)&colors[1] = color2;
    for(t=0;t<len;t++) {
	if(img[t] == color1) {
	    img[t] = 0;
	} else {
	    img[t] = 0xffffffff;
	}
    }
}

static void decodeMonochromeImage(RGBA*data, int width, int height, RGBA*colors)
{
    int t;
    int len = width*height;

    for(t=0;t<len;t++) {
	U32 m = data[t].r;
	data[t].r = (colors[0].r * (255-m) + colors[1].r * m) >> 8;
	data[t].g = (colors[0].g * (255-m) + colors[1].g * m) >> 8;
	data[t].b = (colors[0].b * (255-m) + colors[1].b * m) >> 8;
	data[t].a = (colors[0].a * (255-m) + colors[1].a * m) >> 8;
    }
}

RGBA* swf_ImageScale(RGBA*data, int width, int height, int newwidth, int newheight)
{
    int x,y;
    RGBA* newdata; 
    scale_lookup_t *p, **lblockx,**lblocky;
    rgba_int_t*tmpline;
    int monochrome = 0;
    RGBA monochrome_colors[2];
    
    if(newwidth<1 || newheight<1)
	return 0;

    if(swf_ImageGetNumberOfPaletteEntries2(data, width, height) == 2) {
	monochrome=1;
	encodeMonochromeImage(data, width, height, monochrome_colors);
    }

    tmpline = (rgba_int_t*)malloc(width*sizeof(rgba_int_t));
    newdata = (RGBA*)malloc(newwidth*newheight*sizeof(RGBA));
  
    lblockx = make_scale_lookup(width, newwidth);
    lblocky = make_scale_lookup(height, newheight);

    for(p=lblocky[0];p<lblocky[newheight];p++)
	p->pos*=width;

    for(y=0;y<newheight;y++) {
	RGBA*destline = &newdata[y*newwidth];
	
	/* create lookup table for y */
	rgba_int_t*l = tmpline;
	scale_lookup_t*p_y,*p_x;
	memset(tmpline, 0, width*sizeof(rgba_int_t));
	for(p_y=lblocky[y];p_y<lblocky[y+1];p_y++) {
	    RGBA*line = &data[p_y->pos];
	    scale_lookup_t*p_x;
	    int weight = p_y->weight;
	    for(x=0;x<width;x++) {
		tmpline[x].r += line[x].r*weight;
		tmpline[x].g += line[x].g*weight;
		tmpline[x].b += line[x].b*weight;
		tmpline[x].a += line[x].a*weight;
	    }
	}

	/* process x direction */
	p_x = lblockx[0];
	for(x=0;x<newwidth;x++) {
	    unsigned int r=0,g=0,b=0,a=0;
	    scale_lookup_t*p_x_to = lblockx[x+1];
	    do { 
		rgba_int_t* col = &tmpline[p_x->pos];
		unsigned int weight = p_x->weight;
		r += col->r*weight;
		g += col->g*weight;
		b += col->b*weight;
		a += col->a*weight;
		p_x++;
	    } while (p_x<p_x_to);

	    destline->r = r >> 16;
	    destline->g = g >> 16;
	    destline->b = b >> 16;
	    destline->a = a >> 16;
	   
	    destline++;
	}
    }

    if(monochrome)
	decodeMonochromeImage(newdata, newwidth, newheight, monochrome_colors);

    free(tmpline);
    free(*lblockx);
    free(lblockx);
    free(*lblocky);
    free(lblocky);
    return newdata;
}


