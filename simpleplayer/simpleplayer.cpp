//仅仅是简单的播放视频，视频是通过主线程中阻塞40ms播放，音频在子线程创建
//后续优化音视频同步
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
//Windows
extern "C"
{
#include "libavcodec/avcodec.h"
#include "libavformat/avformat.h"
#include "libswresample/swresample.h"
#include "libswscale/swscale.h"
#include "libavutil/imgutils.h"
#include "SDL2/SDL.h"
};
#else
//Linux...
#ifdef __cplusplus
extern "C"
{
#endif
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libswresample/swresample.h>
#include <SDL2/SDL.h>
#ifdef __cplusplus
};
#endif
#endif


#define MAX_AUDIO_FRAME_SIZE 192000 // 1 second of 48khz 32bit audio

//Use SDL
#define USE_SDL 1
AVCodecContext	*pCodecCtx;
AVCodec			*pCodec;

//Buffer:
//|-----------|-------------|
//chunk-------pos---len-----|
static  Uint8  *audio_chunk;
static  Uint32  audio_len;
static  Uint8  *audio_pos;

/* The audio function callback takes the following parameters:
* stream: A pointer to the audio buffer to be filled
* len: The length (in bytes) of the audio buffer
*/
void  fill_audio(void *udata, Uint8 *stream, int len) {
	//SDL 2.0
	SDL_memset(stream, 0, len);
	if (audio_len == 0)
		return;

	len = (len>audio_len ? audio_len : len);	/*  Mix  as  much  data  as  possible  */

	SDL_MixAudio(stream, audio_pos, len, SDL_MIX_MAXVOLUME);
	audio_pos += len;
	audio_len -= len;
}
//-----------------

int audio_thread_proc(void *opaque)
{
	if (avcodec_open2(pCodecCtx, pCodec, NULL)<0) {
		printf("Could not open codec.\n");
		return -1;
	}
	return 0;
}
int main(int argc, char* argv[])
{
	AVFormatContext	*pFormatCtx;
	int				i, audioStream, videostream;
	AVPacket		*packet;
	uint8_t			*out_buffer;
	AVFrame			*pFrame;
	int ret;
	uint32_t len = 0;
	int got_picture;
	int index = 0;
	int64_t in_channel_layout;
	struct SwrContext *au_convert_ctx;

	FILE *pFile = NULL;
	char url[] = "../ring.mp4";
									
	avformat_network_init();
	pFormatCtx = avformat_alloc_context();
	//Open
	if (avformat_open_input(&pFormatCtx, url, NULL, NULL) != 0) {
		printf("Couldn't open input stream.\n");
		return -1;
	}
	// Retrieve stream information
	if (avformat_find_stream_info(pFormatCtx, NULL)<0) {
		printf("Couldn't find stream information.\n");
		return -1;
	}
	// Dump valid information onto standard error
	av_dump_format(pFormatCtx, 0, url, false);

	// Find the first audio stream
	videostream = audioStream = -1;
	for (i = 0; i < pFormatCtx->nb_streams; i++)
	{
		if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_AUDIO) {
			audioStream = i;
		}
		else if (pFormatCtx->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO) {
			videostream = i;
		}
	}
		
	if (audioStream == -1 || videostream == -1) {
		printf("Didn't find a audio stream.\n");
		return -1;
	}

