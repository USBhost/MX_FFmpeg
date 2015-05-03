/*
 *  libzvbi -- VBI proxy client
 *
 *  Copyright (C) 2003, 2004 Tom Zoerner
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Library General Public
 *  License as published by the Free Software Foundation; either
 *  version 2 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Library General Public License for more details.
 *
 *  You should have received a copy of the GNU Library General Public
 *  License along with this library; if not, write to the 
 *  Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor, 
 *  Boston, MA  02110-1301  USA.
 */

/* $Id: proxy-client.h,v 1.5 2008/02/19 00:35:21 mschimek Exp $ */

#ifndef PROXY_CLIENT_H
#define PROXY_CLIENT_H

/* Public */

#include <sys/time.h> /* struct timeval */

/**
 * @ingroup Proxy
 * @brief Proxy client context
 *
 * A reference to this anonymous structure is returned by
 * vbi_proxy_client_create and must be passed to the device capture
 * interface and/or all subsequent calls to proxy client interface
 * functions.  The contents of this structure are private and must
 * not be accessed or changed by the caller.
 */
typedef struct vbi_proxy_client vbi_proxy_client;

/**
 * @ingroup Proxy
 * @brief Bits in event mask parameter to proxy client callback function
 */
typedef enum
{
   /**
    * Channel control token was granted, client may now change the channel.
    * Note: client should return the token after the channel change was
    * completed (the channel will still remain reserved for the requested
    * time)
    */
   VBI_PROXY_EV_CHN_GRANTED   = 1<<0,
   /**
    * Channel (e.g. TV tuner frequency) was changed by another client.
    */
   VBI_PROXY_EV_CHN_CHANGED   = 1<<1,
   /**
    * Norm was changed by another client (in a way which affects VBI,
    * e.g. changes between PAL/SECAM are ignored.)  The client must update
    * its services, else no data will be forwarded by the proxy until
    * the norm is changed back.
    */
   VBI_PROXY_EV_NORM_CHANGED  = 1<<2,
   /**
    * Proxy requests to return the channel control token.  The client is no
    * longer allowed to switch the channel and must immediately reply with
    * a channel notification with flag @c VBI_PROXY_CHN_TOKEN
    */
   VBI_PROXY_EV_CHN_RECLAIMED = 1<<3,
   /**
    * Empty event mask
    */
   VBI_PROXY_EV_NONE          = 0
} VBI_PROXY_EV_TYPE;

/**
 * @ingroup Proxy
 * @brief Function prototype for proxy client callback
 *
 * The first parameter is the value which the client passed when installing
 * the callback; it's just passed through to the callback unmodified.
 * The second parameter contains one or more bits to describe which events
 * occured wince the last call.
 */
typedef void VBI_PROXY_CLIENT_CALLBACK ( void * p_client_data,
                                         VBI_PROXY_EV_TYPE ev_mask );

/* forward declaration from io.h */
struct vbi_capture_buffer;

/**
 * @addtogroup Proxy
 * @{
 */
extern vbi_proxy_client *
vbi_proxy_client_create( const char *dev_name,
                         const char *p_client_name,
                         VBI_PROXY_CLIENT_FLAGS client_flags,
                         char **pp_errorstr,
                         int trace_level );

extern void
vbi_proxy_client_destroy( vbi_proxy_client * vpc );

extern vbi_capture *
vbi_proxy_client_get_capture_if( vbi_proxy_client * vpc );

extern VBI_PROXY_CLIENT_CALLBACK *
vbi_proxy_client_set_callback( vbi_proxy_client * vpc,
                               VBI_PROXY_CLIENT_CALLBACK * p_callback,
                               void * p_data );

extern VBI_DRIVER_API_REV
vbi_proxy_client_get_driver_api( vbi_proxy_client * vpc );

extern int
vbi_proxy_client_channel_request( vbi_proxy_client * vpc,
                                  VBI_CHN_PRIO chn_prio,
                                  vbi_channel_profile * chn_profile );

extern int
vbi_proxy_client_channel_notify( vbi_proxy_client * vpc,
                                 VBI_PROXY_CHN_FLAGS notify_flags,
                                 unsigned int scanning );

/**
 * @brief Modes for channel suspend requests.
 */
typedef enum
{
   /**
    * Request proxy daemon to stop acquisition (e.g. required by some
    * device drivers to allow a norm change.)   Depending on the driver
    * this may result in the proxy closing the device file handle
    * or just stopping the VBI data stream.
    * Note this command is only allowed when the client is in control
    * of the channel.
    */
   VBI_PROXY_SUSPEND_START,
   /**
    * Restart data acquisition after a previous suspension.
    */
   VBI_PROXY_SUSPEND_STOP
} VBI_PROXY_SUSPEND;

extern int
vbi_proxy_client_channel_suspend( vbi_proxy_client * vpc,
                                  VBI_PROXY_SUSPEND cmd );

int
vbi_proxy_client_device_ioctl( vbi_proxy_client * vpc,
                               int request,
                               void * p_arg );

extern int
vbi_proxy_client_get_channel_desc( vbi_proxy_client * vpc,
                                   unsigned int * p_scanning,
                                   vbi_bool * p_granted );

extern vbi_bool
vbi_proxy_client_has_channel_control( vbi_proxy_client * vpc );

/** @} */

/* Private */

#endif  /* PROXY_CLIENT_H */

/*
Local variables:
c-set-style: K&R
c-basic-offset: 8
End:
*/
