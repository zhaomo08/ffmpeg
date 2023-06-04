#include <libavutil/avutil.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <SDL.h>

typedef struct _VideoState {
    AVCodecContext *avctx;
    AVPacket       *pkt;
    AVFrame        *frame;

    SDL_Texture    *texture;
}VideoState;

static int w_width  = 640;
static int w_height = 480;

static SDL_Window   *win = NULL;
static SDL_Renderer *renderer = NULL;

static void render(VideoState *is){

    SDL_UpdateYUVTexture(is->texture,
                         NULL,
                         is->frame->data[0], is->frame->linesize[0],
                         is->frame->data[1], is->frame->linesize[1],
                         is->frame->data[2], is->frame->linesize[2]);

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, is->texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

static int decode(VideoState *is){
    int ret = -1;
    char buf[1024];

    ret = avcodec_send_packet(is->avctx, is->pkt);
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to send frame to decoder!\n");
        goto __OUT;
    }

    while( ret >= 0){
        ret = avcodec_receive_frame(is->avctx, is->frame);
        if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
            ret = 0;
            goto __OUT;
        } else if( ret < 0) {
            ret = -1; //退出程序
            goto __OUT;
        }
        render(is);
    }
__OUT:
    return ret;
}

int main(int argc, char *argv[]){

    int ret = -1;
    int idx = -1;

    char *src = NULL;

    AVFormatContext *fmtCtx = NULL;
    AVStream *inStream = NULL;

    const AVCodec *dec = NULL;
    AVCodecContext *ctx = NULL;

    AVPacket *pkt = NULL;
    AVFrame *frame = NULL;

    SDL_Texture *texture = NULL;
    SDL_Event event;

    Uint32 pixformat = 0;
    int video_width = 0;
    int video_height = 0;

    VideoState *is = NULL;

    av_log_set_level(AV_LOG_DEBUG);

    //1. 判断输入参数
    if(argc < 2){ //argv[0], simpleplayer, argv[1] src 
        av_log(NULL, AV_LOG_INFO, "arguments must be more than 2!\n");
        exit(-1);
    }

    src = argv[1];

    is = av_mallocz(sizeof(VideoState));
    if(!is){
        av_log(NULL, AV_LOG_ERROR, "NO MEMORY!\n");
        goto __END;
    }

    //2. 初始化SDL，并创建窗口和Render
    //2.1
    if(SDL_Init(SDL_INIT_VIDEO)) {
        fprintf( stderr, "Could not initialize SDL - %s\n", SDL_GetError());
        return -1;
    }
    //2.2 creat window from SDL
    win = SDL_CreateWindow("Simple Player",
                           SDL_WINDOWPOS_UNDEFINED,
                           SDL_WINDOWPOS_UNDEFINED,
                           w_width, w_height,
                           SDL_WINDOW_OPENGL|SDL_WINDOW_RESIZABLE);
    if(!win) {
        fprintf(stderr, "Failed to create window, %s\n",SDL_GetError());
        goto __END;
    }

    renderer = SDL_CreateRenderer(win, -1, 0);

    //3. 打开多媒体文件，并获得流信息
    if((ret = avformat_open_input(&fmtCtx, src, NULL, NULL)) < 0) {
        av_log(NULL, AV_LOG_ERROR, "%s\n", av_err2str(ret));
        goto __END;
    }

    if((ret = avformat_find_stream_info(fmtCtx, NULL)) < 0 ){
         av_log(NULL, AV_LOG_ERROR, "%s\n", av_err2str(ret));
         goto __END;
    }

    //4. 查找最好的视频流
    idx = av_find_best_stream(fmtCtx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
    if(idx < 0) {
        av_log(fmtCtx, AV_LOG_ERROR, "Does not include audio stream!\n");
        goto __END;
    }

    //5. 根据流中的codec_id, 获得解码器
    inStream = fmtCtx->streams[idx];
    dec = avcodec_find_decoder(inStream->codecpar->codec_id);
    if(!dec){
        av_log(NULL, AV_LOG_ERROR, "Could not find libx264 Codec");
        goto __END;
    }

    //6. 创建解码器上下文
    ctx = avcodec_alloc_context3(dec);
    if(!ctx){
        av_log(NULL, AV_LOG_ERROR, "NO MEMRORY\n");
        goto __END;
    }
    //7. 从视频流中拷贝解码器参数到解码器上文中
    ret = avcodec_parameters_to_context(ctx, inStream->codecpar);
    if(ret < 0){
        av_log(ctx, AV_LOG_ERROR, "Could not copyt codecpar to codec ctx!\n");
        goto __END;
    }

    //8. 绑定解码器上下文
    ret = avcodec_open2(ctx, dec , NULL);
    if(ret < 0) {
        av_log(ctx, AV_LOG_ERROR, "Don't open codec: %s \n", av_err2str(ret));
        goto __END;
    }
    //9. 根据视频的宽/高创建纹理
    video_width = ctx->width;
    video_height = ctx->height;
    pixformat = SDL_PIXELFORMAT_IYUV;
    texture = SDL_CreateTexture(renderer,
                                pixformat,
                                SDL_TEXTUREACCESS_STREAMING,
                                video_width,
                                video_height);

    pkt = av_packet_alloc();
    frame = av_frame_alloc();

    is->texture = texture;
    is->avctx = ctx;
    is->pkt = pkt;
    is->frame = frame;

    //10. 从多媒体文件中读取数据，进行解码
    while(av_read_frame(fmtCtx, pkt) >= 0) {
        if(pkt->stream_index == idx) {
            //11. 对解码后的视频帧进行渲染
            decode(is);
        }
        //12. 处理SDL事件
        SDL_PollEvent(&event);
        switch(event.type) {
            case SDL_QUIT:
                goto __QUIT;
                break;
            default:
                break;
        } 
        av_packet_unref(pkt);
    }

    is->pkt = NULL;
    decode(is);

__QUIT:
    ret = 0;

__END:
    //13. 收尾，释放资源
    if(frame){
        av_frame_free(&frame);
    }

    if(pkt){
        av_packet_free(&pkt);
    }

    if(ctx){
        avcodec_free_context(&ctx);
    }

    if(fmtCtx){
        avformat_close_input(&fmtCtx);
    }

    if(win){
        SDL_DestroyWindow(win);
    }

    if(renderer){
        SDL_DestroyRenderer(renderer);
    }

    if(texture){
        SDL_DestroyTexture(texture);
    }

    SDL_Quit();

    return ret;
}