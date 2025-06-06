#if defined(STARBOARD)
#include <errno.h>
#endif
#include <time.h>
#include <sys/time.h>
#include "syscall.h"

int gettimeofday(struct timeval *restrict tv, void *restrict tz)
{
	struct timespec ts;
#if defined(STARBOARD)
  if (!tv) {
    errno = EFAULT;
    return -1;
  }
#else
	if (!tv) return 0;
#endif
	clock_gettime(CLOCK_REALTIME, &ts);
	tv->tv_sec = ts.tv_sec;
	tv->tv_usec = (int)ts.tv_nsec / 1000;
	return 0;
}
