/*
   dvbsubs - a program for decoding DVB subtitles (ETS 300 743)

   File: dvbsubs.c

   Old code (C) 2002 Dave Chapman
   New code (C) 2009 Michael H. Schimek
 
   This program is free software; you can redistribute it and/or
   modify it under the terms of the GNU General Public License
   as published by the Free Software Foundation; either version 2
   of the License, or (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
   Or, point your browser to http://www.gnu.org/copyleft/gpl.html
*/


#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <unistd.h>
#include <time.h>
#include <sys/poll.h>
#include <ctype.h>
#include <sys/time.h>
#include <stdbool.h>
#include <stdarg.h>
#include <errno.h>
#include <setjmp.h>
#include <assert.h>
#include <inttypes.h>

#include "dvbsubs.h"

#if __GNUC__ < 3
#  define likely(expr) (expr)
#  define unlikely(expr) (expr)
#else
#  define likely(expr) __builtin_expect(expr, 1)
#  define unlikely(expr) __builtin_expect(expr, 0)
#endif

#undef TRUE
#define TRUE 1
#undef FALSE
#define FALSE 0

/* These should be defined in inttypes.h. */
#ifndef PRId64
#  define PRId64 "lld"
#endif
#ifndef PRIu64
#  define PRIu64 "llu"
#endif
#ifndef PRIx64
#  define PRIx64 "llx"
#endif

#define N_ELEMENTS(array) (sizeof (array) / sizeof ((array)[0]))

page_t page;
region_t regions[MAX_REGIONS];
textsub_t textsub;

int y=0;
int x=0;

int fd_osd;
int num_windows=1;
int acquired=0;
struct timeval start_tv;

unsigned int curr_obj;
unsigned int curr_reg[64];

unsigned char white[4]={255,255,255,0xff};
unsigned char green[4]={0,255,0,0xdf} ;
unsigned char blue[4]={0,0,255,0xbf} ;
unsigned char yellow[4]={255,255,0,0xbf} ;
unsigned char black[4]={0,0,0,0xff} ; 
unsigned char red[4]={255,0,0,0xbf} ;
unsigned char magenta[4]={255,0,255,0xff};
unsigned char othercol[4]={0,255,255,0xff};

unsigned char transp[4]={0,255,0,0};

//unsigned char buf[100000];
uint8_t * buf;;
int i=0;
int nibble_flag=0;
int in_scanline=0;

int sub_idx=0;

FILE * outfile;

static void
output_textsub(FILE * outf) {
  int temp, h, m, s, ms;
  int i;

  temp = textsub.start_pts;
  h = temp / 3600000;
  temp %= 3600000;
  m = temp / 60000;
  temp %= 60000;
  s = temp / 1000;
  temp %= 1000;
  ms = temp;
  fprintf(outf, "%d\n%02d:%02d:%02d,%03d --> ", ++sub_idx, h, m, s, ms);

  temp = textsub.end_pts;
  h = temp / 3600000;
  temp %= 3600000;
  m = temp / 60000;
  temp %= 60000;
  s = temp / 1000;
  temp %= 1000;
  ms = temp;
  fprintf(outf, "%02d:%02d:%02d,%03d\n", h, m, s, ms);

  for (i=0; i<MAX_REGIONS; i++) {
    if (strlen(textsub.regions[i]) > 0) 
      fprintf(outf,"%s", textsub.regions[i]);
  }
  fprintf(outf, "\n");
  fflush(outf);
}

static void
process_gocr_output(const char *const fname, int region)
{
  FILE *file;
  int read;
  file = fopen(fname, "r");
  if (file == NULL) {
    perror("fopen failed");
    return;
  }
  read = fread(textsub.regions[region],sizeof(char), 64, file);
  if (read <= 0)
    perror("error reading");
  textsub.regions[region][read] = '\0';
  if (textsub.regions[region][0] == ',' && textsub.regions[region][1] == '\n') {
    char tmp[66];
    char * eol;
    strcpy(tmp,textsub.regions[region]+2);
    eol = rindex(tmp,'\n');
    strcpy(eol,",\n");
    strcpy(textsub.regions[region], tmp);
  }
  fclose(file);
}



static void
output_pgm(FILE *f, int r)
{
  int x, y;
  fprintf(f,
    "P5\n"
    "%d %d\n"
    "255\n",
    regions[r].width, regions[r].height);
  for (y = 0; y < regions[r].height; ++y) {
    for (x = 0; x < regions[r].width; ++x) {
      int res = 0;
      int pix = regions[r].img[y*regions[r].width+x];
      if (regions[r].alpha[pix])
        res = regions[r].palette[pix] * regions[r].alpha[pix];
      else
        res = 0;
      res = (65535 - res) >> 8;
      putc(res&0xff, f);
    }
  }
  putc('\n', f);
}

#define GOCR_PROGRAM "gocr"
static void
run_ocr(int region, unsigned long long pts) {
  FILE *f;
  char inbuf[128];
  char outbuf[128];
  char cmd[512];
  int cmdres;
  //const char *const tmpfname = tmpnam(NULL);
  sprintf(inbuf, "subtitle-%llu-%d.pgm", pts / 90, region);
  sprintf(outbuf, "tmp.txt");
  f = fopen(inbuf, "w");
  output_pgm(f, region);
  fclose(f);
  //sprintf(cmd, GOCR_PROGRAM" -v 1 -s 7 -d 0 -m 130 -m 256 -m 32 -i %s -o %s", inbuf, outbuf);
  sprintf(cmd, GOCR_PROGRAM" -s 8 -d 0 -m 130  -i %s -o %s", inbuf, outbuf);
  cmdres = system(cmd);
  if (cmdres < 0) {
    perror("system failed");
    exit(EXIT_FAILURE);
  }
  else if (cmdres) {
    fprintf(stderr, GOCR_PROGRAM" returned %d\n", cmdres);
    exit(cmdres);
  }
  process_gocr_output(outbuf,region);
  unlink(inbuf);
  unlink(outbuf);
}

static void init_data() {
  int i;

  for (i=0;i<MAX_REGIONS;i++) {
    page.regions[i].is_visible=0;
    regions[i].win=-1;
  }
}

static void create_region(int region_id,int region_width,int region_height,int region_depth) {
  region_depth = region_depth; /* unused */

  regions[region_id].win=num_windows++;
  //fprintf(stderr,"region %d created - win=%d, height=%d, width=%d, depth=%d\n",region_id,regions[region_id].win,region_height,region_width,region_depth);
  regions[region_id].width=region_width;
  regions[region_id].height=region_height;

  memset(regions[region_id].img,15,sizeof(regions[region_id].img));
}

static void do_plot(int r,int x, int y, unsigned char pixel) {
  int i;
  if ((y >= 0) && (y < regions[r].height)) {
    i=(y*regions[r].width)+x;
    regions[r].img[i]=pixel;
  } else {
    fprintf(stderr,"plot out of region: x=%d, y=%d - r=%d, height=%d\n",x,y,r,regions[r].height);
  }
}

static void plot(int r,int run_length, unsigned char pixel) {
  int x2=x+run_length;

//  fprintf(stderr,"plot: x=%d,y=%d,length=%d,pixel=%d\n",x,y,run_length,pixel);
  while (x < x2) {
    do_plot(r,x,y,pixel);
    //fprintf(stderr,"%s",pixel==0?"  ":pixel==5?"..":pixel==6?"oo":pixel==7?"xx":pixel==8?"OO":"XX");
    x++;
  }
    
  //  x+=run_length;
}

static unsigned char next_nibble () {
  unsigned char x;

  if (nibble_flag==0) {
    x=(buf[i]&0xf0)>>4;
    nibble_flag=1;
  } else {
    x=(buf[i++]&0x0f);
    nibble_flag=0;
  }
  return(x);
}

/* function taken from "dvd2sub.c" in the svcdsubs packages in the
   vcdimager contribs directory.  Author unknown, but released under GPL2.
*/


/*void set_palette(int region_id,int id,int Y_value, int Cr_value, int Cb_value, int T_value) {
 int Y,Cr,Cb,R,G,B;
 unsigned char colour[4];

 Y=Y_value;
 Cr=Cr_value;
 Cb=Cb_value;
 B = 1.164*(Y - 16)                    + 2.018*(Cb - 128);
 G = 1.164*(Y - 16) - 0.813*(Cr - 128) - 0.391*(Cb - 128);
 R = 1.164*(Y - 16) + 1.596*(Cr - 128);
 if (B<0) B=0; if (B>255) B=255;
 if (G<0) G=0; if (G>255) G=255;
 if (R<0) R=0; if (R>255) R=255;
 colour[0]=R;
 colour[1]=B;
 colour[2]=G;
 if (Y==0) {
   colour[3]=0;
 } else {
   colour[3]=255;
 }

 //fprintf(stderr,"setting palette: region=%d,id=%d, R=%d,G=%d,B=%d,T=%d\n",region_id,id,R,G,B,T_value);

} 
*/

static inline void set_palette(int region_id,int id,int Y_value, int Cr_value, int Cb_value, int T_value) {
  Cr_value = Cr_value; /* unused */
  Cb_value = Cb_value;

  regions[region_id].palette[id] = Y_value;
  if (Y_value == 0) T_value = 0;
  regions[region_id].alpha[id] = T_value;
  //fprintf(stderr,"setting palette: region=%d,id=%d, val=%d,alpha=%d\n",region_id,id,Y_value,T_value);
}

