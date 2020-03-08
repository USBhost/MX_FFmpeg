
#ifdef ESP_PLATFORM

#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#include <lwip/sockets.h>
#include <sys/uio.h>
#include <assert.h>


ssize_t writev(int fd, const struct iovec *iov, int iovcnt)
{
  int total = 0;
  for (int i = 0; i < iovcnt; i++) {
    int left = iov[i].iov_len;
    while (left > 0) {
      int count = write(fd, iov[i].iov_base, left);
      if (count == -1) {
        return -1;
      }
      total += count;
      left -= count;
    }
  }
  return total;
}


ssize_t readv(int fd, const struct iovec *iov, int iovcnt)
{
  ssize_t total = 0;
  for (int i = 0; i < iovcnt; i++) {
    int left = iov[i].iov_len;
    while (left > 0) {
      int count = read(fd, iov[i].iov_base, left);
      if (count == -1) {
        return -1;
      }
      if (count == 0) {
        return total;
      }
      total += count;
      left -= count;
    }
  }
  return total;
}

#endif

