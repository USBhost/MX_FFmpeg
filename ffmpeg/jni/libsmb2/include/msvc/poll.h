
#ifndef _MSVC_POLL_H_
#define _MSVC_POLL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <sys/socket.h>

static inline int poll(struct pollfd pfd[], uint32_t size, int nvecs)
{
  return WSAPoll(pfd, size, nvecs);
}

#ifdef __cplusplus
}
#endif
#endif /* !_MSVC_POLL_H_ */