static void decode_4bit_pixel_code_string(int r, int object_id, int ofs, int n) {
  int next_bits,
      switch_1,
      switch_2,
      switch_3,
      run_length,
      pixel_code=0;

  int bits;
  unsigned int data;
  int j;

  object_id = object_id; /* unused */
  ofs = ofs;

  if (in_scanline==0) {
    // printf("<scanline>\n");
    //fprintf(stderr,"\n");
    in_scanline=1;
  }
  nibble_flag=0;
  j=i+n;
  while(i < j) {
//    printf("start of loop, i=%d, nibble-flag=%d\n",i,nibble_flag);
//    printf("buf=%02x %02x %02x %02x\n",buf[i],buf[i+1],buf[i+2],buf[i+3]);

    pixel_code = 0;
    bits=0;
    next_bits=next_nibble();

    if (next_bits!=0) {
      pixel_code=next_bits;
      // printf("<pixel run_length=\"1\" pixel_code=\"%d\" />\n",pixel_code);
      plot(r,1,pixel_code);
      bits+=4;
    } else {
      bits+=4;
      data=next_nibble();
      switch_1=(data&0x08)>>3;
      bits++;
      if (switch_1==0) {
        run_length=(data&0x07);
        bits+=3;
        if (run_length!=0) {
          // printf("<pixel run_length=\"%d\" pixel_code=\"0\" />\n",run_length+2);
          plot(r,run_length+2,pixel_code);
        } else {
//          printf("end_of_string - run_length=%d\n",run_length);
          break;
        }
      } else {
        switch_2=(data&0x04)>>2;
        bits++;
        if (switch_2==0) {
          run_length=(data&0x03);
          bits+=2;
          pixel_code=next_nibble();
          bits+=4;
          //printf("<pixel run_length=\"%d\" pixel_code=\"%d\" />\n",run_length+4,pixel_code);
          plot(r,run_length+4,pixel_code);
        } else {
          switch_3=(data&0x03);
          bits+=2;
          switch (switch_3) {
            case 0: // printf("<pixel run_length=\"1\" pixel_code=\"0\" />\n");
                    plot(r,1,pixel_code);
                    break;
            case 1: // printf("<pixel run_length=\"2\" pixel_code=\"0\" />\n");
                    plot(r,2,pixel_code);
                    break;
            case 2: run_length=next_nibble();
                    bits+=4;
                    pixel_code=next_nibble();
                    bits+=4;
                    // printf("<pixel run_length=\"%d\", pixel_code=\"%d\" />\n",run_length+9,pixel_code);
                    plot(r,run_length+9,pixel_code);
                    break;
            case 3: run_length=next_nibble();
                    run_length=(run_length<<4)|next_nibble();
                    bits+=8;
                    pixel_code=next_nibble();
                    bits+=4;
                    // printf("<pixel run_length=\"%d\" pixel_code=\"%d\" />\n",run_length+25,pixel_code);
                    plot(r,run_length+25,pixel_code);
          }
        }
      }
    }

//    printf("used %d bits\n",bits);
  }
  if (nibble_flag==1) {
    i++;
    nibble_flag=0;
  }
}


static void process_pixel_data_sub_block(int r, int o, int ofs, int n) {
  int data_type;
  int j;

  j=i+n;

  x=(regions[r].object_pos[o])>>16;
  y=((regions[r].object_pos[o])&0xffff)+ofs;
//  fprintf(stderr,"process_pixel_data_sub_block: r=%d, x=%d, y=%d\n",r,x,y);
//  printf("process_pixel_data: %02x %02x %02x %02x %02x %02x\n",buf[i],buf[i+1],buf[i+2],buf[i+3],buf[i+4],buf[i+5]);
  while (i < j) {
    data_type=buf[i++];

//    printf("<data_type>%02x</data_type>\n",data_type);

    switch(data_type) {
      case 0x11: decode_4bit_pixel_code_string(r,o,ofs,n-1);
                 break;
      case 0xf0: // printf("</scanline>\n");
                 in_scanline=0;
                 x=(regions[r].object_pos[o])>>16;
                 y+=2;
                 break;
      default: fprintf(stderr,"unimplemented data_type %02x in pixel_data_sub_block\n",data_type);
    }
  }

  i=j;
}
static int process_page_composition_segment() {
  int page_id,
      segment_length,
      page_time_out,
      page_version_number,
      page_state;
  int region_id,region_x,region_y;
  int j;

  page_id=(buf[i]<<8)|buf[i+1]; i+=2;
  segment_length=(buf[i]<<8)|buf[i+1]; i+=2;

  j=i+segment_length;

  page_time_out=buf[i++];
  page_version_number=(buf[i]&0xf0)>>4;
  page_state=(buf[i]&0x0c)>>2;
  i++;

  // printf("<page_composition_segment page_id=\"0x%02x\">\n",page_id);
  // printf("<page_time_out>%d</page_time_out>\n",page_time_out);
  // printf("<page_version_number>%d</page_version_number>\n",page_version_number);
  // printf("<page_state>");
  //fprintf(stderr,"page_state=%d (",page_state);
  switch(page_state) {
     case 0: //fprintf(stderr,"normal_case)\n");
       break ;
     case 1: //fprintf(stderr,"acquisition_point)\n");
       break ;
     case 2: //fprintf(stderr,"mode_change)\n");
       break ;
     case 3: //fprintf(stderr,"reserved)\n");
       break ;
  }
  // printf("</page_state>\n");

  if ((acquired==0) && (page_state!=2) && (page_state!=1)) {
    //fprintf(stderr,"waiting for mode_change\n");
    return 1;
  } else {
    //fprintf(stderr,"acquired=1\n");
    acquired=1;
  }
  // printf("<page_regions>\n");
  // IF the packet contains no data (i.e. is  used to clear a
  // previous subtitle), do nothing
  if (i>=j) {
    //fprintf(stderr,"Empty sub, return\n");
    return 1;
  }

  while (i<j) {
    region_id=buf[i++];
    i++; // reserved
    region_x=(buf[i]<<8)|buf[i+1]; i+=2;
    region_y=(buf[i]<<8)|buf[i+1]; i+=2;

    page.regions[region_id].x=region_x;
    page.regions[region_id].y=region_y;
    page.regions[region_id].is_visible=1;
 
    //fprintf(stderr,"page_region id=%02x x=%d y=%d\n",region_id,region_x,region_y);
  }  
  // printf("</page_regions>\n");
  // printf("</page_composition_segment>\n");
  return 0;

}

static void process_region_composition_segment() {
  int page_id,
      segment_length,
      region_id,
      region_version_number,
      region_fill_flag,
      region_width,
      region_height,
      region_level_of_compatibility,
      region_depth,
      CLUT_id,
      region_8_bit_pixel_code,
      region_4_bit_pixel_code,
      region_2_bit_pixel_code;
  int object_id,
      object_type,
      object_provider_flag,
      object_x,
      object_y,
      foreground_pixel_code,
      background_pixel_code;
  int j;
  int o;

  page_id=(buf[i]<<8)|buf[i+1]; i+=2;
  segment_length=(buf[i]<<8)|buf[i+1]; i+=2;

  j=i+segment_length;

  region_id=buf[i++];
  region_version_number=(buf[i]&0xf0)>>4;
  region_fill_flag=(buf[i]&0x08)>>3;
  i++;
  region_width=(buf[i]<<8)|buf[i+1]; i+=2;
  region_height=(buf[i]<<8)|buf[i+1]; i+=2;
  region_level_of_compatibility=(buf[i]&0xe0)>>5;
  region_depth=(buf[i]&0x1c)>>2;
  i++;
  CLUT_id=buf[i++];
  region_8_bit_pixel_code=buf[i++];
  region_4_bit_pixel_code=(buf[i]&0xf0)>>4;
  region_2_bit_pixel_code=(buf[i]&0x0c)>>2;
  i++;


  if (regions[region_id].win < 0) {
    // If the region doesn't exist, then open it.
    create_region(region_id,region_width,region_height,region_depth);
    regions[region_id].CLUT_id=CLUT_id;
  }

  if (region_fill_flag==1) {
    //fprintf(stderr,"filling region %d with %d\n",region_id,region_4_bit_pixel_code);
    memset(regions[region_id].img,region_4_bit_pixel_code,sizeof(regions[region_id].img));
  }

  // printf("<region_composition_segment page_id=\"0x%02x\" region_id=\"0x%02x\">\n",page_id,region_id);

  // printf("<region_version_number>%d</region_version_number>\n",region_version_number);
  // printf("<region_fill_flag>%d</region_fill_flag>\n",region_fill_flag);
  // printf("<region_width>%d</region_width>\n",region_width);
  // printf("<region_height>%d</region_height>\n",region_height);
  // printf("<region_level_of_compatibility>%d</region_level_of_compatibility>\n",region_level_of_compatibility);
  // printf("<region_depth>%d</region_depth>\n",region_depth);
  // printf("<CLUT_id>%d</CLUT_id>\n",CLUT_id);
  // printf("<region_8_bit_pixel_code>%d</region_8_bit_pixel_code>\n",region_8_bit_pixel_code);
  // printf("<region_4_bit_pixel_code>%d</region_4_bit_pixel_code>\n",region_4_bit_pixel_code);
  // printf("<region_2_bit_pixel_code>%d</region_2_bit_pixel_code>\n",region_2_bit_pixel_code);

  regions[region_id].objects_start=i;  
  regions[region_id].objects_end=j;  

  for (o=0;o<65536;o++) {
    regions[region_id].object_pos[o]=0xffffffff;
  }

  // printf("<objects>\n");
  while (i < j) {
    object_id=(buf[i]<<8)|buf[i+1]; i+=2;
    object_type=(buf[i]&0xc0)>>6;
    object_provider_flag=(buf[i]&0x30)>>4;
    object_x=((buf[i]&0x0f)<<8)|buf[i+1]; i+=2;
    object_y=((buf[i]&0x0f)<<8)|buf[i+1]; i+=2;

    regions[region_id].object_pos[object_id]=(object_x<<16)|object_y;
      
    // printf("<object id=\"0x%02x\" type=\"0x%02x\">\n",object_id,object_type);
    // printf("<object_provider_flag>%d</object_provider_flag>\n",object_provider_flag);
    // printf("<object_x>%d</object_x>\n",object_x);
    // printf("<object_y>%d</object_y>\n",object_y);
    if ((object_type==0x01) || (object_type==0x02)) {
      foreground_pixel_code=buf[i++];
      background_pixel_code=buf[i++];
      // printf("<foreground_pixel_code>%d</foreground_pixel_code>\n",foreground_pixel_code);
      // printf("<background_pixel_code>%d</background_pixel_code>\n",background_pixel_code);
    }

    // printf("</object>\n");
  }
  // printf("</objects>\n");
  // printf("</region_composition_segment>\n");
}

