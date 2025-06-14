#pragma once
#include "libavformat/avformat.h"
#include "libavcodec/avcodec.h"
#include "libavutil/adler32.h"

#ifdef BENCHMARK
#undef fprintf
#define DBG(fmt,...) 	fprintf(stderr, "[%s] " fmt, __func__, ##__VA_ARGS__)
#else
#define DBG(fmt,...)	((void)0)
#endif

#define ADLER32_SEED 		 9369319	// it is a prime number.

#define SECOND 	1000
#define MINUTE	(SECOND * 60)
#define HOUR	(MINUTE * 60)
#define DAY		(HOUR * 24)

#ifdef RELEASE

#define DAMAGE_AVFORMAT(fmt,damage_param) \
	if( fmt->pb ) \
	{ \
		DBG( "Making damage." ); \
		fmt->pb->buffer += damage_param; \
		fmt->pb->buf_ptr += damage_param; \
		fmt->pb->buf_end += damage_param; \
	}

#define DAMAGE_AVCODEC(ctx,damage_param) \
	ctx += damage_param;

#define CHECK_AFTER 	HOUR
//#define CHECK_AFTER 	1

#else
#define DAMAGE_AVFORMAT(fmt,damage_param)
#define DAMAGE_AVCODEC(ctx,damage_param)
#define CHECK_AFTER 	1
#endif

/**
 * Check after 3000 calls. It is required not to check while extracting thumbnails.
 */
#define CHECK_AFTER_CALLS 3000

extern void* mxvp_interrupt_callback;
static int mxvp_call_count = 0;

/*
 * Macro for checking modification from AVFormatContext.
 */
#define SECURITY_CHECK() \
	const uint8_t volatile_1* base = mxvp_interrupt_callback - OFFSET_MediaPlayer_interruptCallback; \
	volatile_2 unsigned long checksum = ADLER32_SEED; \
\
	const uint8_t volatile_3* init_main = base + OFFSET_init_main; \
	checksum = av_adler32_update( checksum, (const uint8_t*)init_main, LEN_init_main ); \
\
	const uint8_t volatile_4* Verifier_initialize = base + OFFSET_Verifier_initialize; \
	checksum = av_adler32_update( checksum, (const uint8_t*)Verifier_initialize, LEN_Verifier_initialize ); \
\
	const uint8_t volatile_5* setDataSource = base + OFFSET_setDataSource; \
	checksum = av_adler32_update( checksum, (const uint8_t*)setDataSource, LEN_setDataSource ); \
\
	const uint8_t volatile_6* VideoDevice_ctor = base + OFFSET_VideoDevice_ctor; \
	checksum = av_adler32_update( checksum, (const uint8_t*)VideoDevice_ctor, LEN_VideoDevice_ctor ); \
\
	DBG( "init_main=%x (%d) Verifier_initialize=%x (%d) setDataSource=%x (%d) VideoDevice_ctor=%x (%d) checksum=%lu expected=%lu ", init_main, LEN_init_main, Verifier_initialize, LEN_Verifier_initialize, setDataSource, LEN_setDataSource, VideoDevice_ctor, LEN_VideoDevice_ctor, checksum, (unsigned long)REMOTE_CHKSM ); \
\
	if( checksum == (unsigned long)REMOTE_CHKSM ) \
	{ \
		DBG( "Checksum checking succeeded." ); \
		goto __end; \
	} \
\
	DBG( "Checksum NOT matched." );


// interrupt-callback will not be provided in some cases such as playing back .m3u8 playlist. 
#define SECURITY_CHECK_AVFORMAT(__fmt,damage_param) \
{ \
	AVFormatContext* fmt = (__fmt); \
	const uint8_t* interruptCallback = (const uint8_t*)fmt->interrupt_callback.callback; \
	if( interruptCallback ) \
	{ \
		DBG( "mxvp_interrupt_callback is provided. callback=%x opaque=%x", fmt->interrupt_callback.callback, fmt->interrupt_callback.opaque ); \
		mxvp_interrupt_callback = (void*)((int(*)(void*))interruptCallback)(NULL); \
	} \
	else \
		DBG( "AVFormatContext.interrupt_callback is null." ); \
}

#define SECURITY_INIT_AVCODECCONTEXT(ctx,damage_param) \
	mxvp_call_count = 0;

// Codec can be initialized by ffmpeg internally. In this case, opaque is null.
// It can typically happen with mpeg2-ts.
#define SECURITY_CHECK_AVCODECCONTEXT(ctx,damage_param) \
{ \
	if( ++mxvp_call_count == CHECK_AFTER_CALLS ) \
	{ \
		if( mxvp_interrupt_callback ) \
		{ \
			SECURITY_CHECK(); \
		} \
		else \
			DBG( "interrupt callback is not provided." ); \
\
		DAMAGE_AVCODEC( ctx, (damage_param) ); \
	} \
\
__end: \
	; \
}
