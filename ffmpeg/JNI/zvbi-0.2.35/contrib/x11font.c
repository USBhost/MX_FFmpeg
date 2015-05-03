/* Copyright (C) 2002 Gerd Knorr */

/* $Id: x11font.c,v 1.5 2006/02/10 06:25:36 mschimek Exp $ */

#include "src/exp-gfx.c"

static void print_head(FILE *fp,
		       const char *foundry,
		       const char *name,
		       const char *slant,
		       int width, int height)
{
    fprintf(fp,
	    "STARTFONT 2.1\n"
	    "FONT -%s-%s-medium-%s-normal--%d-%d-75-75-c-%d-iso10646-1\n"
	    "SIZE %d 75 75\n"
	    "FONTBOUNDINGBOX 6 13 0 -2\n"
	    "STARTPROPERTIES 25\n"
	    "FONTNAME_REGISTRY \"\"\n"
	    "FOUNDRY \"%s\"\n"
	    "FAMILY_NAME \"%s\"\n"
	    "WEIGHT_NAME \"medium\"\n"
	    "SLANT \"%s\"\n"
	    "SETWIDTH_NAME \"normal\"\n"
	    "ADD_STYLE_NAME \"\"\n"
	    "PIXEL_SIZE %d\n"
	    "POINT_SIZE %d\n"
	    "RESOLUTION_X 75\n"
	    "RESOLUTION_Y 75\n"
	    "SPACING \"c\"\n"
	    "AVERAGE_WIDTH %d\n"
	    "CHARSET_REGISTRY \"iso10646\"\n"
	    "CHARSET_ENCODING \"1\"\n"
	    "COPYRIGHT \"fixme\"\n"
	    "CAP_HEIGHT 9\n"
	    "X_HEIGHT 18\n"
	    "FONT \"-%s-%s-medium-%s-normal--%d-%d-75-75-c-%d-iso10646-1\"\n"
	    "WEIGHT 10\n"
	    "RESOLUTION 103\n"
	    "QUAD_WIDTH %d\n"
	    "DEFAULT_CHAR 0\n"
	    "FONT_ASCENT %d\n"
	    "FONT_DESCENT 0\n"
	    "ENDPROPERTIES\n",
	    foundry,name,slant,height,height*10,width*10,
	    height,
	    foundry,name,slant,height,height*10,width*10,
	    foundry,name,slant,height,height*10,width*10,
	    width,height);
}

static void
print_font(const char *filename,
	   const char *foundry,
	   const char *name, int italic,
	   uint8_t *font, int cw, int ch, int cpl,
	   int count, unsigned int (*map)(unsigned int,int), int invalid)
{
    FILE *fp;
    int x,y,i,c,on,bit,byte,mask1,mask2;

    fp = stdout;
    if (NULL != filename) {
	fp = fopen(filename,"w");
	if (NULL == fp)
	    fprintf(stderr,"open %s: %s\n",filename,strerror(errno));
	fprintf(stderr,"writing %s\n",filename);
    }
    
    print_head(fp, foundry, name, italic ? "i" : "r", cw, ch);
    fprintf(fp,"CHARS %d\n", count);
    
    for (i = 0; i < 0xffff; i++) {
	c = map(i, italic);
	if (invalid == c)
	    continue;

	fprintf(fp,"STARTCHAR fixme\n"
		"ENCODING %d\n"
		"SWIDTH %d 0\n"
		"DWIDTH %d 0\n"
		"BBX %d %d 0 0\n"
		"BITMAP\n",
		i,cw*10,cw,
		cw,ch);
	for (y = 0; y < ch; y++) {
	    bit  = cpl * cw * y + cw * c;
	    byte = 0;
	    for (x = 0; x < cw; x++) {
		mask1 = 1 << (bit & 7);
		mask2 = 1 << (7-(x & 7));
		on    = font[bit >> 3] & mask1;
		if (on)
		    byte |= mask2;
		if (7 == (x&7)) {
		    fprintf(fp,"%02x",byte);
		    byte = 0;
		}
		bit++;
	    }
	    fprintf(fp,"%02x\n",byte);
	}
	fprintf(fp,"ENDCHAR\n");
    }
    fprintf(fp,"ENDFONT\n");

    if (NULL != filename)
	fclose(fp);
}

int 
main ()
{
    print_font("teletext.bdf","ets","teletext",0,(uint8_t *) wstfont2_bits,
	       TCW,TCH,TCPL,1448,unicode_wstfont2,357);
    print_font("teletexti.bdf","ets","teletext",1,(uint8_t *) wstfont2_bits,
	       TCW,TCH,TCPL,1449,unicode_wstfont2,357);
    print_font("caption.bdf","ets","caption",0,(uint8_t *) ccfont2_bits,
	       CCW,CCH,CCPL,120,unicode_ccfont2,15);
    print_font("captioni.bdf","ets","caption",1,(uint8_t *) ccfont2_bits,
	       CCW,CCH,CCPL,120,unicode_ccfont2,15 + 4 * 32);
    return 0;
}