static void process_CLUT_definition_segment() {
  int page_id,
      segment_length,
      CLUT_id,
      CLUT_version_number;

  int CLUT_entry_id,
      CLUT_flag_8_bit,
      CLUT_flag_4_bit,
      CLUT_flag_2_bit,
      full_range_flag,
      Y_value,
      Cr_value,
      Cb_value,
      T_value;

  int j;
  int r;

  page_id=(buf[i]<<8)|buf[i+1]; i+=2;
  segment_length=(buf[i]<<8)|buf[i+1]; i+=2;
  j=i+segment_length;

  CLUT_id=buf[i++];
  CLUT_version_number=(buf[i]&0xf0)>>4;
  i++;

  // printf("<CLUT_definition_segment page_id=\"0x%02x\" CLUT_id=\"0x%02x\">\n",page_id,CLUT_id);

  // printf("<CLUT_version_number>%d</CLUT_version_number>\n",CLUT_version_number);
  // printf("<CLUT_entries>\n");
  while (i < j) {
    CLUT_entry_id=buf[i++];
      
    // printf("<CLUT_entry id=\"0x%02x\">\n",CLUT_entry_id);
    CLUT_flag_2_bit=(buf[i]&0x80)>>7;
    CLUT_flag_4_bit=(buf[i]&0x40)>>6;
    CLUT_flag_8_bit=(buf[i]&0x20)>>5;
    full_range_flag=buf[i]&1;
    i++;
    // printf("<CLUT_flag_2_bit>%d</CLUT_flag_2_bit>\n",CLUT_flag_2_bit);
    // printf("<CLUT_flag_4_bit>%d</CLUT_flag_4_bit>\n",CLUT_flag_4_bit);
    // printf("<CLUT_flag_8_bit>%d</CLUT_flag_8_bit>\n",CLUT_flag_8_bit);
    // printf("<full_range_flag>%d</full_range_flag>\n",full_range_flag);
    if (full_range_flag==1) {
      Y_value=buf[i++];
      Cr_value=buf[i++];
      Cb_value=buf[i++];
      T_value=buf[i++];
    } else {
      Y_value=(buf[i]&0xfc)>>2;
      Cr_value=(buf[i]&0x2<<2)|((buf[i+1]&0xc0)>>6);
      Cb_value=(buf[i+1]&0x2c)>>2;
      T_value=buf[i+1]&2;
      i+=2;
    }
    // printf("<Y_value>%d</Y_value>\n",Y_value);
    // printf("<Cr_value>%d</Cr_value>\n",Cr_value);
    // printf("<Cb_value>%d</Cb_value>\n",Cb_value);
    // printf("<T_value>%d</T_value>\n",T_value);
    // printf("</CLUT_entry>\n");

    // Apply CLUT to every region it applies to.
    for (r=0;r<MAX_REGIONS;r++) {
      if (regions[r].win >= 0) {
        if (regions[r].CLUT_id==CLUT_id) {
          set_palette(r,CLUT_entry_id,Y_value,Cr_value,Cb_value,255-T_value);
        }
      }
    }
  }
  // printf("</CLUT_entries>\n");
  // printf("</CLUT_definition_segment>\n");
}

static void process_object_data_segment() {
  int page_id,
      segment_length,
      object_id,
      object_version_number,
      object_coding_method,
      non_modifying_colour_flag;
      
  int top_field_data_block_length,
      bottom_field_data_block_length;
      
  int j;
  int old_i;
  int r;

  page_id=(buf[i]<<8)|buf[i+1]; i+=2;
  segment_length=(buf[i]<<8)|buf[i+1]; i+=2;
  j=i+segment_length;
  
  object_id=(buf[i]<<8)|buf[i+1]; i+=2;
  curr_obj=object_id;
  object_version_number=(buf[i]&0xf0)>>4;
  object_coding_method=(buf[i]&0x0c)>>2;
  non_modifying_colour_flag=(buf[i]&0x02)>>1;
  i++;

  // printf("<object_data_segment page_id=\"0x%02x\" object_id=\"0x%02x\">\n",page_id,object_id);

  // printf("<object_version_number>%d</object_version_number>\n",object_version_number);
  // printf("<object_coding_method>%d</object_coding_method>\n",object_coding_method);
  // printf("<non_modifying_colour_flag>%d</non_modifying_colour_flag>\n",non_modifying_colour_flag);

  // fprintf(stderr,"decoding object %d\n",object_id);
  old_i=i;
  for (r=0;r<MAX_REGIONS;r++) {
    // If this object is in this region...
   if (regions[r].win >= 0) {
    //fprintf(stderr,"testing region %d, object_pos=%08x\n",r,regions[r].object_pos[object_id]);
    if (regions[r].object_pos[object_id]!=0xffffffff) {
      //fprintf(stderr,"rendering object %d into region %d\n",object_id,r);
      i=old_i;
      if (object_coding_method==0) {
        top_field_data_block_length=(buf[i]<<8)|buf[i+1]; i+=2;
        bottom_field_data_block_length=(buf[i]<<8)|buf[i+1]; i+=2;

        process_pixel_data_sub_block(r,object_id,0,top_field_data_block_length);

        process_pixel_data_sub_block(r,object_id,1,bottom_field_data_block_length);
      }
    }
   }
  }
  // Data should be word-aligned, pass the next byte if necessary
  if (((old_i - i) & 0x1) == 0)
    i++;
}

static void process_pes_packet() {
  int n;
  unsigned long long PTS;
  unsigned char PTS_1;
  unsigned short PTS_2,PTS_3;
  double PTS_secs;
  int r; 

  int segment_length,
      segment_type;
  int empty_sub = 0;

  init_data();
  gettimeofday(&start_tv,NULL);

  // printf("<?xml version=\"1.0\" ?>\n");
  i=6;

  i++;  // Skip some boring PES flags
  if (buf[i]!=0x80) {
   fprintf(stdout,"UNEXPECTED PES HEADER: %02x\n",buf[i]);
   exit(-1);
  }
  i++; 
  if (buf[i]!=5) {
   fprintf(stdout,"UNEXPECTED PES HEADER DATA LENGTH: %d\n",buf[i]);
   exit(-1);
  }
  i++;  // Header data length
  PTS_1=(buf[i++]&0x0e)>>1;  // 3 bits
  PTS_2=(buf[i]<<7)|((buf[i+1]&0xfe)>>1);         // 15 bits
  i+=2;
  PTS_3=(buf[i]<<7)|((buf[i+1]&0xfe)>>1);         // 15 bits
  i+=2;

  PTS=PTS_1;
  PTS=(PTS << 15)|PTS_2;
  PTS=(PTS << 15)|PTS_3;

  PTS_secs=(PTS/90000.0);

  // printf("<pes_packet data_identifier=\"0x%02x\" pts_secs=\"%.02f\">\n",buf[i++],PTS_secs);
  i++;
  // printf("<subtitle_stream id=\"0x%02x\">\n",buf[i++]);
  i++;
  while (buf[i]==0x0f) {
    /* SUBTITLING SEGMENT */
    i++;  // sync_byte
    segment_type=buf[i++];

    /* SEGMENT_DATA_FIELD */
    switch(segment_type) {
      case 0x10: empty_sub = process_page_composition_segment(); 
                 break;
      case 0x11: process_region_composition_segment();
                 break;
      case 0x12: process_CLUT_definition_segment();
                 break;
      case 0x13: process_object_data_segment();
                 break;
      default:
        segment_length=(buf[i+2]<<8)|buf[i+3];
        i+=segment_length+4;
//        printf("SKIPPING segment %02x, length %d\n",segment_type,segment_length);
    }
  }   
  // printf("</subtitle_stream>\n");
  // printf("</pes_packet>\n");
  // fprintf(stderr,"End of PES packet - time=%.2f\n",PTS_secs);
  /* if (empty_sub) */ {
    int i;
    if (textsub.start_pts < 0)
      /* return */;
    else {
    textsub.end_pts = PTS/90;
    output_textsub(outfile);
    textsub.end_pts = textsub.start_pts = -1;
    for(i=0; i<MAX_REGIONS; i++)
      textsub.regions[i][0] = '\0';
    }
  }
  /* else */ {
    textsub.start_pts = PTS/90;
    n=0;
    for (r=0;r<MAX_REGIONS;r++) {
      if (regions[r].win >= 0) {
        if (page.regions[r].is_visible) {
          //int xx,yy;
          //fprintf(stderr,"displaying region %d at %d,%d width=%d,height=%d PTS = %g\n",
        //r,page.regions[r].x,page.regions[r].y,regions[r].width,regions[r].height, PTS_secs);
          /*for(yy=0;yy<regions[r].height;yy++) {
            for(xx=0;xx<regions[r].width;xx++) {
              unsigned char pix = regions[r].img[+yy*regions[r].width+xx];
              fprintf(stderr,"%s",pix==0?"  ":pix==5?"..":pix==6?"oo":pix==7?"xx":pix==8?"OO":"XX");
            }
            fprintf(stderr,"\n");
          }*/
          run_ocr(r, PTS);
          n++;
        }
        /* else {
          //fprintf(stderr,"hiding region %d\n",r);
        }*/
      }
    }
    /*if (n > 0) {
      fprintf(stderr,"%d regions visible - showing\n",n);
    } else {
      fprintf(stderr,"%d regions visible - hiding\n",n);
    }*/
  }
//    if (acquired) { sleep(1); }
}

