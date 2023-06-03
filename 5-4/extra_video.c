#include <stdio.h>

#include <libavutil/log.h>
#include <libavutil/avutil.h>
#include <libavformat/avformat.h>


// ./extra_video /Users/mesay/Downloads/kyrie_Irving.mp4 1.flv

// h264   只能25帧率   flv mp4 带源信息的  可以根据时间戳计算帧率  看到真实帧率

int main(int argc, char* argv[]){

    // 1. 处理一些参数, 
    char* src;
    char* dst;
    int ret = -1;
    int idx = -1;

    AVFormatContext *pFmtCtx = NULL;
    AVFormatContext *oFmtCtx = NULL;

    const AVOutputFormat *outFmt = NULL;
    AVStream *outStream = NULL;
    AVStream *inStream = NULL;

    AVPacket pkt;




    if(argc < 3){  //argv[0], extra_audio  
        av_log(NULL, AV_LOG_INFO ,"arguments must be more than 3");
        return -1;
    }
    src = argv[1];
    dst = argv[2];
    // 2. 打开多媒体文件
    ret = avformat_open_input(&pFmtCtx, src, NULL, NULL);
    if (ret < 0 )
    {
        av_log(NULL, AV_LOG_ERROR,"%s\n" ,av_err2str(ret));
        exit(-1);
    }
    


    // 3. 从多媒体文件中找到视频文件
    idx = av_find_best_stream(pFmtCtx, AVMEDIA_TYPE_VIDEO, -1 , -1, NULL, 0);
    if (idx < 0 )
    {
         av_log(pFmtCtx, AV_LOG_ERROR,"Does not include audio stream!\n ");
         goto _ERROR;
    }


    // 4. 打开目的文件的上下文avformat_free_context
    oFmtCtx = avformat_alloc_context();
    if (!oFmtCtx)
    {
         av_log(NULL, AV_LOG_ERROR,"NO Memory!\n");
         goto _ERROR;
        /* code */
    }
    outFmt = av_guess_format(NULL, dst, NULL);

    oFmtCtx->oformat =outFmt;


    // 5. 为目的文件,创建一个新的视频流
    outStream = avformat_new_stream(oFmtCtx, NULL);

    // 6. 设置输出视频参数
    inStream = pFmtCtx->streams[idx];
    avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
    outStream->codecpar->codec_tag = 0;

    //  绑定
    ret = avio_open2(&oFmtCtx->pb,dst,AVIO_FLAG_WRITE, NULL, NULL);
    if (ret < 0 )
    {
        av_log(oFmtCtx, AV_LOG_ERROR,"%s\n" ,av_err2str(ret));
         goto _ERROR;
    }

    // 7. 写多媒体文件头到目的文件
    ret = avformat_write_header(oFmtCtx, NULL);
    if (ret < 0 )
    {
        av_log(oFmtCtx, AV_LOG_ERROR,"%s\n" ,av_err2str(ret));
         goto _ERROR;
    }
    // 8. 从源多媒体文件中读到视频数据到目的文件中
    while (av_read_frame(pFmtCtx, &pkt) >= 0)
    {
        if (pkt.stream_index ==idx)
        {
            pkt.pts = av_rescale_q_rnd(pkt.pts, inStream->time_base, outStream->time_base, (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
            pkt.dts = av_rescale_q_rnd(pkt.dts, inStream->time_base,outStream->time_base, (AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));
            pkt.duration = av_rescale_q(pkt.duration,inStream->time_base, outStream->time_base);
            pkt.stream_index = 0;
            pkt.pos = -1;
            av_interleaved_write_frame(oFmtCtx, &pkt);

            av_packet_unref(&pkt);
        }
    }
    
    // 9. 写多媒体文件尾到文件中
    av_write_trailer(oFmtCtx);
    // 10. 将申请的资源释放掉
_ERROR:
    if (pFmtCtx)
    {
        avformat_close_input(&pFmtCtx);
        pFmtCtx = NULL;
    }
    if (oFmtCtx->pb)
    {
        avio_close(oFmtCtx->pb);
    }
    
     if (oFmtCtx)
    {
        avformat_free_context(oFmtCtx);
        oFmtCtx = NULL;
    }

    printf("hello world!\n");
    return 0;
}
