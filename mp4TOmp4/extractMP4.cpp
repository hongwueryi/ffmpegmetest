#include "stdafx.h"
//��ȡ��Ƶ������Ƶ��
extern "C" {
//#include "libavutil/log.h"
#include "libavformat/avio.h"
#include "libavformat/avformat.h"
}

#define ERROR_STR_SIZE 1024
#define SHOW_VEDIO 1
int main(int argc, char *argv[]) {
	int err_code;
	char errors[1024];

	char *src_filename = NULL;
	char *dst_filename = NULL;

	int audio_stream_index;
	int vedio_stream_index;

	//������
	AVFormatContext *fmt_ctx = NULL;
	AVFormatContext *ofmt_ctx = NULL;

	//֧�ָ��ָ���������ļ���ʽ��MP4��FLV��3GP�ȵ�
	AVOutputFormat *output_fmt = NULL;

	AVStream *in_stream = NULL;
	AVStream *out_stream = NULL;

	AVPacket pkt;

	av_log_set_level(AV_LOG_DEBUG);

	if (argc < 3) {
		av_log(NULL, AV_LOG_DEBUG, "argc < 3��\n");
		return -1;
	}

	src_filename = argv[1];
	dst_filename = argv[2];

	if (src_filename == NULL || dst_filename == NULL) {
		av_log(NULL, AV_LOG_DEBUG, "src or dts file is null!\n");
		return -1;
	}


	if ((err_code = avformat_open_input(&fmt_ctx, src_filename, NULL, NULL)) < 0) {
		av_strerror(err_code, errors, 1024);
		av_log(NULL, AV_LOG_DEBUG, "�������ļ�ʧ��: %s, %d(%s)\n",
			src_filename,
			err_code,
			errors);
		return -1;
	}

	if ((err_code = avformat_find_stream_info(fmt_ctx, NULL)) < 0) {
		av_strerror(err_code, errors, 1024);
		av_log(NULL, AV_LOG_DEBUG, "failed to find stream info: %s, %d(%s)\n",
			src_filename,
			err_code,
			errors);
		return -1;
	}

	av_dump_format(fmt_ctx, 0, src_filename, 0);

	if (fmt_ctx->nb_streams < 2) {
		//����С��2��˵������ļ���Ƶ����Ƶ�������������ܱ�֤�������ļ��д���
		av_log(NULL, AV_LOG_ERROR, "�����ļ�����������2��\n");
		exit(1);
	}

	//�õ��ļ�����Ƶ��
	/**ֻ��Ҫ�޸�����AVMEDIA_TYPE_VIDEO����**/
#if SHOW_VEDIO
	vedio_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_VIDEO, -1, -1, NULL, 0);
	if (vedio_stream_index < 0) {
		av_log(NULL, AV_LOG_DEBUG, " ��ȡ��Ƶ��ʧ��%s,%s\n",
			av_get_media_type_string(AVMEDIA_TYPE_VIDEO),
			src_filename);
		return AVERROR(EINVAL);
	}
	in_stream = fmt_ctx->streams[vedio_stream_index];
#else
	audio_stream_index = av_find_best_stream(fmt_ctx, AVMEDIA_TYPE_AUDIO /*AVMEDIA_TYPE_VIDEO*/, -1, -1, NULL, 0);
	if (audio_stream_index < 0) {
		av_log(NULL, AV_LOG_DEBUG, " ��ȡ��Ƶ��ʧ��%s,%s\n",
			av_get_media_type_string(AVMEDIA_TYPE_AUDIO),
			src_filename);
		return AVERROR(EINVAL);
	}
	in_stream = fmt_ctx->streams[audio_stream_index];
#endif
	

	
	//������Ϣ
	AVCodecParameters *in_codecpar = in_stream->codecpar;


	// ���������
	ofmt_ctx = avformat_alloc_context();

	//����Ŀ���ļ����������ʺϵ��������
	output_fmt = av_guess_format(NULL, dst_filename, NULL);
	if (!output_fmt) {
		av_log(NULL, AV_LOG_DEBUG, "����Ŀ�������������ʧ�ܣ�\n");
		exit(1);
	}

	ofmt_ctx->oformat = output_fmt;

	//�½������
	out_stream = avformat_new_stream(ofmt_ctx, NULL);
	if (!out_stream) {
		av_log(NULL, AV_LOG_DEBUG, "���������ʧ�ܣ�\n");
		exit(1);
	}

	// ��������Ϣ������������У�����ֻ�ǳ�ȡ��Ƶ������������Ƶ������������ֻ��Copy
	if ((err_code = avcodec_parameters_copy(out_stream->codecpar, in_codecpar)) < 0) {
		av_strerror(err_code, errors, ERROR_STR_SIZE);
		av_log(NULL, AV_LOG_ERROR,
			"�����������ʧ�ܣ�, %d(%s)\n",
			err_code, errors);
	}

	out_stream->codecpar->codec_tag = 0;

	//��ʼ��AVIOContext,�ļ������������
	if ((err_code = avio_open(&ofmt_ctx->pb, dst_filename, AVIO_FLAG_WRITE)) < 0) {
		av_strerror(err_code, errors, 1024);
		av_log(NULL, AV_LOG_DEBUG, "�ļ���ʧ�� %s, %d(%s)\n",
			dst_filename,
			err_code,
			errors);
		exit(1);
	}



	av_dump_format(ofmt_ctx, 0, dst_filename, 1);


	//��ʼ�� AVPacket�� ���Ǵ��ļ��ж��������ݻ��ݴ�������
	av_init_packet(&pkt);
	pkt.data = NULL;
	pkt.size = 0;


	// дͷ����Ϣ
	if (avformat_write_header(ofmt_ctx, NULL) < 0) {
		av_log(NULL, AV_LOG_DEBUG, "д��ͷ����Ϣʧ�ܣ�");
		exit(1);
	}

	//ÿ����һ֡����
	while (av_read_frame(fmt_ctx, &pkt) >= 0) {
#if SHOW_VEDIO
		if (pkt.stream_index == vedio_stream_index) {
#else
		if (pkt.stream_index == audio_stream_index) {
#endif
			pkt.pts = av_rescale_q_rnd(pkt.pts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF);
				
			pkt.dts = av_rescale_q_rnd(pkt.dts, in_stream->time_base, out_stream->time_base, AV_ROUND_NEAR_INF);
				//(AV_ROUND_NEAR_INF | AV_ROUND_PASS_MINMAX));

			pkt.duration = av_rescale_q(pkt.duration, in_stream->time_base, out_stream->time_base);
			pkt.pos = -1;
			pkt.stream_index = 0;
			//����д�����ý���ļ�
			av_interleaved_write_frame(ofmt_ctx, &pkt);
			//�������ü����������ڴ�й©
			av_packet_unref(&pkt);
		}
	}

	//дβ����Ϣ
	av_write_trailer(ofmt_ctx);

	//���������ͷ��ڴ�
	avformat_close_input(&fmt_ctx);
	avio_close(ofmt_ctx->pb);

	return 0;
}