////////////////////////////////VIDEO////////////////////////////////////////////////////
	AVCodec* pVideoCodec = avcodec_find_decoder(pFormatCtx->streams[videostream]->codecpar->codec_id);
	if (pVideoCodec == NULL)
	{
		return -1;
	}

	AVCodecContext* pVideoCtx = avcodec_alloc_context3(pVideoCodec);
	avcodec_parameters_to_context(pVideoCtx, pFormatCtx->streams[videostream]->codecpar);

	if (avcodec_open2(pVideoCtx, pVideoCodec, NULL) < 0)
	{
		return -1;
	}

	SwsContext *img_convert_ctx;
	img_convert_ctx = sws_getContext(pVideoCtx->width, pVideoCtx->height, AV_PIX_FMT_YUV420P,
		pVideoCtx->width, pVideoCtx->height, AV_PIX_FMT_YUV420P, SWS_BICUBIC, NULL, NULL, NULL);

	AVFrame* pFrameYUV = av_frame_alloc();
	unsigned char *video_out_buffer = (unsigned char *)av_malloc(
		av_image_get_buffer_size(AV_PIX_FMT_YUV420P, pVideoCtx->width, pVideoCtx->height, 1));
	av_image_fill_arrays(pFrameYUV->data, pFrameYUV->linesize, 
		video_out_buffer, AV_PIX_FMT_YUV420P, pVideoCtx->width, pVideoCtx->height, 1);

//////////////////////////////////////AUDIO/////////////////////////////////////////////

	// Find the decoder for the audio stream
	pCodec = avcodec_find_decoder(pFormatCtx->streams[audioStream]->codecpar->codec_id);
	if (pCodec == NULL) {
		printf("Codec not found.\n");
		return -1;
	}

	// Get a pointer to the codec context for the audio stream
	pCodecCtx = avcodec_alloc_context3(pCodec);
	avcodec_parameters_to_context(pCodecCtx, pFormatCtx->streams[audioStream]->codecpar);

	// Open codec
	/*if (avcodec_open2(pCodecCtx, pCodec, NULL)<0) {
		printf("Could not open codec.\n");
		return -1;
	}*/
	SDL_Thread* audiothread = SDL_CreateThread(audio_thread_proc, NULL, NULL);

	//Out Audio Param
	uint64_t out_channel_layout = AV_CH_LAYOUT_4POINT0;  //AV_CH_LAYOUT_STEREO;
														 //nb_samples: AAC-1024 MP3-1152
	int out_nb_samples = pCodecCtx->frame_size;
	AVSampleFormat out_sample_fmt = AV_SAMPLE_FMT_S16;
	int out_sample_rate = 48000;
	int out_channels = av_get_channel_layout_nb_channels(out_channel_layout);
	//Out Buffer Size
	int out_buffer_size = av_samples_get_buffer_size(NULL, out_channels, out_nb_samples, out_sample_fmt, 1);

	out_buffer = (uint8_t *)av_malloc(MAX_AUDIO_FRAME_SIZE * 2);
	pFrame = av_frame_alloc();


////////////////////////////////SDL////////////////////////////////////////////////
#if USE_SDL
	//Init
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER)) {
		printf("Could not initialize SDL - %s\n", SDL_GetError());
		return -1;
	}
	//SDL 2.0 Support for multiple windows
	int screen_w = pVideoCtx->width;
	int screen_h = pVideoCtx->height;
	SDL_Window* screen = SDL_CreateWindow("Simplest ffmpeg player's Window", SDL_WINDOWPOS_UNDEFINED, SDL_WINDOWPOS_UNDEFINED,
		screen_w, screen_h,
		SDL_WINDOW_OPENGL);

	if (!screen) {
		printf("SDL: could not create window - exiting:%s\n", SDL_GetError());
		return -1;
	}

	SDL_Renderer* sdlRenderer = SDL_CreateRenderer(screen, -1, 0);
	SDL_Texture* sdlTexture = SDL_CreateTexture(sdlRenderer, SDL_PIXELFORMAT_IYUV, SDL_TEXTUREACCESS_STREAMING, pVideoCtx->width, pVideoCtx->height);
	SDL_Rect sdlRect;
	sdlRect.x = 0;
	sdlRect.y = 0;
	sdlRect.w = screen_w;
	sdlRect.h = screen_h;

	//SDL_AudioSpec
	SDL_AudioSpec wanted_spec;
	wanted_spec.freq = out_sample_rate;
	wanted_spec.format = AUDIO_S16SYS;
	wanted_spec.channels = out_channels;
	wanted_spec.silence = 0;
	wanted_spec.samples = out_nb_samples;
	wanted_spec.callback = fill_audio;
	wanted_spec.userdata = pCodecCtx;

	if (SDL_OpenAudio(&wanted_spec, NULL)<0) {
		printf("can't open audio.\n");
		return -1;
	}

