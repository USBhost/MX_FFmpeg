/* From RFC1204  HMAC-MD5
 */

#ifndef HMAC_MD5_H
#define HMAC_MD5_H

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <string.h>
#include <sys/types.h>

#ifdef HAVE_STDINT_H
#include <stdint.h>
#endif

#if (__BYTE_ORDER == __BIG_ENDIAN)
#  define WORDS_BIGENDIAN 1
#endif

#if !defined(PS2_EE_PLATFORM) && !defined(PS2_IOP_PLATFORM)
typedef uint32_t UWORD32;
#endif

#ifdef __cplusplus
extern "C" {
#endif

void
smb2_hmac_md5(unsigned char *text, int text_len, unsigned char *key, int key_len,
	 unsigned char *digest);

#ifdef __cplusplus
}
#endif

#endif /* !HMAC_MD5_H */
