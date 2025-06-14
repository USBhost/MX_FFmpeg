/*
 * 16개의 color table을 guess하여 저장한다. 모든 opaque color가 caching된 경우에만 저장된 color table을 사용한다.
 * subtitle은 frame이 많지 않으므로 매번 guess하더라도 성능차이는 크지 않다.
 */
/*
 * 이 방식의 단점은 subtitle이 일반적인 pattern이 아닌 특수한 pattern으로 사용되었는데 그것이 palette로 저장되는 경우
 * 전체 자막이 잘못된 색으로 나온다는 것이다. colormap으로 1:1 mapping하는 편이 낳다.
 *
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <exception>
#include <new>

#ifdef BENCHMARK
#include <time.h>
#include <sys/time.h>

static int clock( clock_t clk_id ) throw()
{
    struct timespec t;

    if( clock_gettime(clk_id, &t) == 0 )
    	return t.tv_sec * 1000 + t.tv_nsec / 1000000;
    else
    	return 0;
}

static int uptime() throw()
{
	return clock( CLOCK_MONOTONIC );
}

#endif


using namespace std;


struct DVDSubContext2
{
	static const uint32_t UNUSED_COLOR = 0xFFFFFFFF;

	// Alpha bits are 0.
	uint32_t palette[16];
	
	DVDSubContext2()
	{
		memset( palette, 0xFF, sizeof(palette) );
	}

	void guess_palette( uint32_t *rgba_palette, uint8_t* colormap, uint8_t* alpha, uint8_t *bitmap, int w, int h );

	/*
	 * dvdalpha has range of 0 - 15.
	 */
	static uint32_t combine( uint32_t rgb, uint8_t dvdalpha ) throw()
	{
		return rgb | ((dvdalpha * 17) << 24);
	}

	void set( uint32_t* rgba_palette, uint8_t* colormap, uint8_t* alphamap, uint32_t rgba, uint8_t index ) throw()
	{
		rgba_palette[index] = combine( rgba, alphamap[index] );
		palette[colormap[index]] = rgba;
	}
};

static_assert( false, "color finding is defected. watch devsubdec1.cpp" );

void DVDSubContext2::guess_palette( uint32_t *rgba_palette, uint8_t* colormap, uint8_t* alpha, uint8_t *bitmap, int w, int h )
{
	static const uint32_t RGB_BACK			= 0x00000000;
	static const uint32_t RGB_TEXT 			= 0x00FFFFFF;
	static const uint32_t RGB_DARK_BORDER	= 0x00000000;
	static const uint32_t RGB_LIGHT_BORDER	= 0x00808080;
	
    // count opaque colors on color map.
    int nb_opaque_count = 0, nb_unknown = 0;
    for( int i = 0; i < 4; i++ )
    {
        if( alpha[i] )
        {
        	++nb_opaque_count;

        	uint32_t color = palette[colormap[i]];
        	if( color == UNUSED_COLOR )
        	{
        		rgba_palette[i] = 0;
        		++nb_unknown;
        	}
        	else
        		rgba_palette[i] = combine( color, alpha[i] );
        }
        else
        	rgba_palette[i] = 0;
    }

    if( nb_unknown == 0 || nb_opaque_count == 0 )
        return;

#ifdef BENCHMARK
    int begin = uptime();
#endif

    // find colors by appearing order.
    uint8_t index[4];
    int nb_index = 0;

    for( int y = 0; y < h ; ++y )
    {
    	uint8_t* px = bitmap + y * w;
    	uint8_t* end = px + w;

    	for( ; px != end; ++px )
    	{
    		uint8_t ii = *px;

    		if( alpha[ii] == 0 )
    			continue;

			for( int i = 0; i < nb_index; ++i )
			{
				if( index[i] == ii )
					goto color_exits;
			}

			index[nb_index++] = ii;

			if( nb_index == nb_opaque_count )
				goto all_colors_found;

    	color_exits:
    		;
    	}
    }

all_colors_found:
	switch( nb_index )
	{
	case 1:
		set( rgba_palette, colormap, alpha, RGB_TEXT, 			index[0] );
		break;

	case 2:
		set( rgba_palette, colormap, alpha, RGB_DARK_BORDER,	index[0] );
		set( rgba_palette, colormap, alpha, RGB_TEXT, 			index[1] );
		break;

	case 3:
		set( rgba_palette, colormap, alpha, RGB_DARK_BORDER, 	index[0] );
		set( rgba_palette, colormap, alpha, RGB_LIGHT_BORDER,	index[1] );
		set( rgba_palette, colormap, alpha, RGB_TEXT, 			index[2] );
		break;

	case 4:
		set( rgba_palette, colormap, alpha, RGB_BACK, 			index[0] );
		set( rgba_palette, colormap, alpha, RGB_DARK_BORDER, 	index[1] );
		set( rgba_palette, colormap, alpha, RGB_LIGHT_BORDER,	index[2] );
		set( rgba_palette, colormap, alpha, RGB_TEXT, 			index[3] );
		break;
	}

#ifdef BENCHMARK
	fprintf( stderr, "%dms were spent to guess dvdsub palette with colormap %d %d %d %d / alpha %d %d %d %d. unknown-colors=%d opaque-colors=%d found-colors=%d",
			uptime()-begin, colormap[0], colormap[1], colormap[2], colormap[3], alpha[0], alpha[1], alpha[2], alpha[3], nb_unknown, nb_opaque_count, nb_index );
#endif
}


extern "C" {

DVDSubContext2* dvdsub2_init()
{
	try
	{
		return new DVDSubContext2();
	}
	catch( bad_alloc& e )
	{
		fputs( "DVDSubContext2 creation failed with std::bad_alloc", stderr );
		return NULL;
	}
}

void dvdsub2_uninit( DVDSubContext2* ctx )
{
	delete ctx;
}

void dvdsub2_guess_palette( DVDSubContext2* ctx, uint32_t *rgba_palette, uint8_t* colormap, uint8_t* alpha, uint8_t *bitmap, int w, int h )
{
	if( ctx == NULL )
		return;

	try
	{
		ctx->guess_palette( rgba_palette, colormap, alpha, bitmap, w, h );
	}
	catch( exception& e )
	{
		fprintf( stderr, "DVDSubContext2::guess_palette() failed with %s", e.what() );
	}
}

}
