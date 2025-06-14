/*
 * 16개의 color table을 직접 guess하는 방식은, 전체 최대 4개의 colormap중 하나라도 안 쓰이는 색이 있으면  guess가 안되어 다음번 호출 시 다시 bitmap을 scan하여야 하므로 사용하지 않는다.
 * 다만, 현재 방식의 경우 최초 bitmap에서 안 쓰이는 색이 이후에 쓰이게 된다면 black으로 표시되는 문제가 있다.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "libavutil/mem.h"
#include "libavformat/avformat.h"
#undef fprintf

#ifdef BENCHMARK
#define DBG(fmt,...) 	fprintf(stderr,(fmt),##__VA_ARGS__)
#else
#define DBG(fmt,...)	((void)0)
#endif


struct RGBPalette
{
	uint32_t 				colors[4];
};

struct ColorAlphaMap
{
	uint32_t				colors;
	uint32_t				alphas;
};

struct Entry
{
	struct ColorAlphaMap	key;
	struct RGBPalette		value;
};

struct DVDSubContext2
{
	int						nb_entry;		// number of entry in the palette cache cache
	struct Entry		   *entries;		// palette cache entry.
};

typedef uint8_t bool;
static const bool true = 1;
static const bool false = 0;

/*
 * dvdalpha has range of 0 - 15.
 */
static uint32_t combine( uint32_t rgb, uint8_t dvdalpha )
{
	return rgb | ((dvdalpha * 17) << 24);
}

static const uint32_t RGB_BACK			= 0x00000000;
static const uint32_t RGB_TEXT 			= 0x00FFFFFF;
static const uint32_t RGB_DARK_BORDER	= 0x00000000;
static const uint32_t RGB_LIGHT_BORDER	= 0x00808080;

