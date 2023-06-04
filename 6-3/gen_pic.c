#include <stdio.h>
#include <libavutil/log.h>
#include <libavutil/avutil.h>
#include <libavutil/imgutils.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswscale/swscale.h>


// ./gen_pic ../test.mp4 out

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
} BITMAPFILEHEADER, *LPBITMAPFILEHEADER, *PBITMAPFILEHEADER;

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
} BITMAPINFOHEADER, *LPBITMAPINFOHEADER, *PBITMAPINFOHEADER;

static void saveBMP(struct SwsContext *swsCtx, 
                    AVFrame *frame, 
                    int w, 
                    int h,
                    char *name){
                        FILE *f = NULL;
                        int dataSize = w * h * 3;

                        //1. 先进行转换，将YUV frame 转成  BGR24 Frame
                        AVFrame *frameBGR= av_frame_alloc();
                        frameBGR->width = w;
                        frameBGR->height = h;
                        frameBGR->format = AV_PIX_FMT_BGR24;

                        av_frame_get_buffer(frameBGR, 0);

                        sws_scale(swsCtx, 
                                  (const uint8_t * const *)frame->data,
                                  frame->linesize,
                                  0,
                                  frame->height,
                                  frameBGR->data, 
                                  frameBGR->linesize);
                        //2. 构造 BITMAPINFOHEADER
                        BITMAPINFOHEADER infoHeader;
                        infoHeader.biSize = sizeof(BITMAPINFOHEADER);
                        infoHeader.biWidth = w;
                        infoHeader.biHeight = h * (-1);
                        infoHeader.biBitCount = 24;
                        infoHeader.biCompression = 0;
                        infoHeader.biSizeImage = 0;
                        infoHeader.biClrImportant = 0;
                        infoHeader.biClrUsed = 0;
                        infoHeader.biXPelsPerMeter = 0;
                        infoHeader.biYPelsPerMeter = 0;
                        infoHeader.biPlanes = 1;

                        //3. 构造 BITMAPFILEHEADER
                        BITMAPFILEHEADER fileHeader = {0, };
                        fileHeader.bfType = 0x4d42; //'BM'
                        fileHeader.bfSize = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER) + dataSize;
                        fileHeader.bfOffBits = sizeof(BITMAPFILEHEADER) + sizeof(BITMAPINFOHEADER);
        
                        //4. 将数据写到文件
                        f = fopen(name, "wb");
                        fwrite(&fileHeader, sizeof(BITMAPFILEHEADER), 1, f);
                        fwrite(&infoHeader, sizeof(BITMAPINFOHEADER), 1, f);
                        fwrite(frameBGR->data[0], 1, dataSize, f);
                       
                        //5. 释放资源
                         fclose(f);
                         av_freep(&frameBGR->data[0]);
                         av_free(frameBGR);
                    }

static void savePic(unsigned char *buf, int linesize, int width, int height, char *name){
    FILE *f;
    
    f = fopen(name, "wb");
    fprintf(f, "P5\n%d %d\n%d\n", width, height, 255);
    for(int i=0; i<height; i++){
        fwrite(buf+ i * linesize, 1, width, f);
    }
    fclose(f);
}

static int decode(AVCodecContext *ctx, 
                  struct SwsContext *swsCtx, 
                  AVFrame *frame, AVPacket *pkt, 
                  const char* fileName){
    int ret = -1;

    char buf[1024];

    ret = avcodec_send_packet(ctx, pkt);
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to send frame to decoder!\n");
        goto _END; 
    }

    while( ret >= 0){
        ret = avcodec_receive_frame(ctx, frame);
        if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
            return 0;
        } else if( ret < 0) {
            return -1; //退出程序
        }
        snprintf(buf, sizeof(buf), "%s-%lld.bmp", fileName, (long long)pkt->pts);

        saveBMP(swsCtx, frame, 640, 360, buf);
        /*  这里没做改变前 保存黑白图片
        savePic(frame->data[0],
                frame->linesize[0],
                frame->width,
                frame->height,
                buf);*/
        if(pkt) {
            av_packet_unref(pkt);
        }
        
    }
