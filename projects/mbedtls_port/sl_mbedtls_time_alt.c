#include "mbedtls/build_info.h"
#include "mbedtls/platform_time.h"


#ifdef MBEDTLS_PLATFORM_MS_TIME_ALT

#include <time.h>
#include "cmsis_os2.h"

int clock_gettime(int clk_id, struct timespec *tp)
{
    (void)clk_id; // Only one clock supported

    // Get system time in milliseconds
    uint32_t ms = osKernelGetTickCount() * (1000 / osKernelGetTickFreq());

    tp->tv_sec = ms / 1000;
    tp->tv_nsec = (ms % 1000) * 1000000L;
    return 0;
}

mbedtls_ms_time_t mbedtls_ms_time(void)
{
    int ret;
    struct timespec tv = {};
    mbedtls_ms_time_t current_ms;

    ret = clock_gettime(0, &tv);
    if (ret) {
        return time(NULL) * 1000L;
    }

    current_ms = tv.tv_sec;
    return current_ms * 1000L + tv.tv_nsec / 1000000L;
}
#endif // MBEDTLS_PLATFORM_MS_TIME_ALT