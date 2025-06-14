#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <linux/stat.h>
#include <stdio.h>
#include "libavcodec/avcodec.h"
#include "libavutil/adler32.h"
#include "security_check.h"
#include "mxvp_checksum.h"

static const uint8_t __demap__[0x80] = { 39,51,61,36,9,60,72,54,4,116,70,120,10,3,103,28,7,95,19,18,102,27,35,63,26,58,32,69,24,16,104,50,126,20,65,21,96,115,100,122,1,86,108,2,93,38,82,17,79,88,48,13,99,76,75,59,109,14,121,12,53,114,41,74,106,57,83,33,89,67,97,101,6,5,77,125,8,92,43,68,111,119,107,90,66,94,117,113,105,98,49,40,87,81,73,84,85,124,46,127,110,80,56,112,44,71,15,45,62,31,78,118,11,0,37,23,30,123,29,25,34,64,47,55,42,91,52,22, };

static const uint8_t libmxvp_mxvp_pro[] = { 122,38,70,137,70,250,38,70,137,70,250,52,80,56,226,56,11,137,71,180,158,226,239,88,38,71,208,231,42,70,186,71,189,98,103,189,80,122,42,216,89,122,170,88,89,184,11,111,103,226,165,208, };
static const uint8_t libmxvp_mxvp_ad[] = { 122,38,70,137,70,122,38,198,9,70,250,180,80,184,226,184,139,137,71,180,158,98,239,216,38,199,208,231,170,70,186,71,189,98,198,38,122,170,88,89,250,42,216,217,184,11,111,103,226,165,80, };
static const uint8_t libmxvp_ffmpeg_v5te[] = { 250,38,70,9,198,122,38,198,137,70,122,180,208,184,226,184,11,137,199,180,158,98,20,148,184,231,71,14,226,111,188,137,199,122,170,216,89,122,42,216,217,56,139,239,231,98,37,208, };
static const uint8_t libmxvp_ffmpeg_v6[] = { 122,166,70,137,70,122,166,70,9,70,250,52,80,184,226,184,11,9,199,180,30,226,148,20,184,231,199,14,226,239,7,122,170,216,89,122,170,216,217,184,139,239,103,226,37,208, };
static const uint8_t libmxvp_ffmpeg_v6_vfp[] = { 122,38,198,9,70,250,166,198,9,70,250,180,80,56,226,56,11,9,199,180,30,226,148,148,56,103,71,142,98,111,135,17,239,20,231,250,42,216,89,250,42,88,89,184,139,111,103,226,165,208, };
static const uint8_t libmxvp_ffmpeg_v7_neon[] = { 250,166,70,9,70,250,166,198,137,70,122,180,80,184,226,56,139,9,199,180,158,226,148,20,56,231,199,14,226,239,251,17,100,71,208,228,122,170,88,89,122,170,88,89,184,139,239,103,226,165,208, };
static const uint8_t libmxvp_ffmpeg_v7[] = { 122,38,70,137,198,250,38,198,9,70,250,52,208,56,98,184,139,137,71,52,30,226,20,20,56,231,71,142,226,239,251,17,111,20,231,111,129,166,90,7,250,170,216,217,250,170,88,217,184,11,111,231,98,37,208, };

#define USES_SS \
		char __sec_buffer__[64];

#define SS(key)	(getString(key, sizeof(key), __sec_buffer__), __sec_buffer__)

static void getString( const uint8_t* mapped, int length, char* buffer )
{
	int i;
	for( i = 0; i < length; ++i )
		*buffer++ = (char)__demap__[*mapped++ & ~0x80];

	*buffer = 0;
}

#ifdef BENCHMARK
#define DBG(fmt,...) 	fprintf(stderr,(fmt),##__VA_ARGS__)
#else
#define DBG(fmt,...)	((void)0)
#endif

/**
 * @param libPath  
 * 
 * @return 0 on success. -1 on failure.
 */
static int do_security_check( const char* libPath, void* returnAddress, unsigned long expectedChecksum )
{
	int fd, size, result = -1;
	void* ptr = NULL;
	unsigned long checksum;
	
	fd = open( libPath, O_RDONLY );
	if( fd < 0 )
	{
		DBG( "open(%s) - errno=%d", libPath, errno );
		return -1;
	}
	
	size = lseek( fd, 0, SEEK_END );
	if( size <= 0 )
	{
		DBG( "lseek(%s) - errno=%d", libPath, errno );
		goto __quit;
	}
	
	ptr = mmap( 0, size, PROT_READ, MAP_SHARED, fd, 0 );
	if( ptr ==  MAP_FAILED )
	{
		DBG( "mmap(%s) - errno=%d", libPath, errno );
		goto __quit;
	}
	
	// File will not be mapped on the same address.
#if 0
	// check return address
	if( returnAddress != 0 )
	{
		if( returnAddress < ptr || returnAddress >= (uint8_t*)ptr + size )
		{
			DBG( "return-address=%x begin=%x end=%x", returnAddress, ptr, (uint8_t*)ptr + size ); 
			goto __quit;
		}
	}
#endif
	
	// test checksum
	checksum = av_adler32_update( 1, ptr, size );
	
	DBG( "checksum=%d expected=%d", checksum, expectedChecksum );
	result = (checksum == expectedChecksum);
	
__quit:
	if( ptr )
		munmap( ptr, size );
	
	close( fd );
	return result;
}

__attribute__((visibility("hidden")))
void security_check( int flags, void* returnAddress )
{
	USES_SS;
	int result = -1;
	
#ifdef HAVE_NEON
	if( flags == FROM_PRO )
		result = do_security_check( SS(libmxvp_mxvp_pro), returnAddress, CHKSM_NEON  );
	else if( flags == FROM_AD )
		result = do_security_check( SS(libmxvp_mxvp_ad), returnAddress, CHKSM_NEON  );
	else if( flags == FROM_SEPARATE_CODEC )
		result = do_security_check( SS(libmxvp_ffmpeg_v7_neon), returnAddress, CHKSM_NEON  );
#elif defined(HAVE_ARMV7A)
	result = do_security_check( SS(libmxvp_ffmpeg_v7), returnAddress, CHKSM_TEGRA2 );
#elif defined(HAVE_ARMVFP)
	result = do_security_check( SS(libmxvp_ffmpeg_v6_vfp), returnAddress, CHKSM_ARMV6_VFP );
#elif defined(HAVE_ARMV6)
	result = do_security_check( SS(libmxvp_ffmpeg_v6), returnAddress, CHKSM_ARMV6 );
#else
	result = do_security_check( SS(libmxvp_ffmpeg_v5te), returnAddress, CHKSM_ARMV5TE );
#endif
	
	if( result == 0 )
		return;

	// remove codecs except first one.
	av_codec_next(NULL)->next = NULL;
}