#define PID_MASK_HI 0x1F
static uint16_t get_pid(uint8_t *pid)
{
  uint16_t pp = 0;

  pp = (pid[0] & PID_MASK_HI) << 8;
  pp |= pid[1];

  return pp;
}

/* From dvb-mpegtools ctools.c, (C) 2000-2002 Marcus Metzler,
   license GPLv2+. */
static ssize_t save_read(int fd, void *buf, size_t count)
{
	ssize_t neof = 1;
	size_t re = 0;
	
	while(neof >= 0 && re < count){
		neof = read(fd, (uint8_t *) buf + re, count - re);
		if (neof > 0) re += neof;
		else break;
	}

	if (neof < 0 && re == 0) return neof;
	else return re;
}

#define TS_SIZE 188
#define IN_SIZE TS_SIZE*10
static uint8_t * get_sub_packets(int fdin, uint16_t pids) {
  uint8_t buffer[IN_SIZE];
  uint8_t mbuf[TS_SIZE];
  uint8_t * packet = NULL;
  uint8_t * next_write = NULL;
  int packet_current_size = 0;
  int packet_size = 0;
  int i;
  int count = 1;
  uint16_t pid;

  // fprintf(stderr,"extract pid %d\n", pids);
  if ((count = save_read(fdin,mbuf,TS_SIZE))<0)
      perror("reading");

  for ( i = 0; i < 188 ; i++){
    if ( mbuf[i] == 0x47 ) break;
  }
  if ( i == 188){
    fprintf(stderr,"Not a TS\n");
    return NULL;
  } else {
    memcpy(buffer,mbuf+i,TS_SIZE-i);
    if ((count = save_read(fdin,mbuf,i))<0)
      perror("reading");
    memcpy(buffer+TS_SIZE-i,mbuf,i);
    i = 188;
  }

  count = 1;
  while (count > 0){
    if ((count = save_read(fdin,buffer+i,IN_SIZE-i)+i)<0)
      perror("reading");
    for( i = 0; i < count; i+= TS_SIZE){
      uint8_t off = 0;

      if ( count - i < TS_SIZE) break;
      pid = get_pid(buffer+i+1);
      if (!(buffer[3+i]&0x10)) // no payload?
        continue;
      if ( buffer[1+i]&0x80){
        fprintf(stderr,"Error in TS for PID: %d\n", pid);
      }
      if (pid != pids)
        continue;

      if ( buffer[3+i] & 0x20) {  // adaptation field?
        off = buffer[4+i] + 1;
      }
      if ( !packet && 
           ! buffer[i+off+4] && ! buffer[i+off+5] && buffer[i+off+6] && 
           buffer[i+off+7] == 0xbd) {
        packet_size = buffer[i+off+8]<<8 | buffer[i+off+9];
        packet_size += 6; // for the prefix, the stream ID and the size field
        
        //fprintf(stderr,"Packet start, size = %d\n",packet_size);
        packet = (uint8_t *)malloc(packet_size);
        next_write = packet;
        packet_current_size = 0;
      }
      if (packet) {
        if (packet_current_size + TS_SIZE-4-off <= packet_size) {
          memcpy(next_write, buffer+4+off+i, TS_SIZE-4-off);
          next_write+=TS_SIZE-4-off;
          packet_current_size += TS_SIZE-4-off;
        }
        else {
          fprintf(stderr,"write beyond buffer limit?\n");
          free(packet);
          packet = NULL;
          next_write = NULL;
          packet_current_size = 0;
          packet_size = 0;
          continue;
        }
        if (packet_current_size == packet_size) {
          // process packet
          //int j=0;
          buf = packet;
          /*for(j=0;j<packet_size;j++) {
            fprintf(stderr,"%02x ", packet[j]);
          }
          fprintf(stderr,"\n");
          */
          process_pes_packet();
          free(packet);
          packet = NULL;
          next_write = NULL;
          packet_current_size = 0;
          packet_size = 0;
        }
      }
    }
    i = 0;
  }
  return NULL;
}

/* New code --------------------------------------------------------------- */

static const char *		my_name;

static unsigned int		option_verbosity;

/* Input file descriptor. */
static int			fd;

/* Transport stream buffer. */
static uint8_t *		ts_buffer;
static unsigned int		ts_buffer_capacity;

/* Statistics. */
static uint64_t			ts_n_bytes_in;
static uint64_t			ts_n_subt_packets_in;

/* Program ID of the subtitle elementary stream. */
static unsigned int		ts_subt_pid;

/* Next expected continuity_counter. Only the 4 least
   significant bits are valid. -1 if unknown. */
static int			ts_next_cc;

/* Subtitle PES buffer. */
static uint8_t *		pes_buffer;
static unsigned int		pes_buffer_capacity;
static unsigned int		pes_in;

/* Expected end of PES packet in pes_buffer, 0 if none. */
static unsigned int		pes_packet_end;

