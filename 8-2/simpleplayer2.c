#include <libavutil/avutil.h>
#include <libavutil/fifo.h>
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
#include <libswresample/swresample.h>
#include <SDL.h>

#define AUDIO_BUFFER_SIZE 1024

typedef struct _MyPacketEle {
    AVPacket *pkt;
}MyPacketEle;

typedef struct _PacketQueue {
    AVFifo *pkts;
    int nb_packets;
    int size;
    int64_t duration;

    SDL_mutex *mutex;
    SDL_cond *cond;
} PacketQueue;

typedef struct _VideoState {
    AVCodecContext *aCtx;
    AVPacket       *aPkt;
    AVFrame        *aFrame;

    struct SwrContext *swr_ctx;

    uint8_t        *audio_buf;
    uint           audio_buf_size;
    int            audio_buf_index;

    AVCodecContext *vCtx;
    AVPacket       *vPkt;
    AVFrame        *vFrame;

    SDL_Texture    *texture;

    PacketQueue    audioq;
}VideoState;

static int w_width  = 640;
static int w_height = 480;

static SDL_Window   *win = NULL;
static SDL_Renderer *renderer = NULL;

static int packet_queue_init(PacketQueue *q){
    memset(q, 0, sizeof(PacketQueue));
    q->pkts = av_fifo_alloc2(1, sizeof(MyPacketEle), AV_FIFO_FLAG_AUTO_GROW);
    if(!q->pkts){
        return AVERROR(ENOMEM);
    }

    q->mutex = SDL_CreateMutex();
    if(!q->mutex){
        return AVERROR(ENOMEM);
    }

    q->cond = SDL_CreateCond();
    if(!q->cond){
        return AVERROR(ENOMEM);
    }

    return 0;
}

static int packet_queue_put_priv(PacketQueue *q, AVPacket *pkt){
    MyPacketEle mypkt;
    int ret;

    mypkt.pkt = pkt;

    ret = av_fifo_write(q->pkts, &mypkt, 1);
    if(ret < 0)
        return ret;
    
    q->nb_packets++;
    q->size += mypkt.pkt->size + sizeof(mypkt);
    q->duration += mypkt.pkt->duration;

    SDL_CondSignal(q->cond);

    return 0;
}

static int packet_queue_put(PacketQueue *q, AVPacket *pkt){
    AVPacket *pkt1;
    int ret;

    pkt1 = av_packet_alloc();
    if(!pkt1){
        av_packet_unref(pkt);
        return -1;
    }

    av_packet_move_ref(pkt1, pkt);

    SDL_LockMutex(q->mutex);
    //..
    ret = packet_queue_put_priv(q, pkt1);
    SDL_UnlockMutex(q->mutex);

    if(ret < 0){
        av_packet_free(&pkt1);
    }

    return ret;

}

static int packet_queue_get(PacketQueue *q, AVPacket *pkt, int block){
    MyPacketEle mypkt;
    int ret;

    SDL_LockMutex(q->mutex);
    for(;;){
        if(av_fifo_read(q->pkts, &mypkt, 1) >=0 ){
            q->nb_packets--;
            q->size -= mypkt.pkt->size + sizeof(mypkt);
            q->duration -= mypkt.pkt->duration;
            av_packet_move_ref(pkt, mypkt.pkt);
            av_packet_free(&mypkt.pkt);
            ret = 1;
            break;
        } else if (!block){
            ret = 0;
            break;
        } else {
            SDL_CondWait(q->cond, q->mutex);
        }
    }
    SDL_UnlockMutex(q->mutex);

    return ret;
}

static void packet_queue_flush(PacketQueue *q){
    MyPacketEle mypkt;

    SDL_LockMutex(q->mutex);
    while(av_fifo_read(q->pkts, &mypkt, 1) >0 ){
        av_packet_free(&mypkt.pkt);
    }
    q->nb_packets = 0;
    q->size = 0;
    q->duration = 0;

    SDL_UnlockMutex(q->mutex);
}

static void packet_queue_destroy(PacketQueue *q){
    packet_queue_flush(q);
    av_fifo_freep2(&q->pkts);
    SDL_DestroyMutex(q->mutex);
    SDL_DestroyCond(q->cond);
}

static void render(VideoState *is){

    SDL_UpdateYUVTexture(is->texture,
                         NULL,
                         is->vFrame->data[0], is->vFrame->linesize[0],
                         is->vFrame->data[1], is->vFrame->linesize[1],
                         is->vFrame->data[2], is->vFrame->linesize[2]);

    SDL_RenderClear(renderer);
    SDL_RenderCopy(renderer, is->texture, NULL, NULL);
    SDL_RenderPresent(renderer);
}

