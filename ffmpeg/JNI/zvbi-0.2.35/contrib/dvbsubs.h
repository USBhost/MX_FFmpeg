/*
   dvbsubs - a program for decoding DVB subtitles (ETS 300 743)

   File: dvbsubs.h

   Copyright (C) Dave Chapman 2002
  
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


#define MAX_REGIONS 5

typedef struct {
  int x,y;
  unsigned char is_visible;
} visible_region_t;

typedef struct {
  int acquired;
  int page_time_out;
  int page_version_number;
  int page_state;
  visible_region_t regions[MAX_REGIONS];
} page_t;

typedef struct {
  int width,height;
  int depth;
  int CLUT_id;
  int win;
  int objects_start,objects_end;
  unsigned int object_pos[65536];
  unsigned char palette[256];
  unsigned char alpha[256];
  unsigned char img[720*576];
} region_t;

typedef struct {
  char regions[MAX_REGIONS][64];
  int next_region;
  int64_t start_pts;
  int64_t end_pts;
} textsub_t;
