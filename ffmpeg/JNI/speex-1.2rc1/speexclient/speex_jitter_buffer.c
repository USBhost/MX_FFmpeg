#include <speex/speex_jitter.h>
#include "speex_jitter_buffer.h"

#ifndef NULL
#define NULL 0
#endif


void speex_jitter_init(SpeexJitter *jitter, void *decoder, int sampling_rate)
{
   jitter->dec = decoder;
   speex_decoder_ctl(decoder, SPEEX_GET_FRAME_SIZE, &jitter->frame_size);

   jitter->packets = jitter_buffer_init(jitter->frame_size);

   speex_bits_init(&jitter->current_packet);
   jitter->valid_bits = 0;

}

void speex_jitter_destroy(SpeexJitter *jitter)
{
   jitter_buffer_destroy(jitter->packets);
   speex_bits_destroy(&jitter->current_packet);
}

void speex_jitter_put(SpeexJitter *jitter, char *packet, int len, int timestamp)
{
   JitterBufferPacket p;
   p.data = packet;
   p.len = len;
   p.timestamp = timestamp;
   p.span = jitter->frame_size;
   jitter_buffer_put(jitter->packets, &p);
}

void speex_jitter_get(SpeexJitter *jitter, spx_int16_t *out, int *current_timestamp)
{
   int i;
   int ret;
   spx_int32_t activity;
   char data[2048];
   JitterBufferPacket packet;
   packet.data = data;
   packet.len = 2048;
   
   if (jitter->valid_bits)
   {
      /* Try decoding last received packet */
      ret = speex_decode_int(jitter->dec, &jitter->current_packet, out);
      if (ret == 0)
      {
         jitter_buffer_tick(jitter->packets);
         return;
      } else {
         jitter->valid_bits = 0;
      }
   }

   ret = jitter_buffer_get(jitter->packets, &packet, jitter->frame_size, NULL);
   
   if (ret != JITTER_BUFFER_OK)
   {
      /* No packet found */

      /*fprintf (stderr, "lost/late frame\n");*/
      /*Packet is late or lost*/
      speex_decode_int(jitter->dec, NULL, out);
   } else {
      speex_bits_read_from(&jitter->current_packet, packet.data, packet.len);
      /* Decode packet */
      ret = speex_decode_int(jitter->dec, &jitter->current_packet, out);
      if (ret == 0)
      {
         jitter->valid_bits = 1;
      } else {
         /* Error while decoding */
         for (i=0;i<jitter->frame_size;i++)
            out[i]=0;
      }
   }
   speex_decoder_ctl(jitter->dec, SPEEX_GET_ACTIVITY, &activity);
   if (activity < 30)
      jitter_buffer_update_delay(jitter->packets, &packet, NULL);
   jitter_buffer_tick(jitter->packets);
}

int speex_jitter_get_pointer_timestamp(SpeexJitter *jitter)
{
   return jitter_buffer_get_pointer_timestamp(jitter->packets);
}