static int decode(VideoState *is){
    int ret = -1;
    char buf[1024];

    ret = avcodec_send_packet(is->vCtx, is->vPkt);
    if(ret < 0) {
        av_log(NULL, AV_LOG_ERROR, "Failed to send frame to decoder!\n");
        goto __OUT;
    }

    while( ret >= 0){
        ret = avcodec_receive_frame(is->vCtx, is->vFrame);
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

static int audio_decode_frame(VideoState *is){
    int ret;
    int len2;

    int data_size = 0;

    AVPacket pkt;

    for(;;){

        if(packet_queue_get(&is->audioq, &pkt, 1) < 0){
            return -1;
        }

        ret = avcodec_send_packet(is->aCtx, &pkt);
        if(ret < 0){
            av_log(is->aCtx, AV_LOG_ERROR, "Failed to send pkt to audio decoder!\n");
            goto __OUT;
        }

        while(ret >=0 ){
            ret = avcodec_receive_frame(is->aCtx, is->aFrame);
            if(ret == AVERROR(EAGAIN) || ret == AVERROR_EOF){
                break;
            } else if( ret < 0){
                av_log(is->aCtx, AV_LOG_ERROR, "Failed to receive frame from audio decoder!\n");
                goto __OUT;
            }

            if(!is->swr_ctx) {
                AVChannelLayout in_ch_layout, out_ch_layout;
                av_channel_layout_copy(&in_ch_layout, &is->aCtx->ch_layout);
                av_channel_layout_copy(&out_ch_layout, &in_ch_layout);

                if(is->aCtx->sample_fmt != AV_SAMPLE_FMT_S16){
                    swr_alloc_set_opts2(&is->swr_ctx,
                                &out_ch_layout,
                                AV_SAMPLE_FMT_S16,
                                is->aCtx->sample_rate,
                                &in_ch_layout,
                                is->aCtx->sample_fmt,
                                is->aCtx->sample_rate, 
                                0, 
                                NULL);
                    swr_init(is->swr_ctx);
                }
            
            }

            if(is->swr_ctx){
                const uint8_t **in = (const uint8_t **)is->aFrame->extended_data;
                int in_count = is->aFrame->nb_samples;
                uint8_t **out = &is->audio_buf;
                int out_count = is->aFrame->nb_samples + 512;

                int out_size = av_samples_get_buffer_size(NULL, is->aFrame->ch_layout.nb_channels, out_count, AV_SAMPLE_FMT_S16, 0);
                av_fast_malloc(&is->audio_buf, &is->audio_buf_size, out_size);

                len2 = swr_convert(is->swr_ctx,
                            out,
                            out_count,
                            in,
                            in_count);

                data_size = len2 * is->aFrame->ch_layout.nb_channels * av_get_bytes_per_sample(AV_SAMPLE_FMT_S16);
            }else {
                is->audio_buf = is->vFrame->data[0];
                data_size = av_samples_get_buffer_size(NULL, 
                                                       is->aFrame->ch_layout.nb_channels, 
                                                       is->aFrame->nb_samples, 
                                                       is->aFrame->format, 
                                                       1);
            }

            av_packet_unref(&pkt);
            av_frame_unref(is->aFrame);

            return data_size;
        }
    }
__OUT:
    return ret;
}

static void sdl_audio_callback(void *userdata, Uint8 *stream, int len){
    
    int len1 = 0;
    int audio_size = 0;

    VideoState *is = (VideoState*)userdata;

    while(len > 0){
        if(is->audio_buf_index >= is->audio_buf_size) {
            audio_size = audio_decode_frame(is);
            if(audio_size < 0 ){
                is->audio_buf_size = AUDIO_BUFFER_SIZE;
                is->audio_buf = NULL;
            }else {
                is->audio_buf_size = audio_size;
            }
            is->audio_buf_index = 0;
        }

        len1 = is->audio_buf_size - is->audio_buf_index;
        if(len1 > len) {
            len1 = len;
        }

        if(is->audio_buf){
            memcpy(stream, (uint8_t *)is->audio_buf + is->audio_buf_index, len1);
        }else {
            memset(stream, 0, len1);
        }

        len -= len1;
        stream += len1;
        is->audio_buf_index += len1;
    }
}

int main(int argc, char *argv[]){

    int ret = -1;
    int vIdx = -1, aIdx = -1;

    char *src = NULL;

    AVFormatContext *fmtCtx = NULL;
    AVStream *aInStream = NULL;
    AVStream *vInStream = NULL;

    const AVCodec *vDec = NULL;
    AVCodecContext *vCtx = NULL;

    const AVCodec *aDec = NULL;
    AVCodecContext *aCtx = NULL;

    AVPacket *pkt = NULL;

    AVPacket *aPkt = NULL;
    AVFrame *aFrame = NULL;

    AVPacket *vPkt = NULL;
    AVFrame *vFrame = NULL;

    SDL_Texture *texture = NULL;
    SDL_Event event;

    Uint32 pixformat = 0;
    int video_width = 0;
    int video_height = 0;

    VideoState *is = NULL;

    SDL_AudioSpec wanted_spec, spec;

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
    if(SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO)) {
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
    for(int i =0; i < fmtCtx->nb_streams; i++){
        if(fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && 
           vIdx < 0) {
            vIdx = i;
           }

        if(fmtCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO && 
           aIdx < 0) {
            aIdx = i;
           }
        
        if(vIdx > -1 && aIdx > -1){
            break;
        }
    }

    if(vIdx == -1){
        av_log(NULL, AV_LOG_ERROR, "Could not find video stream!\n");
        goto __END;
    }

    if(aIdx == -1){
        av_log(NULL, AV_LOG_ERROR, "Could not find audio stream!\n");
        goto __END;
    }

    aInStream = fmtCtx->streams[aIdx];
    vInStream = fmtCtx->streams[vIdx];
   
    //5. 根据流中的codec_id, 获得解码器
    vDec = avcodec_find_decoder(vInStream->codecpar->codec_id);
    if(!vDec){
        av_log(NULL, AV_LOG_ERROR, "Could not find libx264 Codec");
        goto __END;
    }

    //6. 创建解码器上下文
    vCtx = avcodec_alloc_context3(vDec);
    if(!vCtx){
        av_log(NULL, AV_LOG_ERROR, "NO MEMRORY\n");
        goto __END;
    }
    //7. 从视频流中拷贝解码器参数到解码器上文中
    ret = avcodec_parameters_to_context(vCtx, vInStream->codecpar);
    if(ret < 0){
        av_log(vCtx, AV_LOG_ERROR, "Could not copyt codecpar to codec ctx!\n");
        goto __END;
    }

    //8. 绑定解码器上下文
    ret = avcodec_open2(vCtx, vDec , NULL);
    if(ret < 0) {
        av_log(vCtx, AV_LOG_ERROR, "Don't open codec: %s \n", av_err2str(ret));
        goto __END;
    }
    //9. 根据视频的宽/高创建纹理
    video_width = vCtx->width;
    video_height = vCtx->height;
    pixformat = SDL_PIXELFORMAT_IYUV;
    texture = SDL_CreateTexture(renderer,
                                pixformat,
                                SDL_TEXTUREACCESS_STREAMING,
                                video_width,
                                video_height);

    //10. 根据流中的codec_id, 获得解码器
    aDec = avcodec_find_decoder(aInStream->codecpar->codec_id);
    if(!vDec){
        av_log(NULL, AV_LOG_ERROR, "Could not find libx264 Codec");
        goto __END;
    }

    //6. 创建解码器上下文
    aCtx = avcodec_alloc_context3(aDec);
    if(!aCtx){
        av_log(NULL, AV_LOG_ERROR, "NO MEMRORY\n");
        goto __END;
    }
    //7. 从视频流中拷贝解码器参数到解码器上文中
    ret = avcodec_parameters_to_context(aCtx, aInStream->codecpar);
    if(ret < 0){
        av_log(aCtx, AV_LOG_ERROR, "Could not copyt codecpar to codec ctx!\n");
        goto __END;
    }

    //8. 绑定解码器上下文
    ret = avcodec_open2(aCtx, aDec , NULL);
    if(ret < 0) {
        av_log(aCtx, AV_LOG_ERROR, "Don't open codec: %s \n", av_err2str(ret));
        goto __END;
    }

    packet_queue_init(&is->audioq);

    aPkt = av_packet_alloc();
    aFrame = av_frame_alloc();

    pkt = av_packet_alloc();
    vPkt = av_packet_alloc();
    vFrame = av_frame_alloc();

    //填充 VideoState
    is->texture = texture;
    is->aCtx = aCtx;
    is->aPkt = aPkt;
    is->aFrame = aFrame;

    is->vCtx = vCtx;
    is->vPkt = vPkt;
    is->vFrame = vFrame;

    //为音频设备设置参数
    wanted_spec.freq = aCtx->sample_rate;
    wanted_spec.format = AUDIO_S16SYS;
    wanted_spec.channels = aCtx->ch_layout.nb_channels;
    wanted_spec.silence = 0;
    wanted_spec.samples = AUDIO_BUFFER_SIZE;
    wanted_spec.callback = sdl_audio_callback;
    wanted_spec.userdata = (void*)is;

    if(SDL_OpenAudio(&wanted_spec, &spec) < 0 ){
        av_log(NULL, AV_LOG_ERROR, "Failed to open audio device!\n");
        goto __END;
    }

    SDL_PauseAudio(0);

    //10. 从多媒体文件中读取数据，进行解码
    while(av_read_frame(fmtCtx, pkt) >= 0) {
        if(pkt->stream_index == vIdx) {
            //11. 对解码后的视频帧进行渲染
            av_packet_move_ref(is->vPkt, pkt);
            decode(is);
        }else if(pkt->stream_index == aIdx){
            packet_queue_put(&is->audioq, pkt);
        } else {
            av_packet_unref(pkt);
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
    }

    is->vPkt = NULL;
    decode(is);

__QUIT:
    ret = 0;

__END:
    //13. 收尾，释放资源
    if(vFrame){
        av_frame_free(&vFrame);
    }

    if(vPkt){
        av_packet_free(&vPkt);
    }

    if(aFrame){
        av_frame_free(&aFrame);
    }

    if(aPkt){
        av_packet_free(&aPkt);
    }

    if(pkt){
        av_packet_free(&pkt);
    }

    if(aCtx){
        avcodec_free_context(&aCtx);
    }

    if(vCtx){
        avcodec_free_context(&vCtx);
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

    if(is){
        av_free(is);
    }

    SDL_Quit();

    return ret;
}