_END:
    return 0;
}

int main(int argc, char *argv[]){

    int ret = -1;
    int idx = -1;

    //1. 处理一些参数；
    char* src;
    char* dst;

    const AVCodec *codec = NULL;
    AVCodecContext *ctx = NULL;

    AVFormatContext *pFmtCtx = NULL;

    AVStream *inStream = NULL;

    AVFrame *frame = NULL;
    AVPacket *pkt = NULL;

    struct SwsContext *swsCtx = NULL;

    av_log_set_level(AV_LOG_DEBUG);
    if(argc < 3){ //argv[0], extra_audio 
        av_log(NULL, AV_LOG_INFO, "arguments must be more than 3!\n");
        exit(-1);
    }

    src = argv[1];
    dst = argv[2];

    //2. 打开多媒体文件
    if((ret = avformat_open_input(&pFmtCtx, src, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "%s\n", av_err2str(ret));
        exit(-1);
    }

    //3. 从多媒体文件中找到视频流
    idx = av_find_best_stream(pFmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if(idx < 0) {
        av_log(pFmtCtx, AV_LOG_ERROR, "Does not include audio stream!\n");
        goto _ERROR;
    }

    inStream = pFmtCtx->streams[idx];

    //4. 查找解码器
    codec = avcodec_find_decoder(inStream->codecpar->codec_id);
    if(!codec){
        av_log(NULL, AV_LOG_ERROR, "Could not find libx264 Codec");
        goto _ERROR;
    }

    //5. 创建解码器上下文
    ctx = avcodec_alloc_context3(NULL);
    if(!ctx){
        av_log(NULL, AV_LOG_ERROR, "NO MEMRORY\n");
        goto _ERROR;
    }

    ret = avcodec_parameters_to_context(ctx, inStream->codecpar);
    if(ret < 0){
        av_log(ctx, AV_LOG_ERROR, "Could not copyt codecpar to codec ctx!\n");
        goto _ERROR;
    }

     //5. 解码器与解码器上下文绑定到一起
    ret = avcodec_open2(ctx, codec , NULL);
    if(ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Don't open codec: %s \n", av_err2str(ret));
        goto _ERROR;
    }

    //5.1 获得SWS上下文
    swsCtx = sws_getContext(ctx->width,  //src width
                            ctx->height, //src height
                            AV_PIX_FMT_YUV420P,//src pix fmt
                            640,  //dst width
                            360, //dst height
                            AV_PIX_FMT_BGR24, //dst pix fmt
                            SWS_BICUBIC, NULL, NULL, NULL);
    if(!swsCtx){
        av_log(NULL, AV_LOG_ERROR, "Could not get Swscale Context!\n");
        goto _ERROR;
    }

     //6. 创建AVFrame
    frame = av_frame_alloc();
    if(!frame){
        av_log(NULL, AV_LOG_ERROR, "NO MEMORY!\n");
        goto _ERROR;
    }

    //7. 创建AVPacket
    pkt = av_packet_alloc();
    if(!pkt){
        av_log(NULL, AV_LOG_ERROR, "NO MEMORY!\n");
        goto _ERROR;
    }

    //8. 从源多媒体文件中读到视频数据
    while(av_read_frame(pFmtCtx, pkt) >= 0) {
        if(pkt->stream_index == idx) {
            decode(ctx, swsCtx, frame, pkt, dst);
        }
    }
    decode(ctx, swsCtx, frame, NULL, dst);

    //9. 将申请的资源释放掉
_ERROR:
    if(pFmtCtx){
        avformat_close_input(&pFmtCtx);
        pFmtCtx = NULL;
    }

    if(ctx){
        avcodec_free_context(&ctx);
        ctx = NULL;
    }

    if(frame){
        av_frame_free(&frame);
        frame = NULL;
    }

    if(pkt){
        av_packet_free(&pkt);
        pkt = NULL;
    }

    if(swsCtx){
        sws_freeContext(swsCtx);
        swsCtx = NULL;
    }
   
    printf("hello, world!\n");
    return 0;
}
