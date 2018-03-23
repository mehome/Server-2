#ifndef TIMEUTIL_H
#define TIMEUTIL_H

#include <stdint.h>
#include "Core.h"

#ifdef _WIN32
#include <windows.h>

//纳秒
inline uint64_t time_nanosecond()
{
	LARGE_INTEGER tick;
	LARGE_INTEGER timestamp;
	QueryPerformanceFrequency(&tick);
	QueryPerformanceCounter(&timestamp);
	uint64_t us=(timestamp.QuadPart % tick.QuadPart)*1E6/tick.QuadPart;
	return us;
}

//微秒
inline uint64_t time_microsecond_()
{
	LARGE_INTEGER tick;
	LARGE_INTEGER timestamp;
	QueryPerformanceFrequency(&tick);
	QueryPerformanceCounter(&timestamp);
	uint64_t us=(timestamp.QuadPart % tick.QuadPart)*1E3/tick.QuadPart;
	return us;
}

//微秒
inline uint64_t time_microsecond()
{
// 从1601年1月1日0:0:0:000到1970年1月1日0:0:0:000的时间(单位100ns)
#define EPOCHFILETIME   (116444736000000000UL)
    FILETIME ft;
    LARGE_INTEGER li;
    int64_t tt = 0;
    GetSystemTimeAsFileTime(&ft);
    li.LowPart = ft.dwLowDateTime;
    li.HighPart = ft.dwHighDateTime;
    // 从1970年1月1日0:0:0:000到现在的微秒数(UTC时间)
    tt = (li.QuadPart - EPOCHFILETIME) /10;
    return tt;
}
//毫秒
inline uint64_t  time_millisecond(){
	return time_microsecond()/1E3;
}
//秒
inline long  time_second(){
	return time_microsecond()/1E6;
}


#define sleep(A) Sleep(A/1000)

//微秒
int usleep(int microsecond){
	LARGE_INTEGER Freq={0};
    if (!QueryPerformanceFrequency(&Freq))
	{
		return -1;
	}
    LARGE_INTEGER Start={0};
    QueryPerformanceCounter(&Start);
	LARGE_INTEGER Curr={0};
	int time = 0;
	for(;;)
	{
		QueryPerformanceCounter(&Curr);
		time= ((Curr.QuadPart - Start.QuadPart)*1000000)/Freq.QuadPart;
		if (time >= delayTime){
			break;
		}
		if(time - delayTime / 1000 > 1){
			Sleep(time - delayTime / 1000);
		}
	}
	return time;
}

#else

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>
#include <sys/time.h>
#include <errno.h>
//纳秒
static inline uint64_t time_nanosecond(){
	struct timespec ts;
	ABORTM(clock_gettime(CLOCK_REALTIME, &ts) == -1);
	return ts.tv_sec*1E9 + ts.tv_nsec;
}

//微秒
inline uint64_t time_microsecond(){
	struct timeval tv;
    ABORTM(gettimeofday(&tv,NULL)==-1);
	return tv.tv_sec*1E6 + tv.tv_usec;
}

//毫秒
inline uint64_t  time_millisecond(){
	struct timeval tv;
	ABORTM(gettimeofday(&tv,NULL) == -1);
	return tv.tv_sec*1E3 + tv.tv_usec/1000;
}

//秒
inline long  time_second(){
	struct timeval tv;
    ABORTM(gettimeofday(&tv,NULL) == -1);
	return tv.tv_sec;
}

//秒
inline double  time_secondd(){
	struct timeval tv;
    ABORTM(gettimeofday(&tv,NULL) == -1);
	double usec = (double)tv.tv_usec;
	return tv.tv_sec + usec/1000000;
}

#endif

typedef struct tm             ngx_tm_t;

#define ngx_tm_sec            tm_sec
#define ngx_tm_min            tm_min
#define ngx_tm_hour           tm_hour
#define ngx_tm_mday           tm_mday
#define ngx_tm_mon            tm_mon
#define ngx_tm_year           tm_year
#define ngx_tm_wday           tm_wday
#define ngx_tm_isdst          tm_isdst

#define ngx_tm_sec_t          int
#define ngx_tm_min_t          int
#define ngx_tm_hour_t         int
#define ngx_tm_mday_t         int
#define ngx_tm_mon_t          int
#define ngx_tm_year_t         int
#define ngx_tm_wday_t         int


#if (NGX_HAVE_GMTOFF)
#define ngx_tm_gmtoff         tm_gmtoff
#define ngx_tm_zone           tm_zone
#endif

#if (NGX_SOLARIS)
#define ngx_timezone(isdst) (- (isdst ? altzone : timezone) / 60)
#else
#define ngx_timezone(isdst) (- (isdst ? __timezone + 3600 : __timezone) / 60)
#endif

#if (NGX_HAVE_SCHED_YIELD)
#define ngx_sched_yield()  sched_yield()
#else
#ifdef _WIN32
#define ngx_sched_yield()  SwitchToThread()
#elif __linux__
#include <pthread.h>
#define ngx_sched_yield()  pthread_yield()
#else
#define ngx_sched_yield()  usleep(1)
#endif
#endif

#endif