#define log(verb, templ, args...) \
	log_message (verb, /* print_errno */ FALSE, templ , ##args)

#define log_errno(verb, templ, args...) \
	log_message (verb, /* print_errno */ TRUE, templ , ##args)

#define bug(templ, args...) \
	log_message (1, /* print_errno */ FALSE, "BUG: " templ , ##args)

static void
log_message			(unsigned int		verbosity,
				 bool			print_errno,
				 const char *		templ,
				 ...)
  __attribute__ ((format (printf, 3, 4)));

static void
log_message			(unsigned int		verbosity,
				 bool			print_errno,
				 const char *		templ,
				 ...)
{
	if (verbosity <= option_verbosity) {
		va_list ap;

		va_start (ap, templ);

		fprintf (stderr, "%s: ", my_name);
		vfprintf (stderr, templ, ap);

		if (print_errno) {
			fprintf (stderr, ": %s.\n",
				 strerror (errno));
		}

		va_end (ap);
	}
}

#define error_exit(templ, args...) \
	error_message_exit (/* print_errno */ FALSE, templ , ##args)

#define errno_exit(templ, args...) \
	error_message_exit (/* print_errno */ TRUE, templ , ##args)

static void
error_message_exit		(bool			print_errno,
				 const char *		templ,
				 ...)
  __attribute__ ((format (printf, 2, 3)));

static void
error_message_exit		(bool			print_errno,
				 const char *		templ,
				 ...)
{
	if (option_verbosity > 0) {
		va_list ap;

		va_start (ap, templ);

		fprintf (stderr, "%s: ", my_name);
		vfprintf (stderr, templ, ap);

		if (print_errno) {
			fprintf (stderr, ": %s.\n",
				 strerror (errno));
		}

		va_end (ap);
	}

	exit (EXIT_FAILURE);
}

static void
no_mem_exit			(void)
{
	error_exit ("Out of memory.");
}

void
hex_dump                        (const uint8_t *        buf,
                                 unsigned int           n_bytes)
{
        const unsigned int mod = 16;
	unsigned int i;

        for (i = 0; i < n_bytes; ++i) {
                fprintf (stderr, "%02x ", buf[i]);
                if ((mod - 1) == (i % mod)) {
                        if (1) {
				unsigned int j;

                                fputc (' ', stderr);
                                for (j = 0; j < mod; ++j) {
                                        int c = buf[i - mod + 1 + j];
                                        if ((c & 0x7F) < 0x20)
                                                c = '.';
                                        fputc (c, stderr);
                                }
                        }
                        fputc ('\n', stderr);
                }
        }
        if ((mod - 1) != (n_bytes % mod))
                fputc ('\n', stderr);
}

#ifdef HAVE_POSIX_MEMALIGN

/* posix_memalign() was introduced in POSIX 1003.1d and may not be
   available on all systems. */
static void *
my_memalign			(size_t			boundary,
				 size_t			size)
{
	void *p;
	int err;

	/* boundary must be a power of two. */
	if (0 != (boundary & (boundary - 1)))
		return malloc (size);

	err = posix_memalign (&p, boundary, size);
	if (0 == err)
		return p;

	errno = err;

	return NULL;
}

#elif defined HAVE_MEMALIGN
/* memalign() is a GNU extension. */
#  define my_memalign memalign
#else
#  define my_memalign(boundary, size) malloc (size)
#endif

static __inline__ unsigned int
get16be				(const uint8_t *	s)
{
	/* XXX Use movw & xchg if available. */

	return s[0] * 256 + s[1];
}

static __inline__ unsigned int
get32be				(const uint8_t *	s)
{
	/* XXX Use movl & bswap if available. */

	return get16be (s + 0) * 65536 + get16be (s + 2);
}

struct bit_stream {
	const uint8_t *			data;

	unsigned int			pos;
	unsigned int			end;

	jmp_buf				exit;
};

static uint8_t
get_bits			(struct bit_stream *	bs,
				 unsigned int		n_bits)
{
	unsigned int pos;
	unsigned int byte_pos;
	unsigned int bit_pos;
	unsigned int value;

	pos = bs->pos;

	if (unlikely (pos + n_bits > bs->end))
		longjmp (bs->exit, 1);

	bs->pos = pos + n_bits;

	byte_pos = pos >> 3;
	bit_pos = pos & 7;

	/* assert (n_bits <= 8); */

	if (bit_pos + n_bits > 8) {
		value = get16be (bs->data + byte_pos) << bit_pos;
	} else {
		value = bs->data[byte_pos] << (bit_pos + 8);
	}

	return (value & 0xFFFF) >> (16 - n_bits);
}

static __inline__ void
realign_bit_stream		(struct bit_stream *	bs,
				 unsigned int		n_bits)
{
	/* assert (is_power_of_two (n_bits)); */

	bs->pos = (bs->pos + (n_bits - 1)) & ~(n_bits - 1);
}

static __inline__ bool
init_bit_stream			(struct bit_stream *	bs,
				 const uint8_t *	s,
				 const uint8_t *	e)
{
	bs->data = s;
	bs->pos = 0;
	bs->end = (e - s) * 8;

	return (0 == setjmp (bs->exit));
}

#define FIELD_DUMP 1

#define begin()								\
do {									\
	if (FIELD_DUMP)							\
		fprintf (stderr, "%s:\n", __FUNCTION__);		\
} while (0)
#define uimsbf bslbf
#define bslbf(field, val)						\
do {									\
	field = (val);							\
	if (FIELD_DUMP)							\
		fprintf (stderr, " " #field " = %u = 0x%x\n",		\
			 (unsigned int) field, (unsigned int) field);	\
} while (0)
#define bslbf_1(field, val)						\
do {									\
	field = (val);							\
	if (FIELD_DUMP)							\
		fprintf (stderr, " " #field " = %u\n",			\
			 (unsigned int) field & 1);			\
} while (0)
#define bslbf_enum(field, val, names)					\
do {									\
	field = (val);							\
	if (FIELD_DUMP)							\
		fprintf (stderr, " " #field " = %u (%s)\n",		\
			 (unsigned int) field, names [field]);		\
} while (0)

/* EN 300 743 Section 7.2.4.2.
   See also Section 11, Table 14, 15, 16. */

static void
eight_bit_pixel_code_string	(struct bit_stream *	bs,
				 int r)
{
	r = r; /* unused */

    in_scanline=1;

	begin ();

	while (bs->pos < bs->end) {
		unsigned int run_length;
		unsigned int pixel_code;
		unsigned int switch_1;
		unsigned int n;

		run_length = 1;
		bslbf (pixel_code, get_bits (bs, 8));

		if (0 == pixel_code) {
			n = get_bits (bs, 8);
			bslbf_1 (switch_1, n >> 7);
			uimsbf (run_length, switch_1 & 127);

			if ((int8_t) n >= 0) {
				/* 00000000 0LLLLLLL */
				if (0 == run_length)
					return;
				run_length += 1;
			} else {
				/* 00000000 1LLLLLLL CCCCCCCC */
				run_length += 3;
				bslbf (pixel_code, get_bits (bs, 8));
			}
		}

		/* plot(r,run_length,pixel_code); */
	}
}

static void
four_bit_pixel_code_string	(struct bit_stream *	bs,
				 int r)
{
	r = r; /* unused */

    in_scanline=1;

	begin ();

	while (bs->pos < bs->end) {
		unsigned int run_length;
		unsigned int pixel_code;

		run_length = 1;
		bslbf (pixel_code, get_bits (bs, 4));

		if (0 == pixel_code) {
			uimsbf (run_length, get_bits (bs, 4));

			switch (run_length) {
			case 0: /* 0000 0000 */
				return;

			case 1 ... 7: /* 0000 0LLL */
				run_length += 2;
				break;

			case 8 ... 11: /* 0000 10LL CCCC */
				run_length += 4 - 8;
				bslbf (pixel_code, get_bits (bs, 4));
				break;

			case 12: /* 0000 1100 */
			case 13: /* 0000 1101 */
				run_length += 1 - 12;
				break;

			case 14: /* 0000 1110 LLLL CCCC */
				pixel_code = get_bits (bs, 8);
				uimsbf (run_length, pixel_code >> 4);
				run_length += 9;
				bslbf (pixel_code, pixel_code & 15);
				break;

			case 15: /* 0000 1111 LLLL LLLL CCCC */
				uimsbf (run_length, get_bits (bs, 8));
				run_length += 25;
				bslbf (pixel_code, get_bits (bs, 4));
				break;
			}
		}

		/* plot(r,run_length,pixel_code); */
	}
}

static void
two_bit_pixel_code_string	(struct bit_stream *	bs,
				 int r)
{
	r = r; /* unused */

    in_scanline=1;

	begin ();

	while (bs->pos < bs->end) {
		unsigned int run_length;
		unsigned int pixel_code;

		run_length = 1;
		bslbf (pixel_code, get_bits (bs, 2));

		if (0 == pixel_code) {
			unsigned int switch_3;

			uimsbf (run_length, get_bits (bs, 2));
			switch (run_length) {
			case 2 ... 3: /* 00 1L LL CC */
				pixel_code = run_length * 16
					+ get_bits (bs, 4);
				uimsbf (run_length, (pixel_code >> 2) - 8);
				run_length += 3;
				bslbf (pixel_code, pixel_code & 3);
				break;

			case 1: /* 00 01 */
				break;

			case 0: /* 00 00 ... */
				bslbf (switch_3, get_bits (bs, 2));
				switch (switch_3) {
				case 0:	/* 00 00 00 */
					return;

				case 1:	/* 00 00 01 */
					run_length = 2;
					break;

				case 2:	/* 00 00 10 LL LL CC */
					pixel_code = get_bits (bs, 6);
					uimsbf (run_length, pixel_code >> 2);
					run_length += 12;
					bslbf (pixel_code, pixel_code & 3);
					break;

				case 3:	/* 00 00 11 LL LL LL LL CC */
					uimsbf (run_length, get_bits (bs, 8));
					run_length += 29;
					bslbf (pixel_code, get_bits (bs, 2));
					break;
				}

				break;
			}
		}

		/* plot(r,run_length,pixel_code); */
	}
}

/* EN 300 743 Section 7.2.4.1. */

static bool
pixel_data_sub_block_loop	(const uint8_t *	s,
				 const uint8_t *	end,
				 int r, int o, int ofs)
{
	struct bit_stream bs;

  x=(regions[r].object_pos[o])>>16;
  y=((regions[r].object_pos[o])&0xffff)+ofs;
//  fprintf(stderr,"process_pixel_data_sub_block: r=%d, x=%d, y=%d\n",r,x,y);
//  printf("process_pixel_data: %02x %02x %02x %02x %02x %02x\n",s[0],s[1],s[2],s[3],s[4],s[5]);

	if (!init_bit_stream (&bs, s, end))
		return FALSE;

	while (bs.pos < bs.end) {
		unsigned int data_type;

		begin ();

		bslbf (data_type, get_bits (&bs, 8));
		switch (data_type) {
		case 0x10:
			if (0) {
				/* Not implemented yet. */
				two_bit_pixel_code_string (&bs,r);
				realign_bit_stream (&bs, 8);
			}
			break;

		case 0x11:
			four_bit_pixel_code_string (&bs,r);
			realign_bit_stream (&bs, 8);
			break;

		case 0x12:
			if (0) {
				/* Not implemented yet. */
				eight_bit_pixel_code_string (&bs,r);
			}
			break;

		case 0xf0: /* end of object line code */
                 in_scanline=0;
                 x=(regions[r].object_pos[o])>>16;
                 y+=2;
		 ++s;
                 break;

		case 0x20: /* 2 to 4-bit map-table */
		case 0x21: /* 2 to 8-bit map-table */
		case 0x22: /* 4 to 8-bit map-table */

		default: fprintf(stderr,"unimplemented data_type %02x in pixel_data_sub_block\n",data_type);
			return TRUE;
		}
	}

	return (bs.pos == bs.end);
}

/* EN 300 743 Section 7.2.4. */

static bool
object_data_segment		(const uint8_t *	s,
				 const uint8_t *	end)
{
	static const char *object_coding_method_names [4] = {
		"coding of pixels",
		"coded as a string of characters",
		"reserved",
		"reserved"
	};
	const uint8_t *old_s;
	unsigned int object_id;
	unsigned int object_version_number;
	unsigned int object_coding_method;
	unsigned int non_modifying_colour_flag;
	unsigned int top_field_data_block_length;
	unsigned int bottom_field_data_block_length;
	unsigned int total_length;
  int r;

	begin ();

	if (s + 8 >= end)
		return FALSE;

	bslbf (object_id, get16be (s + 6));
	uimsbf (object_version_number, s[8] >> 4);
	bslbf_enum (object_coding_method, (s[8] >> 2) & 3,
		    object_coding_method_names);
	bslbf_1 (non_modifying_colour_flag, s[8] & 2);
	/* reserved [1] */

  curr_obj=object_id;

	switch (object_coding_method) {
	case 0: /* coding of pixels */
		if (s + 12 >= end)
			return FALSE;

		uimsbf (top_field_data_block_length, get16be (s + 9));
		uimsbf (bottom_field_data_block_length, get16be (s + 11));

		total_length = 13 + top_field_data_block_length
			+ bottom_field_data_block_length;
		/* 8_stuff_bits for 16 bit alignment. */
		total_length += total_length & 1;

		if (s + total_length != end)
			return FALSE;

		old_s = s + 13;

  for (r=0;r<MAX_REGIONS;r++) {
    // If this object is in this region...
   if (regions[r].win >= 0) {
    //fprintf(stderr,"testing region %d, object_pos=%08x\n",r,regions[r].object_pos[object_id]);
    if ((int) regions[r].object_pos[object_id]!=-1) {
      //fprintf(stderr,"rendering object %d into region %d\n",object_id,r);
      s = old_s;
      if (!pixel_data_sub_block_loop (s, s + top_field_data_block_length,
				      r,object_id,0))
	      return FALSE;
      s += top_field_data_block_length;
      if (!pixel_data_sub_block_loop (s, s + bottom_field_data_block_length,
				      r,object_id,1))
	      return FALSE;
    }
   }
  }
		/* FIXME: "if a segment carries no data for the bottom field, i.e. the bottom_field_data_block_length contains the value '0x0000', then the pixel-data_sub-block for the top field shall apply for the bottom field also." */

		break;

	case 1: /* coded as a string of characters */
		fprintf (stderr, "Whoops! Coding as characters "
			 "not supported.\n");
		break;

	case 2 ... 3: /* reserved */
		break;
	}

	return TRUE;
}

/* EN 300 743 Section 7.2.3. */

static bool
CLUT_definition_segment		(const uint8_t *	s,
				 const uint8_t *	end)
{
	unsigned int CLUT_id;
	unsigned int CLUT_version_number;

	begin ();

	if (s + 7 >= end)
		return FALSE;

	bslbf (CLUT_id, s[6]);
	uimsbf (CLUT_version_number, s[7] >> 4);
	/* reserved [4] */

	while (s + 11 < end) {
		unsigned int CLUT_entry_id;
		unsigned int two_bit_entry_CLUT_flag;
		unsigned int four_bit_entry_CLUT_flag;
		unsigned int eight_bit_entry_CLUT_flag;
		unsigned int full_range_flag;
		unsigned int Y_value;
		unsigned int Cr_value;
		unsigned int Cb_value;
		unsigned int T_value;
  int r;

		bslbf (CLUT_entry_id, s[8]);
		bslbf_1 (two_bit_entry_CLUT_flag, s[9] & 0x80);
		bslbf_1 (four_bit_entry_CLUT_flag, s[9] & 0x40);
		bslbf_1 (eight_bit_entry_CLUT_flag, s[9] & 0x20);
		/* reserved [4] */
		bslbf_1 (full_range_flag, s[9] & 1);

		if (full_range_flag) {
			if (s + 13 >= end)
				return FALSE;

			bslbf (Y_value, s[10]);
			bslbf (Cr_value, s[11]);
			bslbf (Cb_value, s[12]);
			bslbf (T_value, s[13]);

			s += 6;
		} else {
			unsigned int n;

			fprintf (stderr, "Whoops! CLUT reduced range "
				 "not supported.\n");
			return FALSE;

			n = get16be (s + 10);

			/* Scale? */
			bslbf (Y_value, n >> 10);
			bslbf (Cr_value, (n >> 6) & 15);
			bslbf (Cb_value, (n >> 2) & 15);
			bslbf (T_value, n & 3);

			s += 4;
		}

    // Apply CLUT to every region it applies to.
    for (r=0;r<MAX_REGIONS;r++) {
      if (regions[r].win >= 0) {
        if ((int) regions[r].CLUT_id == (int) CLUT_id) {
	  /* XXX 255-T? */
          set_palette(r,CLUT_entry_id,Y_value,Cr_value,Cb_value,255-T_value);
        }
      }
    }

	}

	return (s + 8 == end);
}

/* EN 300 743 Section 7.2.2. */

static bool
region_composition_segment	(const uint8_t *	s,
				 const uint8_t *	end)
{
  int o;

	static const char *region_level_of_compatibility_names [8] = {
		"reserved",
		"2 bit/entry CLUT required",
		"4 bit/entry CLUT required",
		"8 bit/entry CLUT required",
		"reserved", "reserved",
		"reserved", "reserved"
	};
	unsigned int region_id;
	unsigned int region_version_number;
	unsigned int region_fill_flag;
	unsigned int region_width;
	unsigned int region_height;
	unsigned int region_level_of_compatibility;
	unsigned int region_depth;
	unsigned int CLUT_id;
	unsigned int region_8_bit_pixel_code;
	unsigned int region_4_bit_pixel_code;
	unsigned int region_2_bit_pixel_code;

	begin ();

	uimsbf (region_id, s[6]);

	if (region_id >= MAX_REGIONS) {
		fprintf (stderr, "Whoops! Too many regions for us.\n");
		return FALSE;
	}

	uimsbf (region_version_number, s[7] >> 4);
	bslbf_1 (region_fill_flag, s[7] & 8);
	/* reserved [3] */
	uimsbf (region_width, get16be (s + 8));
	uimsbf (region_height, get16be (s + 10));

	/* EN 300 743 Section 7.2.2. */
	if ((region_width - 1) > 719
	    || (region_height - 1) > 575)
		return FALSE;

	bslbf_enum (region_level_of_compatibility, s[12] >> 5,
		    region_level_of_compatibility_names);

	region_depth = (s[12] >> 2) & 7;
	if (FIELD_DUMP) {
		if (region_depth >= 1 && region_depth <= 3) {
			fprintf (stderr, " region_depth = %u (%u bits)\n",
				 region_depth, 1 << region_depth);
		} else {
			fprintf (stderr, " region_depth = %u (reserved)\n",
				 region_depth);
		}
	}

	if (0xF1 & ((1 << region_level_of_compatibility) |
		    (1 << region_depth)))
		return FALSE;

	/* reserved [2] */
	bslbf (CLUT_id, s[13]);
	bslbf (region_8_bit_pixel_code, s[14]);
	bslbf (region_4_bit_pixel_code, s[15] >> 4);
	bslbf (region_2_bit_pixel_code, (s[15] >> 2) & 3);
	/* reserved [2] */

  if (regions[region_id].win < 0) {
    // If the region doesn't exist, then open it.
    create_region(region_id,region_width,region_height,region_depth);
    regions[region_id].CLUT_id=CLUT_id;
  }

  if (region_fill_flag) {
    memset(regions[region_id].img,region_4_bit_pixel_code,sizeof(regions[region_id].img));
  }

  for (o=0;o<(int) N_ELEMENTS (regions[0].object_pos);o++) {
    regions[region_id].object_pos[o]=-1;
  }

	while (s + 21 < end) {
		static const char *object_type_names [4] = {
			"basic_object, bitmap",
			"basic_object, character",
			"composite_object, string of characters",
			"reserved"
		};
		unsigned int object_id;
		unsigned int object_type;
		unsigned int object_horizontal_position;
		unsigned int object_vertical_position;
		unsigned int foreground_pixel_code;
		unsigned int background_pixel_code;
		unsigned int n;

		bslbf (object_id, get16be (s + 16));
		n = get16be (s + 18);
		bslbf_enum (object_type, n >> 14, object_type_names);
		uimsbf (object_horizontal_position, n & 0xFFF);
		/* reserved [4] */
		n = get16be (s + 20);
		uimsbf (object_vertical_position, n & 0xFFF);

		switch (object_type) {
		case 0: /* bitmap */
    regions[region_id].object_pos[object_id]=
			(object_horizontal_position<<16)|
			object_vertical_position;
			s += 6;
			break;

		case 1: /* character */
		case 2: /* character string */
			if (s + 23 >= end)
				return FALSE;
			bslbf (foreground_pixel_code, s[22]);
			bslbf (background_pixel_code, s[23]);
			s += 8;
			break;

		case 3: /* reserved */
			s += 6;
			break;
		}
	}

	return (s + 16 == end);
}

/* EN 300 743 Section 7.2.1. */

static bool
page_composition_segment	(bool *			empty_sub,
				 const uint8_t *	s,
				 const uint8_t *	end)
{
	static const char *page_state_names [4] = {
		"normal case",
		"acquisition point",
		"mode change",
		"reserved"
	};
	unsigned int page_time_out;
	unsigned int page_version_number;
	unsigned int page_state;

	*empty_sub = FALSE;

	begin ();

	if (s + 7 >= end)
		return FALSE;

	uimsbf (page_time_out, s[6]);
	uimsbf (page_version_number, s[7] >> 4);
	bslbf_enum (page_state, (s[7] >> 2) & 3, page_state_names);
	/* reserved [2] */

	if (page_state >= 3)
		return FALSE;

  if ((acquired==0) && (page_state!=2) && (page_state!=1)) {
    //fprintf(stderr,"waiting for mode_change\n");
	  *empty_sub = TRUE;
	  return TRUE;
  } else {
    //fprintf(stderr,"acquired=1\n");
    acquired=1;
  }

  // IF the packet contains no data (i.e. is  used to clear a
  // previous subtitle), do nothing
	if (s + 8 == end) {
		//fprintf(stderr,"Empty sub, return\n");
		*empty_sub = TRUE;
		return TRUE;
	}

	while (s + 13 < end) {
		unsigned int region_id;
		unsigned int region_horizontal_address;
		unsigned int region_vertical_address;

		bslbf (region_id, s[8]);
		/* reserved [8] */
		uimsbf (region_horizontal_address, get16be (s + 10));
		uimsbf (region_vertical_address, get16be (s + 12));

		if (region_id >= MAX_REGIONS) {
			fprintf (stderr, "Whoops! Too many regions for us.\n");
			return FALSE;
		}

    page.regions[region_id].x=region_horizontal_address;
    page.regions[region_id].y=region_vertical_address;
    page.regions[region_id].is_visible=1;
 
    //fprintf(stderr,"page_region id=%02x x=%d y=%d\n",region_id,region_x,region_y);

		s += 6;
	}

	return (s + 8 == end);
}

/* EN 300 743 Section 7.2. */

static bool
subtitling_segment_loop	        (bool *			empty_sub,
				 const uint8_t *	s,
				 const uint8_t *	end)
{
	for (;;) {
		unsigned int sync_byte;

		begin ();

		bslbf (sync_byte, s[0]);
		if (0x0F == sync_byte) {
			unsigned int segment_type;
			unsigned int page_id;
			unsigned int segment_length;
			const uint8_t *segment_end;
			bool success;

			/* sync_byte [8], segment_type [8],
			   page_id [16], segment_length [16],
			   segment_data_field [segment_length * 8],
			   end_of_PES_data_field_marker [8]. */
			if (s + 6 >= end)
				return FALSE;

			bslbf (segment_type, s[1]);
			bslbf (page_id, get16be (s + 2));
			uimsbf (segment_length, get16be (s + 4));

			segment_end = s + 6 + segment_length;
			if (segment_end >= end)
				return FALSE;

			switch (segment_type) {
			case 0x10:
				success = page_composition_segment
					(empty_sub, s, segment_end); 
				break;

			case 0x11:
				success = region_composition_segment
					(s, segment_end);
				break;

			case 0x12:
				success = CLUT_definition_segment
					(s, segment_end);
				break;

			case 0x13:
				success = object_data_segment
					(s, segment_end);
				break;

			default: /* 0x40 ... 0x7F Reserved
				    0x80 End of display segment
				    0x81 ... 0xEF Private data
				    0xFF stuffing */
				if (1) {
					hex_dump (s, segment_end - s);
				}
				success = TRUE;
				break;
			}

			if (!success)
				return FALSE;

			s = segment_end;
		} else if (0xFF == sync_byte) {
			/* end_of_PES_data_field_marker. */
			break;
		} else {
			return FALSE;
		}
	}

	return TRUE;
}

/* packet_start_code_prefix [24], stream_id [8],
   PES_packet_length [16]. */
static const unsigned int	MAX_PES_PACKET_SIZE = 6 + 65535;

static const unsigned int	PTS_BITS = 33;
static const uint64_t		PTS_MASK = (((int64_t) 1) << 33) - 1;

static bool
decode_time_stamp		(int64_t *		ts,
				 const uint8_t *	s,
				 unsigned int		marker)
{
	/* ISO 13818-1 Section 2.4.3.6 */

	if (0 != ((marker ^ s[0]) & 0xF1))
		return FALSE;

	if (NULL != ts) {
		unsigned int a, b, c;

		/* marker [4], TS [32..30], marker_bit,
		   TS [29..15], marker_bit,
		   TS [14..0], marker_bit */
		a = (s[0] >> 1) & 0x7;
		b = get16be (s + 1) >> 1;
		c = get16be (s + 3) >> 1;

		*ts = ((int64_t) a << 30) + (b << 15) + (c << 0);
	}

	return TRUE;
}

static void
dump_pes_packet_header		(FILE *			fp,
				 const uint8_t *	pes_packet,
				 const uint8_t *	end)
{
	unsigned int packet_start_code_prefix;
	unsigned int stream_id;
	unsigned int PES_packet_length;
	unsigned int PES_scrambling_control;
	unsigned int PES_priority;
	unsigned int data_alignment_indicator;
	unsigned int copyright;
	unsigned int original_or_copy;
	unsigned int PTS_DTS_flags;
	unsigned int ESCR_flag;
	unsigned int ES_rate_flag;
	unsigned int DSM_trick_mode_flag;
	unsigned int additional_copy_info_flag;
	unsigned int PES_CRC_flag;
	unsigned int PES_extension_flag;
	unsigned int PES_header_data_length;
	int64_t ts;

	/* ISO 13818-1 Section 2.4.3.6. */

	fputs ("PES packet", fp);

	if (pes_packet + 9 >= end)
		goto truncated;

	packet_start_code_prefix  = ((pes_packet[0] << 16) |
				     (pes_packet[1] << 8) |
				     (pes_packet[2] << 0));
	stream_id		  = pes_packet[3];
	PES_packet_length	  = get16be (pes_packet + 4);
	/* '10' */
	PES_scrambling_control	  = (pes_packet[6] & 0x30) >> 4;
	PES_priority		  = pes_packet[6] & 0x08;
	data_alignment_indicator  = pes_packet[6] & 0x04;
	copyright		  = pes_packet[6] & 0x02;
	original_or_copy	  = pes_packet[6] & 0x01;
	PTS_DTS_flags		  = (pes_packet[7] & 0xC0) >> 6;
	ESCR_flag		  = pes_packet[7] & 0x20;
	ES_rate_flag		  = pes_packet[7] & 0x10;
	DSM_trick_mode_flag	  = pes_packet[7] & 0x08;
	additional_copy_info_flag = pes_packet[7] & 0x04;
	PES_CRC_flag		  = pes_packet[7] & 0x02;
	PES_extension_flag	  = pes_packet[7] & 0x01;
	PES_header_data_length	  = pes_packet[8];

	fprintf (fp, " %06X%02X %5u "
		 "%u%u%u%c%c%c%c%u%c%c%c%c%c%c %u",
		 packet_start_code_prefix, stream_id,
		 PES_packet_length,
		 !!(pes_packet[6] & 0x80),
		 !!(pes_packet[6] & 0x40),
		 PES_scrambling_control,
		 PES_priority ? 'P' : '-',
		 data_alignment_indicator ? 'A' : '-',
		 copyright ? 'C' : '-',
		 original_or_copy ? 'O' : 'C',
		 PTS_DTS_flags,
		 ESCR_flag ? 'E' : '-',
		 ES_rate_flag ? 'E' : '-',
		 DSM_trick_mode_flag ? 'D' : '-',
		 additional_copy_info_flag ? 'A' : '-',
		 PES_CRC_flag ? 'C' : '-',
		 PES_extension_flag ? 'X' : '-',
		 PES_header_data_length);

	switch (PTS_DTS_flags) {
	case 0: /* no timestamps */
	case 1: /* forbidden */
		fputc ('\n', fp);
		break;

	case 2: /* PTS only */
		if (pes_packet + 14 >= end)
			goto truncated;
		if (decode_time_stamp (&ts, &pes_packet[9], 0x21))
			fprintf (fp, " PTS=%" PRId64 "\n", ts);
		else
			fputs (" bad PTS\n", fp);
		break;

	case 3: /* PTS and DTS */
		if (pes_packet + 19 >= end)
			goto truncated;
		if (decode_time_stamp (&ts, &pes_packet[9], 0x31))
			fprintf (fp, " PTS=%" PRId64, ts);
		else
			fputs (" bad PTS", fp);
		if (decode_time_stamp (&ts, &pes_packet[14], 0x11))
			fprintf (fp, " DTS=%" PRId64 "\n", ts);
		else
			fputs (" bad DTS\n", fp);
		break;
	}

	return;

 truncated:
	fputs (" truncated\n", fp);
}

static bool
pes_subt_packet			(const uint8_t *	pes_packet,
				 const uint8_t *	end)
{
  double PTS_secs;
  int r;

	const uint8_t *s;
	unsigned int n;
	unsigned int PES_header_data_length;
	int64_t pts;
	unsigned int data_identifier;
	unsigned int subtitle_stream_id;
	bool empty_sub;

	if (0) {
		dump_pes_packet_header (stderr, pes_packet, end);
	}

	/* Minimum PES packet header size is 9 bytes, plus at least 5
	   bytes for the mandatory PTS (EN 300743 Section 6), plus at
	   least 3 bytes for the PES_data_field (EN 300743 Section
	   7.1). */
	if (pes_packet + 8 + 5 + 3 > end)
		return FALSE;

	n = get16be (pes_packet + 6);

	/* '10', PES_scrambling_control == '00' (not scrambled),
	   data_alignment_indicator == 1 (EN 300743 Section 6),
	   PTS_DTS_flags == '10' (EN 300743 Section 6). */
	if (0x8480 != (n & 0xF4C0))
		return FALSE;

	PES_header_data_length = pes_packet[8];
	if (PES_header_data_length < 5
	    || pes_packet + PES_header_data_length + 12 > end)
		return FALSE;

	if (!decode_time_stamp (&pts, &pes_packet[9], 0x21))
		return FALSE;

	s = pes_packet + PES_header_data_length + 9;

	/* EN 300743 Section 7.1: PES_data_field. */

	data_identifier = s[0];
	if (0x20 != data_identifier)
		return FALSE;

	subtitle_stream_id = s[1];
	if (0x00 != subtitle_stream_id) {
		/* Not a DVB subtitling stream. */
		return TRUE;
	}

  init_data();
  gettimeofday(&start_tv,NULL);

  PTS_secs=(pts/90000.0);

	empty_sub = FALSE;

	if (!subtitling_segment_loop (&empty_sub, s + 2, end)) {
		return FALSE;
	}

  // fprintf(stderr,"End of PES packet - time=%.2f\n",PTS_secs);
  if (empty_sub) {
    int i;
    if (textsub.start_pts < 0)
      return TRUE;
    textsub.end_pts = pts/90;
    output_textsub(outfile);
    textsub.end_pts = textsub.start_pts = -1;
    for(i=0; i<MAX_REGIONS; i++)
      textsub.regions[i][0] = '\0';
  }
  else if (textsub.start_pts >= 0) {
    int i;
    textsub.end_pts = pts/90;
    output_textsub(outfile);
    textsub.end_pts = textsub.start_pts = -1;
    for(i=0; i<MAX_REGIONS; i++)
      textsub.regions[i][0] = '\0';
  }
  else {
    int n;

    textsub.start_pts = pts/90;
    n=0;
    for (r=0;r<MAX_REGIONS;r++) {
      if (regions[r].win >= 0) {
        if (page.regions[r].is_visible) {
          //int xx,yy;
          //fprintf(stderr,"displaying region %d at %d,%d width=%d,height=%d PTS = %g\n",
        //r,page.regions[r].x,page.regions[r].y,regions[r].width,regions[r].height, PTS_secs);
          /*for(yy=0;yy<regions[r].height;yy++) {
            for(xx=0;xx<regions[r].width;xx++) {
              unsigned char pix = regions[r].img[+yy*regions[r].width+xx];
              fprintf(stderr,"%s",pix==0?"  ":pix==5?"..":pix==6?"oo":pix==7?"xx":pix==8?"OO":"XX");
            }
            fprintf(stderr,"\n");
          }*/
          run_ocr(r, pts);
          n++;
        }
        /* else {
          //fprintf(stderr,"hiding region %d\n",r);
        }*/
      }
    }
    /*if (n > 0) {
      fprintf(stderr,"%d regions visible - showing\n",n);
    } else {
      fprintf(stderr,"%d regions visible - hiding\n",n);
    }*/
  }
//    if (acquired) { sleep(1); }

	return TRUE;
}

static void
ts_subt_reset			(void)
{
	pes_in = 0;
	pes_packet_end = 0;
}

static bool
ts_subt_packet			(const uint8_t		ts_packet[188],
				 unsigned int		header_length)
{
	unsigned int payload_unit_start_indicator;
	unsigned int payload_length;
	unsigned int PES_packet_length;

	payload_unit_start_indicator = ts_packet[1] & 0x40;

	/* ISO 13818-1 Section 2.4.3.3. */
	if (payload_unit_start_indicator) {
		if (unlikely (pes_in > 0)) {
			/* TS packet headers and PES_packet_length
			   disagree about the PES packet size. */
			ts_subt_reset ();
		}
	} else if (unlikely (0 == pes_in)) {
		/* Discard remainder of previous PES packet. */
		return TRUE;
	}

	payload_length = 188 - header_length;

	memcpy (pes_buffer + pes_in,
		ts_packet + header_length, payload_length);

	pes_in += payload_length;

	if (0 == pes_packet_end) {
		if (unlikely (pes_in < 6))
			return TRUE; /* need more data */

		/* EN 300743 Section 6. */
		if (0x000001BD != get32be (pes_buffer + 0)) {
			ts_subt_reset ();
			return FALSE;
		}

		PES_packet_length = get16be (pes_buffer + 4);

		pes_packet_end = 6 + PES_packet_length;
	}

	if (pes_in < pes_packet_end)
		return TRUE; /* need more data */

	if (unlikely (pes_in > pes_packet_end)) {
		/* TS packet headers and PES_packet_length
		   disagree about the PES packet size. */
		ts_subt_reset ();
		return FALSE;
	}

	pes_subt_packet (pes_buffer, pes_buffer + pes_packet_end);

	ts_subt_reset ();

	return TRUE;
}

static const unsigned int	TS_PACKET_SIZE = 188;

static void
dump_ts_packet_header		(FILE *			fp,
				 const uint8_t		ts_packet[188])
{
	unsigned int sync_byte;
	unsigned int transport_error_indicator;
	unsigned int payload_unit_start_indicator;
	unsigned int transport_priority;
	unsigned int PID;
	unsigned int transport_scrambling_control;
	unsigned int adaptation_field_control;
	unsigned int continuity_counter;
	unsigned int header_length;

	sync_byte			= ts_packet[0];
	transport_error_indicator	= ts_packet[1] & 0x80;
	payload_unit_start_indicator	= ts_packet[1] & 0x40;
	transport_priority		= ts_packet[1] & 0x20;
	PID				= get16be (ts_packet + 1) & 0x1FFF;
	transport_scrambling_control	= (ts_packet[3] & 0xC0) >> 6;
	adaptation_field_control	= (ts_packet[3] & 0x30) >> 4;
	continuity_counter		= ts_packet[3] & 0x0F;

	if (adaptation_field_control >= 2) {
		unsigned int adaptation_field_length;

		adaptation_field_length = ts_packet[4];
		header_length = 5 + adaptation_field_length;
	} else {
		header_length = 4;
	}

	fprintf (fp,
		 "TS packet %02x %c%c%c 0x%04x=%u %u%u%x %u\n",
		 sync_byte,
		 transport_error_indicator ? 'E' : '-',
		 payload_unit_start_indicator ? 'S' : '-',
		 transport_priority ? 'P' : '-',
		 PID, PID,
		 transport_scrambling_control,
		 adaptation_field_control,
		 continuity_counter,
		 header_length);
}

static bool
ts_filter			(const uint8_t		ts_packet[188])
{
	unsigned int transport_error_indicator;
	unsigned int pid;
	unsigned int adaptation_field_control;
	unsigned int header_length;

	if (0) dump_ts_packet_header (stderr, ts_packet);

	transport_error_indicator = ts_packet[1] & 0x80;
	if (unlikely (transport_error_indicator)) {
		log (2, "TS transmission error\n");
		ts_subt_reset ();
		ts_next_cc = -1;
		return TRUE;
	}

	pid = get16be (ts_packet + 1) & 0x1FFF;

	if (ts_subt_pid != pid)
		return TRUE;

	++ts_n_subt_packets_in;

	if (0) dump_ts_packet_header (stderr, ts_packet);

	adaptation_field_control = (ts_packet[3] & 0x30) >> 4;

	if (likely (1 == adaptation_field_control)) {
		header_length = 4;
	} else if (3 == adaptation_field_control) {
		unsigned int adaptation_field_length;

		adaptation_field_length = ts_packet[4];

		/* Zero length is used for stuffing. */
		if (adaptation_field_length > 0) {
			unsigned int discontinuity_indicator;

			/* ISO 13818-1 Section 2.4.3.5. Also the code
			   below assumes header_length <=
			   packet_size. */
			if (adaptation_field_length > 182) {
				log (2, "TS AFL error\n");
				ts_subt_reset ();
				ts_next_cc = -1;
				return FALSE;
			}

			/* ISO 13818-1 Section 2.4.3.5. */
			discontinuity_indicator = ts_packet[5] & 0x80;
			if (discontinuity_indicator) {
				log (2, "TS discontinuity\n");
				ts_subt_reset ();
			}
		}

		header_length = 5 + adaptation_field_length;
	} else if (0 == adaptation_field_control) {
		log (2, "TS AFC error\n");
		ts_subt_reset ();
		ts_next_cc = -1;
		return FALSE;
	} else {
		/* 2 == adaptation_field_control: no payload. */
		/* ISO 13818-1 Section 2.4.3.3:
		   continuity_counter shall not increment. */
		return TRUE;
	}

	if (unlikely (0 != ((ts_next_cc ^ ts_packet[3]) & 0x0F))) {
		/* Continuity counter mismatch. */

		if (ts_next_cc < 0) {
			/* First TS packet. */
		} else if (0 == (((ts_next_cc - 1) ^ ts_packet[3]) & 0x0F)) {
			/* ISO 13818-1 Section 2.4.3.3: Repeated
			   packet. */
			return TRUE;
		} else {
			log (2, "TS continuity error\n");
			ts_subt_reset ();
		}
	}

	ts_next_cc = ts_packet[3] + 1;

	ts_subt_packet (ts_packet, header_length);

	return TRUE;
}

static unsigned int
ts_sync				(const uint8_t *	ts,
				 const uint8_t *	end)
{
	const uint8_t *ts0 = ts;

	for (;;) {
		unsigned int avail;

		avail = end - ts;
		if (avail < 188)
			break; /* need more data */

		if (unlikely (0x47 != ts[0])) {
			unsigned int offset;

			if (avail < 188 + 187)
				break; /* need more data */

			if (ts_n_subt_packets_in > 0)
				log (2, "TS sync lost.\n");

			for (offset = 0; offset < (avail - 187); ++offset) {
				if (0x47 == ts[offset]
				    && 0x47 == ts[offset + 188])
					break;
			}

			if (offset >= (avail - 187)) {
				ts_subt_reset ();
				return end - ts0 - 187;
			}

			ts += offset;
		}

		if (likely (ts_filter (ts)))
			ts += 188;
		else
			ts += 1;
	}

	return ts - ts0; /* num. bytes consumed */
}

static void
file_read_loop			(void)
{
	ssize_t in;
	ssize_t out;

	assert (0 == (ts_buffer_capacity & 4095));

	in = 0;
	out = 0;

	for (;;) {
		ssize_t space;
		ssize_t actual;
		ssize_t left;

		space = ts_buffer_capacity - in;
		assert (space > 0);

		for (;;) {
			actual = read (fd, ts_buffer + in, space);
			if (actual >= 0)
				break;
			if (EINTR == errno)
				continue;
			errno_exit ("Read error");
		}

		if (0 == actual)
			break; /* eof */

		in += actual;

		ts_n_bytes_in += actual;

		out += ts_sync (ts_buffer + out, ts_buffer + in);

		left = in - out;
		if (left > 0) {
			/* Keep reads page aligned. */
			in = out & 4095;

			memmove (ts_buffer + in,
				 ts_buffer + out, left);
		} else {
			assert (0 == left);
			in = 0;
		}

		out = in;
		in += left;
	}
}

static void
init				(void)
{
	ts_buffer_capacity = 32 * 1024;
	ts_buffer = my_memalign (4096, ts_buffer_capacity);
	if (NULL == ts_buffer) {
		no_mem_exit ();
	}

	ts_n_bytes_in = 0;
	ts_n_subt_packets_in = 0;
	ts_next_cc = -1;

	pes_buffer_capacity = MAX_PES_PACKET_SIZE + TS_PACKET_SIZE;
	pes_buffer = my_memalign (4096, pes_buffer_capacity);
	if (NULL == pes_buffer) {
		no_mem_exit ();
	}

	ts_subt_reset ();
}

/* Old code */

int main(int argc, char* argv[]) {
  int pid;
 
  if (argc!=4) {
    fprintf(stderr,"USAGE: dvbsubs PID input_file output_file\n");
    exit(0);
  }

  pid=atoi(argv[1]);
  fd=open(argv[2],O_RDONLY);
  outfile=fopen(argv[3],"w");
  textsub.start_pts = textsub.end_pts = -1;

  if (1) {
    get_sub_packets(fd,pid);
  } else {
	ts_subt_pid = pid;
	init ();
	file_read_loop ();
  }

  fclose(outfile);
  close(fd);
  return 0;
}
