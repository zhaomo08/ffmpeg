#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>

#define WORD uint16_t
#define DWORD uint32_t
#define LONG int32_t

#pragma pack(2)
typedef struct tagBITMAPFILEHEADER {
  WORD  bfType;
  DWORD bfSize;
  WORD  bfReserved1;
  WORD  bfReserved2;
  DWORD bfOffBits;
} BITMAPFILEHEADER, *PBITMAPFILEHEADER;


typedef struct tagBITMAPINFOHEADER {
  DWORD biSize;
  LONG  biWidth;
  LONG  biHeight;
  WORD  biPlanes;
  WORD  biBitCount;
  DWORD biCompression;
  DWORD biSizeImage;
  LONG  biXPelsPerMeter;
  LONG  biYPelsPerMeter;
  DWORD biClrUsed;
  DWORD biClrImportant;
} BITMAPINFOHEADER, *PBITMAPINFOHEADER;

void saveBMP(struct SwsContext *img_convert_ctx, AVFrame *frame, int w, int h, char *filename)
{
    //1 先进行转换,  YUV420=>RGB24:
    // int w = img_convert_ctx->frame_dst->width;
    // int h = img_convert_ctx->frame_dst->height;

    int data_size = w * h * 3;

    AVFrame *pFrameRGB = av_frame_alloc();
  
    //avpicture_fill((AVPicture *)pFrameRGB, buffer, AV_PIX_FMT_BGR24, w, h);
    pFrameRGB->width = w;
    pFrameRGB->height = h;
    pFrameRGB->format =  AV_PIX_FMT_BGR24;

    av_frame_get_buffer(pFrameRGB, 0);

    sws_scale(img_convert_ctx, 
	          (const uint8_t* const *)frame->data, 
              frame->linesize,
              0, frame->height, pFrameRGB->data, pFrameRGB->linesize);

    //2 构造 BITMAPINFOHEADER
    BITMAPINFOHEADER header;
    header.biSize = sizeof(BITMAPINFOHEADER);


    header.biWidth = w;
    header.biHeight = h*(-1);
    header.biBitCount = 24;
    header.biCompression = 0;
    header.biSizeImage = 0;
    header.biClrImportant = 0;
    header.biClrUsed = 0;
    header.biXPelsPerMeter = 0;
    header.biYPelsPerMeter = 0;
    header.biPlanes = 1;

    //3 构造文件头
    BITMAPFILEHEADER bmpFileHeader = {0,};
    //HANDLE hFile = NULL;
    DWORD dwTotalWriten = 0;
    DWORD dwWriten;

    bmpFileHeader.bfType = 0x4d42; //'BM';
    bmpFileHeader.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER)+ data_size;
    bmpFileHeader.bfOffBits=sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER);

    FILE* pf = fopen(filename, "wb");
    fwrite(&bmpFileHeader, sizeof(BITMAPFILEHEADER), 1, pf);
    fwrite(&header, sizeof(BITMAPINFOHEADER), 1, pf);
    fwrite(pFrameRGB->data[0], 1, data_size, pf);
    fclose(pf);


    //释放资源
    //av_free(buffer);
    av_freep(&pFrameRGB[0]);
    av_free(pFrameRGB);
}

static void pgm_save(unsigned char *buf, int wrap, int xsize, int ysize,
                     char *filename)
{
    FILE *f;
    int i;

    f = fopen(filename,"w");
    fprintf(f, "P5\n%d %d\n%d\n", xsize, ysize, 255);
    for (i = 0; i < ysize; i++)
        fwrite(buf + i * wrap, 1, xsize, f);
    fclose(f);
}

static int decode_write_frame(const char *outfilename, AVCodecContext *avctx,
                              struct SwsContext *img_convert_ctx, AVFrame *frame, AVPacket *pkt)
{
    int ret = -1;
    char buf[1024];

    ret = avcodec_send_packet(avctx, pkt);
    if (ret < 0) {
        fprintf(stderr, "Error while decoding frame, %s(%d)\n", av_err2str(ret), ret);
        return ret;
    }

    while (ret >= 0) {
        fflush(stdout);

	    ret = avcodec_receive_frame(avctx, frame);
        if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
            return 0;
        }else if( ret < 0){
            return -1;
        }

