// mp4TOmp4.cpp : 定义控制台应用程序的入口点。
//

#include "stdafx.h"

extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libavutil/avutil.h"
#include "libswscale/swscale.h"
#include "libavutil/opt.h"
}

//const char* SRC_FILE = "taitan.mkv";   //有bug，待调整
const char* SRC_FILE = "../taitan.mp4";
const char* OUT_FILE = "outfile.h264";
const char* OUT_FMT_FILE = "outfmtfile.mp4";
int main()
{
	AVFormatContext* pFormat = NULL;
	if (avformat_open_input(&pFormat, SRC_FILE, NULL, NULL) < 0)
	{
		return 0;
	}
	AVCodecContext* video_dec_ctx = NULL;
	AVCodec* video_dec = NULL;
	if (avformat_find_stream_info(pFormat, NULL) < 0)
	{
		return 0;
	}
	
	int video_stream_index = 0;
	int index = 0;
	for (index = 0; index < pFormat->nb_streams; index++)
	{
		if (pFormat->streams[index]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
		{
			video_stream_index = index;
			break;
		}
	}

	video_dec = avcodec_find_decoder(pFormat->streams[video_stream_index]->codecpar->codec_id);
	video_dec_ctx = avcodec_alloc_context3(video_dec);
	int ret = avcodec_parameters_to_context(video_dec_ctx, pFormat->streams[video_stream_index]->codecpar);
	if (ret < 0)
	{
		return 0;
	}

	av_dump_format(pFormat, 0, SRC_FILE, 0);
	if (avcodec_open2(video_dec_ctx, video_dec, NULL) < 0)
	{
		return 0;
	}

	AVFormatContext* pOFormat = NULL;
	AVOutputFormat* ofmt = NULL;
	if (avformat_alloc_output_context2(&pOFormat, NULL, NULL, OUT_FILE) < 0)
	{
		return 0;
	}

	ofmt = pOFormat->oformat;

	if (avio_open(&(pOFormat->pb), OUT_FILE, AVIO_FLAG_READ_WRITE) < 0)
	{
		return 0;
	}

	AVCodec *video_enc = NULL;
	video_enc = avcodec_find_encoder(AV_CODEC_ID_H264);
	AVCodecContext* video_enc_ctx = avcodec_alloc_context3(video_enc);
	if (NULL == video_enc_ctx)
	{
		return 0;
	}
	AVStream *video_st = avformat_new_stream(pOFormat, video_enc);
	if (!video_st)
		return 0;

	avcodec_parameters_from_context(video_st->codecpar, video_dec_ctx);
	avcodec_parameters_to_context(video_enc_ctx, video_st->codecpar);

	video_enc_ctx->width = video_dec_ctx->width;
	video_enc_ctx->height = video_dec_ctx->height;
	video_enc_ctx->pix_fmt = AV_PIX_FMT_YUV420P;
	video_enc_ctx->time_base.num = 1;
	video_enc_ctx->time_base.den = 25;
	video_enc_ctx->bit_rate = video_dec_ctx->bit_rate;
	video_enc_ctx->gop_size = 250;
	video_enc_ctx->max_b_frames = 10;
	video_enc_ctx->qmin = 10;
	video_enc_ctx->qmax = 51;
	if (avcodec_open2(video_enc_ctx, video_enc, NULL) < 0)
	{
		printf("编码器打开失败！\n");
		return 0;
	}

	printf("Output264video Information====================\n");
	av_dump_format(pOFormat, 0, OUT_FILE, 1);
	printf("Output264video Information====================\n");

	//mp4 file
	AVFormatContext* pMp4Format = NULL;
	AVOutputFormat* pMp4OFormat = NULL;
	if (avformat_alloc_output_context2(&pMp4Format, NULL, NULL, OUT_FMT_FILE) < 0)
	{
		return 0;
	}
	pMp4OFormat = pMp4Format->oformat;
	if (avio_open(&(pMp4Format->pb), OUT_FMT_FILE, AVIO_FLAG_READ_WRITE) < 0)
	{
		return 0;
	}

	for (int i = 0; i < pFormat->nb_streams; i++) {
		AVCodecContext* CodecCtx = NULL;
		AVCodec* codec = avcodec_find_decoder(pFormat->streams[i]->codecpar->codec_id);
		if (NULL == codec)
		{
			return 0;
		}
		CodecCtx = avcodec_alloc_context3(codec);
		if (NULL == CodecCtx)
		{
			return 0;
		}
		avcodec_parameters_to_context(CodecCtx, pFormat->streams[i]->codecpar);

		AVStream *out_stream = avformat_new_stream(pMp4Format, CodecCtx->codec);
		if (!out_stream) {
			return 0;
		}

		avcodec_parameters_from_context(out_stream->codecpar, CodecCtx);


		AVCodecContext* newstreamCodecCtx = avcodec_alloc_context3(CodecCtx->codec);
		if (NULL == newstreamCodecCtx)
		{
			return 0;
		}
		avcodec_parameters_to_context(newstreamCodecCtx, out_stream->codecpar);
		out_stream->codecpar->codec_tag = 0;
		if (pMp4Format->oformat->flags & AVFMT_GLOBALHEADER)
		{
			newstreamCodecCtx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
		}		
	}

	
	av_dump_format(pMp4Format, 0, OUT_FMT_FILE, 1);

	if (avformat_write_header(pMp4Format, NULL) < 0)
	{
		return 0;
	}

	av_opt_set(video_enc_ctx->priv_data, "preset", "superfast", 0);
	av_opt_set(video_enc_ctx->priv_data, "tune", "zerolatency", 0);
	if (avformat_write_header(pOFormat, NULL) < 0)
	{
		return 0;
	}

	AVPacket *pkt = new AVPacket();
	av_init_packet(pkt);
	AVFrame *pFrame = av_frame_alloc();
	int ts = 0;
	while (1)
	{
		if (av_read_frame(pFormat, pkt) < 0)
		{
			//avio_close(pOFormat->pb);
			av_write_trailer(pMp4Format);
			avio_close(pMp4Format->pb);
			delete pkt;
			return 0;
		}
		if (pkt->stream_index == AVMEDIA_TYPE_VIDEO)
		{
			ret = avcodec_send_packet(video_dec_ctx, pkt);
			if (ret < 0) 
			{
				return 0;
			}
			while (ret >= 0) {
				ret = avcodec_receive_frame(video_dec_ctx, pFrame);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF) {
					break;
				}
				else if (ret < 0) {
					return 0;  //end处进行资源释放等善后处理
				}
				if (ret >= 0) {
					//pFrame->pts = pFrame->pkt_pts;//ts++;
					AVPacket *tmppkt = new AVPacket;
					av_init_packet(tmppkt);
					int size = video_enc_ctx->width*video_enc_ctx->height * 3 / 2;
					char* buf = new char[size];
					memset(buf, 0, size);
					tmppkt->data = (uint8_t*)buf;
					tmppkt->size = size;

					int ret2 = avcodec_send_frame(video_enc_ctx, pFrame);
					if (ret2 < 0)
					{
						return 0;
					}

					int got_packet = avcodec_receive_packet(video_enc_ctx, tmppkt);
					if (!got_packet) {
						AVStream *in_stream = pFormat->streams[pkt->stream_index];
						AVStream *out_stream = pMp4Format->streams[pkt->stream_index];

						tmppkt->pts = av_rescale_q_rnd(tmppkt->pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF);
						tmppkt->dts = av_rescale_q_rnd(tmppkt->dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF);
						tmppkt->duration = av_rescale_q(tmppkt->duration, in_stream->time_base, out_stream->time_base);
						tmppkt->pos = -1;
						ret2 = av_interleaved_write_frame(pMp4Format, tmppkt);
						if (ret2 < 0)
							return 0;
						delete tmppkt;
						delete buf;
					}
					else {
						ret2 = 0;
					}
					if (ret2 < 0)
					{
						return 0;
					}
				}
			}
		}
		else if (pkt->stream_index == AVMEDIA_TYPE_AUDIO)
		{
			AVStream *in_stream = pFormat->streams[pkt->stream_index];
			AVStream *out_stream = pMp4Format->streams[pkt->stream_index];

			pkt->pts = av_rescale_q_rnd(pkt->pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF);
			pkt->dts = av_rescale_q_rnd(pkt->dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF);
			pkt->duration = av_rescale_q(pkt->duration, in_stream->time_base, out_stream->time_base);
			pkt->pos = -1;
			if (av_interleaved_write_frame(pMp4Format, pkt) < 0)
				return 0;
		}
	}
	av_frame_free(&pFrame);
	return 0;
}
