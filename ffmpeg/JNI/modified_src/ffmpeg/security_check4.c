#include <errno.h>
#include <signal.h>
#include "libavcodec/avcodec.h"
#include "libavutil/adler32.h"
#include "libavformat/avformat.h"
#include "security_check4.h"
#undef fprintf

#ifdef BENCHMARK
#define DBG(fmt,...) 	fprintf(stderr,(fmt),##__VA_ARGS__)
#else
#define DBG(fmt,...)	((void)0)
#endif

typedef int 	bool;
#define true	1
#define false	0

#define ADLER32_SEED 		3276509	// it is a prime number.

#define STATUS_NOT_TESTED 	0
#define STATUS_SUCCESS 		1
#define STATUS_FAILURE 		-1

#if 0
/*
 * Debugger detection.
 *
 * @see http://etutorials.org/Programming/secure+programming/Chapter+12.+Anti-Tampering/12.13+Detecting+Unix+Debuggers/
 */
static int num_traps = 0;

static void dbg_trap(int signo)
{
	++num_traps;
}

static inline bool isDebuggerPresent()
{
	int before = num_traps;

	if( signal(SIGTRAP, dbg_trap) == SIG_ERR )
	{
		DBG( "signal(SIGTRAP) failed. errno=%d", errno );
		return true;
	}

	raise( SIGTRAP );

	if( before < num_traps )
		return false;

	DBG( "A debugger was found; SIGTRAP was not captured. num_traps=%d", num_traps );
	return true;
}
#endif

static bool check( const uint8_t* interruptCallback )
{
	if( interruptCallback == NULL )
	{
		DBG( "Interrupt callback is not provided." );
		return false;
	}

#if 0
	if( isDebuggerPresent() )
	{
		DBG( "Halted security checking since debugging trap found." );
		return true;
	}
#endif

	// test interruptcallback provider.
	int baseTime = ((int(*)(void*))interruptCallback)(NULL);
	if( baseTime == 0 )
	{
		DBG( "InterruptCallback is not from the MediaPlayer." );
		return true;
	}

#define SECOND 	1000
#define MINUTE	(SECOND * 60)
#define HOUR	(MINUTE * 60)
#define DAY		(HOUR * 24)

	// Don't check for first 1 hour.
	if( baseTime > 0 && baseTime < HOUR )
	{
		DBG( "Checking is skept since base time is too small: %dm", baseTime / MINUTE );
		return true;
	}

	// accumulate each sections
	const uint8_t* base = interruptCallback - OFFSET_MediaPlayer_interruptCallback;
	unsigned long checksum = ADLER32_SEED;

	const uint8_t* init_main = base + OFFSET_init_main;
	checksum = av_adler32_update( checksum, (const uint8_t*)init_main, LEN_init_main );

	const uint8_t* Verifier_initialize = base + OFFSET_Verifier_initialize;
	checksum = av_adler32_update( checksum, (const uint8_t*)Verifier_initialize, LEN_Verifier_initialize );

	const uint8_t* setDataSource = base + OFFSET_setDataSource;
	checksum = av_adler32_update( checksum, (const uint8_t*)setDataSource, LEN_setDataSource );

	const uint8_t* VideoDevice_ctor = base + OFFSET_VideoDevice_ctor;
	checksum = av_adler32_update( checksum, (const uint8_t*)VideoDevice_ctor, LEN_VideoDevice_ctor );

	DBG( "init_main=%x (%d) Verifier_initialize=%x (%d) setDataSource=%x (%d) VideoDevice_ctor=%x (%d) checksum=%lu expected=%lu ",
			init_main, LEN_init_main, Verifier_initialize, LEN_Verifier_initialize, setDataSource, LEN_setDataSource, VideoDevice_ctor, LEN_VideoDevice_ctor,
			checksum, (unsigned long)REMOTE_CHKSM );

	if( checksum != (unsigned long)REMOTE_CHKSM )
	{
		DBG( "Checksum NOT matched." );
		return false;
	}

	DBG( "Checksum checking succeeded." );
	return true;
}

__attribute__((visibility("hidden")))
void security_check( AVFormatContext* ctx )
{
	DBG( "Checking security: interrupt_callback.callback=%x interrupt_callback.opaque=%x", ctx->interrupt_callback.callback, ctx->interrupt_callback.opaque );

	if( check((const uint8_t*)ctx->interrupt_callback.callback) == false )
	{
#ifdef RELEASE
		// makes buffer overflow.
		if( ctx->pb )
		{
			DBG( "Making damage." );
			ctx->pb->buffer += 0x4000;
			ctx->pb->buf_ptr += 0x4000;
			ctx->pb->buf_end += 0x4000;
		}
#endif
	}
}