        /* the picture is allocated by the decoder, no need to free it */
        snprintf(buf, sizeof(buf), "%s-%d.bmp", outfilename, avctx->frame_number);
        /*pgm_save(frame->data[0], frame->linesize[0],
                 frame->width, frame->height, buf);*/
        
        saveBMP(img_convert_ctx, frame, 160,  120, buf);
        
    }
    return 0;
}

int main(int argc, char **argv)
{
    int ret;
    int idx;

    const char *filename, *outfilename;

    AVFormatContext *fmt_ctx = NULL;

    const AVCodec *codec = NULL;
    AVCodecContext *ctx = NULL;

    AVStream *inStream = NULL;

    AVFrame *frame = NULL;  
    AVPacket avpkt;

    struct SwsContext *img_convert_ctx;

    if (argc <= 2) {
        fprintf(stderr, "Usage: %s <input file> <output file>\n", argv[0]);
        exit(0);
    }
    filename    = argv[1];
    outfilename = argv[2];

    /* open input file, and allocate format context */
    if (avformat_open_input(&fmt_ctx, filename, NULL, NULL) < 0) {
        fprintf(stderr, "Could not open source file %s\n", filename);
        exit(1);
    }

    /* retrieve stream information */
    if (avformat_find_stream_info(fmt_ctx, NULL) < 0) {
        fprintf(stderr, "Could not find stream information\n");
        exit(1);
    }

    /* dump input information to stderr */
    //av_dump_format(fmt_ctx, 0, filename, 0);

    //av_init_packet(&avpkt);

    /* set end of buffer to 0 (this ensures that no overreading happens for damaged MPEG streams) */
    //memset(inbuf + INBUF_SIZE, 0, AV_INPUT_BUFFER_PADDING_SIZE);
    //

    idx = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if (idx < 0) {
        fprintf(stderr, "Could not find %s stream in input file '%s'\n",
                av_get_media_type_string(AVMEDIA_TYPE_VIDEO), filename);
        return idx;
    }

    inStream = fmt_ctx->streams[idx];

    /* find decoder for the stream */
    codec = avcodec_find_decoder(inStream->codecpar->codec_id);
    if (!codec) {
        fprintf(stderr, "Failed to find %s codec\n",
                av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
        return AVERROR(EINVAL);
    }

    ctx = avcodec_alloc_context3(NULL);
    if (!ctx) {
        fprintf(stderr, "Could not allocate video codec context\n");
        exit(1);
    }

    /* Copy codec parameters from input stream to output codec context */
    if ((ret = avcodec_parameters_to_context(ctx, inStream->codecpar)) < 0) {
        fprintf(stderr, "Failed to copy %s codec parameters to decoder context\n",
                av_get_media_type_string(AVMEDIA_TYPE_VIDEO));
        return ret;
    }

    /* open it */
    if (avcodec_open2(ctx, codec, NULL) < 0) {
        fprintf(stderr, "Could not open codec\n");
        exit(1);
    }

    img_convert_ctx = sws_getContext(ctx->width, ctx->height,
                                     ctx->pix_fmt,
                                     160, 120,
                                     AV_PIX_FMT_BGR24,
                                     SWS_BICUBIC, NULL, NULL, NULL);

    if (img_convert_ctx == NULL)
    {
        fprintf(stderr, "Cannot initialize the conversion context\n");
        exit(1);
    }

    frame = av_frame_alloc();
    if (!frame) {
        fprintf(stderr, "Could not allocate video frame\n");
        exit(1);
    }

    while (av_read_frame(fmt_ctx, &avpkt) >= 0) {
        if(avpkt.stream_index == idx){
            if (decode_write_frame(outfilename, ctx, img_convert_ctx, frame, &avpkt) < 0)
                exit(1);
        }

        av_packet_unref(&avpkt);
    }

    decode_write_frame(outfilename, ctx, img_convert_ctx, frame, NULL);

    avformat_close_input(&fmt_ctx);

    sws_freeContext(img_convert_ctx);
    avcodec_free_context(&ctx);
    av_frame_free(&frame);

    return 0;
}