static void guess_palette( struct DVDSubContext2* ctx, uint32_t *rgba_palette, uint8_t* colormap, uint8_t* alpha, uint8_t *bitmap, int w, int h )
{
	int i, y;

	uint32_t colormap32 = *(uint32_t*)colormap;
	uint32_t alphamap32 = *(uint32_t*)alpha;

	// search the cache
	for( i = 0; i < ctx->nb_entry; ++i )
	{
		struct ColorAlphaMap key = ctx->entries[i].key;

		if( key.colors == colormap32 && key.alphas == alphamap32 )
		{
	    	memcpy( rgba_palette, &ctx->entries[i].value, sizeof(ctx->entries[i].value) );

	    	DBG( "DVDSubContext2 - matching cache entry is found. #%d/%d", i, ctx->nb_entry );
	    	return;
		}
	}

#ifdef BENCHMARK
    int64_t begin = av_gettime();
#endif

    for( i = 0; i < 4; ++i )
    	rgba_palette[i] = 0;

    // count opaque colors on color map.
    bool color_used[16];
    memset( color_used, false, sizeof(color_used) );

    int nb_opaque_colors = 0;
    for( i = 0; i < 4; ++i )
    {
        if( alpha[i] && color_used[colormap[i]] == false )
        {
            color_used[colormap[i]] = true;
            nb_opaque_colors++;
        }
    }

    if( nb_opaque_colors == 0 )
        return;

    // find colors by appearing order.
    uint8_t colors_found[4];	// palette index by finding order.
    int nb_colors_found = 0;
    memset( color_used, false, sizeof(color_used) );

    for( y = 0; y < h ; ++y )
    {
    	uint8_t* px = bitmap + y * w;
    	uint8_t* end = px + w;

    	for( ; px != end; ++px )
    	{
    		uint8_t idx = *px;

    		if( alpha[idx] == 0 )
    			continue;

    		uint8_t palette_idx = colormap[idx];
    		if( color_used[palette_idx] == false )
    		{
    			color_used[palette_idx] = true;
    			colors_found[nb_colors_found++] = palette_idx;

    			if( nb_colors_found == nb_opaque_colors )
    				goto all_indexes_found;
    		}
    	}
    }

all_indexes_found:
	switch( nb_colors_found )
	{
	case 1:
		for( i = 0; i < 4; ++i )
		{
			if( colormap[i] == colors_found[0] )
				rgba_palette[i] = combine( RGB_TEXT, 		alpha[i] );
		}
		break;

	case 2:
		for( i = 0; i < 4; ++i )
		{
			if( colormap[i] == colors_found[0] )
				rgba_palette[i] = combine( RGB_DARK_BORDER, alpha[i] );
			else if( colormap[i] == colors_found[1] )
				rgba_palette[i] = combine( RGB_TEXT, 		alpha[i] );
		}
		break;

	case 3:
		for( i = 0; i < 4; ++i )
		{
			if( colormap[i] == colors_found[0] )
				rgba_palette[i] = combine( RGB_DARK_BORDER, alpha[i] );
			else if( colormap[i] == colors_found[1] )
				rgba_palette[i] = combine( RGB_LIGHT_BORDER,alpha[i] );
			else if( colormap[i] == colors_found[2] )
				rgba_palette[i] = combine( RGB_TEXT, 		alpha[i] );
		}
		break;

	case 4:
		for( i = 0; i < 4; ++i )
		{
			if( colormap[i] == colors_found[0] )
				rgba_palette[i] = combine( RGB_BACK, 		alpha[i] );
			else if( colormap[i] == colors_found[1] )
				rgba_palette[i] = combine( RGB_DARK_BORDER, alpha[i] );
			else if( colormap[i] == colors_found[2] )
				rgba_palette[i] = combine( RGB_LIGHT_BORDER,alpha[i] );
			else if( colormap[i] == colors_found[3] )
				rgba_palette[i] = combine( RGB_TEXT, 		alpha[i] );
		}
		break;
	}

	// backup palette if all colors were found
//	if( nb_colors_found == nb_opaque_colors )		--> 실제로 entry가 누락되었어도 사용되지 않던 color가 사용되는 경우는 거의 없다.
	if( nb_colors_found > 0 )
	{
		struct Entry* newEntries = av_realloc( ctx->entries, sizeof(struct Entry) * (ctx->nb_entry+1) );
		if( newEntries == NULL )
		{
			fprintf( stderr, "DVDSubContext2 - Can't re-allocate palette cache entry." );
			return;
		}

		struct Entry* entry = newEntries + ctx->nb_entry;
		entry->key.colors = colormap32;
		entry->key.alphas = alphamap32;
		memcpy( entry->value.colors, rgba_palette, sizeof(entry->value.colors) );

		DBG( "DVDSubContext2 - color/alphamap cache entry is increasing. %d -> %d", ctx->nb_entry, ctx->nb_entry+1 );

		++ctx->nb_entry;
		ctx->entries = newEntries;
	}

	DBG( "DVDSubContext2 - %dms were spent to guess dvdsub palette with colormap %d %d %d %d / alpha %d %d %d %d. opaque-colors=%d found-colors=%d",
			(int)((av_gettime()-begin)/1000), colormap[0], colormap[1], colormap[2], colormap[3], alpha[0], alpha[1], alpha[2], alpha[3], nb_opaque_colors, nb_colors_found );
}


struct DVDSubContext2* dvdsub2_init()
{
	struct DVDSubContext2* ctx = av_mallocz( sizeof(struct DVDSubContext2) );

	DBG( "DVDSubContext2 - context created: %x", ctx );

	return ctx;
}

void dvdsub2_uninit( struct DVDSubContext2* ctx )
{
	if( ctx == NULL )
		return;

	av_free( ctx->entries );
	av_free( ctx );

	DBG( "DVDSubContext2 - context freed: %x", ctx );
}

void dvdsub2_guess_palette( struct DVDSubContext2* ctx, uint32_t *rgba_palette, uint8_t* colormap, uint8_t* alpha, uint8_t *bitmap, int w, int h )
{
	if( ctx == NULL )
		return;

	guess_palette( ctx, rgba_palette, colormap, alpha, bitmap, w, h );
}
