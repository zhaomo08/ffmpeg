#include <stdio.h>

#include <stdlib.h>
#include <libavutil/log.h>
#include <libavutil/avutil.h>
#include <libavformat/avformat.h>

int main(int argc, char* argv[]){

    // 1. 处理一些参数, 
    char* src;
    char* dst;

    int *stream_map = NULL; 

    int64_t *dts_start_time = NULL;
    int64_t *pts_start_time = NULL;

    int i = 0;
    int ret = -1;
    int idx = -1;
    int stream_idx = 0;

    double starttime;
    double endtime;



    AVFormatContext *pFmtCtx = NULL;
    AVFormatContext *oFmtCtx = NULL;

    const AVOutputFormat *outFmt = NULL;


    AVPacket pkt;



    // cut src  dst  start  end 

    if(argc < 5){  //argv[0], extra_audio  
        av_log(NULL, AV_LOG_INFO ,"arguments must be more than 3");
        return -1;
    }
    src = argv[1];
    dst = argv[2];
    starttime = atof(argv[3]);
    endtime = atof(argv[4]);

    // 2. 打开多媒体文件
    ret = avformat_open_input(&pFmtCtx, src, NULL, NULL);
    if (ret < 0 )
    {
        av_log(NULL, AV_LOG_ERROR,"%s\n" ,av_err2str(ret));
        exit(-1);
    }
    


    // 4. 打开目的文件的上下文avformat_free_context
    avformat_alloc_output_context2(&oFmtCtx, NULL, NULL, dst);
    if (!oFmtCtx)
    {
        av_log(oFmtCtx, AV_LOG_ERROR,"NO MEMORY!\n");
        goto _ERROR;
    }
    
    stream_map = av_calloc(pFmtCtx->nb_streams, sizeof(int));
    if (!stream_map)
    {
        av_log(oFmtCtx, AV_LOG_ERROR,"NO MEMORY!\n");
        goto _ERROR;

    }
    
    for(i=0; i < pFmtCtx->nb_streams; i++){
        AVStream *outStream = NULL;
        AVStream *inStream = pFmtCtx->streams[i];
        AVCodecParameters *inCodercPar = inStream->codecpar;
        if (inCodercPar->codec_type != AVMEDIA_TYPE_AUDIO && 
        inCodercPar->codec_type != AVMEDIA_TYPE_VIDEO  &&
        inCodercPar->codec_type != AVMEDIA_TYPE_SUBTITLE)
        {
            stream_map[i] = -1;
            continue;
        }
        stream_map[i] = stream_idx++;
         // 5. 为目的文件,创建一个新的流

        outStream = avformat_new_stream(oFmtCtx, NULL);

        if (!outStream){
             av_log(oFmtCtx, AV_LOG_ERROR,"NO MEMORY!\n");
            goto _ERROR;
        }
        avcodec_parameters_copy(outStream->codecpar, inStream->codecpar);
        outStream->codecpar->codec_tag = 0;
    }

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

    // seek 
    ret = av_seek_frame(pFmtCtx, -1, starttime*AV_TIME_BASE, AVSEEK_FLAG_BACKWARD);
    if (ret < 0 )
    {
        av_log(oFmtCtx, AV_LOG_ERROR,"%s\n" ,av_err2str(ret));
        goto _ERROR;
    }

    dts_start_time = av_calloc(pFmtCtx->nb_streams, sizeof(int64_t));
    for (int t = 0; t < pFmtCtx->nb_streams; t++)
    {
        dts_start_time[t] = -1;
    }
    
    pts_start_time = av_calloc(pFmtCtx->nb_streams, sizeof(int64_t));
    for (int t = 0; t < pFmtCtx->nb_streams; t++)
    {
        pts_start_time[t] = -1;
    }
    


    // 8. 从源多媒体文件中读到数据到目的文件中
    while (av_read_frame(pFmtCtx, &pkt) >= 0)
    
    {
        AVStream *inStream,*outStream;


        if (dts_start_time[pkt.stream_index] == -1  && pkt.dts > 0)
        {
            dts_start_time[pkt.stream_index] = pkt.dts;
        }
        if (pts_start_time[pkt.stream_index] == -1  && pkt.pts > 0)
        {
            pts_start_time[pkt.stream_index] = pkt.pts;
        }
        
        inStream = pFmtCtx->streams[pkt.stream_index];


        if (av_q2d(inStream->time_base) * pkt.pts > endtime)
        {
            av_log(oFmtCtx, AV_LOG_INFO,"success\n");
            break;
        }
        

        if (stream_map[pkt.stream_index] < 0 ){
            av_packet_unref(&pkt);
            continue;
        }

        pkt.pts =pkt.pts - pts_start_time[pkt.stream_index];
        pkt.dts =pkt.dts - dts_start_time[pkt.stream_index];
        if (pkt.dts > pkt.pts)
        {
            pkt.pts = pkt.dts;
        }
        

    
        pkt.stream_index = stream_map[pkt.stream_index];
        outStream = pFmtCtx->streams[pkt.stream_index];
        av_packet_rescale_ts(&pkt,inStream->time_base,outStream->time_base); 


        pkt.pos = -1;
        av_interleaved_write_frame(oFmtCtx, &pkt);
        av_packet_unref(&pkt);

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
     if (stream_map)
    {
       av_free(stream_map);
    }


    if (dts_start_time)
    {
        av_free(dts_start_time);
    }
     if (pts_start_time)
    {
        av_free(pts_start_time);
    }
    
    printf("hello world!\n");
    return 0;
}
