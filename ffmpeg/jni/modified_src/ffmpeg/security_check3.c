#include <errno.h>
#include <signal.h>
#include "libavcodec/avcodec.h"
#include "libavutil/adler32.h"
#include "libavformat/avformat.h"
#include "security_check2.h"
#undef fprintf

#ifdef BENCHMARK
#define DBG(fmt,...) 	fprintf(stderr,(fmt),##__VA_ARGS__)
#else
#define DBG(fmt,...)	((void)0)
#endif

typedef int 	bool;
#define true	1
#define false	0

#define PTR_MASK		0x8E87C549
#define ADLER32_SEED 	39916801	// it is a prime number.
#define EI_NIDENT		(16+4)

static unsigned int m_w;
static unsigned int m_z;

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

static bool isDebuggerPresent()
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

/**
 * From http://www.codeproject.com/Articles/25172/Simple-Random-Number-Generation
 */
static inline unsigned int random()
{
	m_z = 36969 * (m_z & 65535) + (m_z >> 16);
	m_w = 18000 * (m_w & 65535) + (m_w >> 16);
	return (m_z << 16) + m_w;
}

static inline bool check( void* baseAddress, int size )
{
	// do not check if debugger attached.
	if( isDebuggerPresent() )
		return true;

	// Skip EI_NIDENT bytes to avoid memory access breakpoint on baseAddress.
	// See http://labmaster.mi.infn.it/Laboratorio2/CompilerCD/clang/l1/ELF.html for ELF header structure.
	unsigned long checksum = av_adler32_update( ADLER32_SEED, (uint8_t*)baseAddress + EI_NIDENT, size - EI_NIDENT );
	
	DBG( "base=%x size=%d checksum=%lu expected=%lu ", baseAddress, size, checksum, (unsigned long)CHKSM_LIBMXVP );
	
	return ((checksum ^ CHKSM_LIBMXVP) * 3 + 39916801) / 3 == 39916801 / 3;	// checksum == CHKSM_LIBMXVP
}

__attribute__((visibility("hidden")))
int security_status = STATUS_NOT_TESTED;

static int calls = 0;

__attribute__((visibility("hidden")))
void security_check( int64_t* combined )
{
#ifdef RELEASE
	if( ++calls != 1000 )
		return;

	int old = __sync_val_compare_and_swap(&security_status, STATUS_NOT_TESTED, STATUS_SUCCESS);
	if( old == STATUS_NOT_TESTED )
	{
		uint32_t ptr = PTR_MASK ^ *(uint32_t*)combined;

		if( check(*(void**)&ptr, (*combined >> 32)) )
		{
			// indication of checking success.
			*combined = 0;
			DBG( "libmxvp.so checksum check succeeded." );
			return;
		}

		security_status = STATUS_FAILURE;
	}

#endif
}


static int intercepted_decode( AVCodecContext *ctx, void *outdata, int *outdata_size, AVPacket *avpkt )
{
	int result = ((int (*)(AVCodecContext*, void*, int*, AVPacket*))ctx->opaque)( ctx, outdata, outdata_size, avpkt );

	void (*func_ptr)( int64_t* ) = GET_FN_PTR(security_check);
	(*func_ptr)( &ctx->vbv_delay );

	if( security_status == STATUS_SUCCESS )
	{
		DBG( "Restored AVCodec 'decode': %x <-- %x", ctx->opaque, ctx->codec->decode );
		ctx->codec->decode = ctx->opaque;
	}
	else if( security_status == STATUS_FAILURE )
		*outdata_size = 0;

	return result;
}

__attribute__((visibility("hidden")))
void setup_intercept( AVCodecContext* ctx )
{
	if( ctx->codec == NULL || ctx->codec->decode == NULL || ctx->codec->decode == intercepted_decode )
		return;

	bool audio = (ctx->codec_type == AVMEDIA_TYPE_AUDIO);
	bool video = (ctx->codec_type == AVMEDIA_TYPE_VIDEO && ctx->thread_type != 0);		// does not intercept thread_type = 0 since it can be a FFService process.

	if( audio == false && video == false )
		return;

	if( isDebuggerPresent() )
		return;

    ctx->opaque = ctx->codec->decode;
   	ctx->codec->decode = intercepted_decode;
   	DBG( "Intercepted AVCodec 'decode': %x --> %x", ctx->opaque, intercepted_decode );

   	// set random seed.
	if( m_w == 0 )
	{
		uint64_t time = (uint64_t)av_gettime();
		m_w = (unsigned int)time;
		m_z = (unsigned int)(time >> 32);
	}
}
