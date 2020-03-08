
#ifndef _MSVC_SYS_SOCKET_H_
#define _MSVC_SYS_SOCKET_H_

#ifdef __cplusplus
extern "C" {
#endif

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif // !WIN32_LEAN_AND_MEAN

#include <stdint.h>
#include <winsock2.h>
#include <ws2tcpip.h>

#include <libsmb2.h>

#define EBADF WSAENOTSOCK

typedef SSIZE_T ssize_t;

struct iovec
{
  unsigned long iov_len; // from WSABUF
  void *iov_base;        
};

inline int writev(t_socket sock, struct iovec *iov, int nvecs)
{
  DWORD ret;

  int res = WSASend(sock, (LPWSABUF)iov, nvecs, &ret, 0, NULL, NULL);

  if (res == 0) {
    return (int)ret;
  }
  return -1;
}

inline int readv(t_socket sock, struct iovec *iov, int nvecs)
{
  DWORD ret;
  DWORD flags = 0;

  int res = WSARecv(sock, (LPWSABUF)iov, nvecs, &ret, &flags, NULL, NULL);

  if (res == 0) {
    return (int)ret;
  }
  return -1;
}

inline int close(t_socket sock)
{
  return closesocket(sock);
}

#ifdef __cplusplus
}
#endif
#endif /* !_MSVC_SYS_SOCKET_H_ */