#endif


	//FIX:Some Codec's Context Information is missing
	in_channel_layout = av_get_default_channel_layout(pCodecCtx->channels);
	//Swr

	au_convert_ctx = swr_alloc();
	au_convert_ctx = swr_alloc_set_opts(au_convert_ctx, out_channel_layout, out_sample_fmt, out_sample_rate,
		in_channel_layout, pCodecCtx->sample_fmt, pCodecCtx->sample_rate, 0, NULL);
	swr_init(au_convert_ctx);

	//Play
	SDL_PauseAudio(0);


	packet = new AVPacket();
	av_init_packet(packet);

	AVFrame *pAudioFrame = av_frame_alloc();
	AVFrame *pVideoFrame = av_frame_alloc();
	while (av_read_frame(pFormatCtx, packet) >= 0) 
	{
		if (packet->stream_index == videostream)
		{
			// 时间基转换(等同于SDL_Delay(40);)
			AVStream *in_stream = pFormatCtx->streams[packet->stream_index];
			AVRational raw_video_time_base = av_inv_q(pVideoCtx->framerate);
			av_packet_rescale_ts(packet, in_stream->time_base, raw_video_time_base);

			int ret = avcodec_send_packet(pVideoCtx, packet);
			if (ret < 0)
			{
				return -1;
			}
			while (ret >= 0)
			{
				ret = avcodec_receive_frame(pVideoCtx, pVideoFrame);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
					break;
				else if (ret < 0)
					return 0;
				if (ret >= 0)
				{
					printf("video dts=%d, pts=%d", packet->dts, packet->pts);
					sws_scale(img_convert_ctx, (const unsigned char* const*)pVideoFrame->data,
						pVideoFrame->linesize, 0, pVideoCtx->height, pFrameYUV->data, pFrameYUV->linesize);
					SDL_UpdateTexture(sdlTexture, NULL, pFrameYUV->data[0], pFrameYUV->linesize[0]);
					SDL_RenderClear(sdlRenderer);
					//SDL_RenderCopy( sdlRenderer, sdlTexture, &sdlRect, &sdlRect );  
					SDL_RenderCopy(sdlRenderer, sdlTexture, NULL, NULL);
					SDL_RenderPresent(sdlRenderer);
					//SDL_Delay(40);
				}
			}
		}
		if (packet->stream_index == audioStream)
		{

			int ret = avcodec_send_packet(pCodecCtx, packet);
			if (ret < 0)
			{
				return -1;
			}
			while (ret >= 0)
			{
				ret = avcodec_receive_frame(pCodecCtx, pAudioFrame);
				if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
					break;
				else if (ret < 0)
					return 0;

				if (ret >= 0)
				{
					printf("audio dts=%d, pts=%d", packet->dts, packet->pts);
					swr_convert(au_convert_ctx, &out_buffer, MAX_AUDIO_FRAME_SIZE, (const uint8_t **)pAudioFrame->data, pAudioFrame->nb_samples);
					printf("index:%5d\t pts:%lld\t packet size:%d\n", index, packet->pts, packet->size);
					index++;
				}
			}

			while (audio_len>0)//Wait until finish
				SDL_Delay(1);

			//Set audio buffer (PCM data)
			audio_chunk = (Uint8 *)out_buffer;
			//Audio buffer length
			audio_len = out_buffer_size;
			audio_pos = audio_chunk;

		}

	}

	//swr_free(&au_convert_ctx);
	sws_freeContext(img_convert_ctx);
#if USE_SDL
	SDL_CloseAudio();//Close SDL
	SDL_Quit();
#endif

	delete packet;
	av_free(out_buffer);
	avcodec_close(pCodecCtx);
	avformat_close_input(&pFormatCtx);

	return 0;
}


