/* Copyright Joyent, Inc. and other Node contributors. All rights reserved.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to
 * deal in the Software without restriction, including without limitation the
 * rights to use, copy, modify, merge, publish, distribute, sublicense, and/or
 * sell copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */


#include <stdlib.h>
#include <stdio.h>
#ifndef _WIN32

#include <pthread.h>
#include <assert.h>
#include <errno.h>

#include <time.h>
#include <sys/time.h>
#include <sys/resource.h>  /* getrlimit() */
#include <unistd.h>  /* getpagesize() */

#include <limits.h>

#ifdef __MVS__
#include <sys/ipc.h>
#include <sys/sem.h>
#endif

#include "thread.h"
#include "log.h"

#undef NANOSEC
#define NANOSEC ((uint64_t) 1e9)

typedef enum {
	UV_CLOCK_PRECISE = 0,  /* Use the highest resolution clock available. */
	UV_CLOCK_FAST = 1      /* Use the fastest clock with <= 1ms granularity. */
} uv_clocktype_t;

static uint64_t uv__hrtime(uv_clocktype_t type) {
	static clock_t fast_clock_id = (clock_t)-1;
	struct timespec t;
	clock_t clock_id;

	/* Prefer CLOCK_MONOTONIC_COARSE if available but only when it has
	* millisecond granularity or better.  CLOCK_MONOTONIC_COARSE is
	* serviced entirely from the vDSO, whereas CLOCK_MONOTONIC may
	* decide to make a costly system call.
	*/
	/* TODO(bnoordhuis) Use CLOCK_MONOTONIC_COARSE for UV_CLOCK_PRECISE
	* when it has microsecond granularity or better (unlikely).
	*/
	if (type == UV_CLOCK_FAST && fast_clock_id == (clock_t)-1) {
		if (clock_getres(CLOCK_MONOTONIC_COARSE, &t) == 0 &&
			t.tv_nsec <= 1 * 1000 * 1000) {
			fast_clock_id = CLOCK_MONOTONIC_COARSE;
		}
		else {
			fast_clock_id = CLOCK_MONOTONIC;
		}
	}

	clock_id = CLOCK_MONOTONIC;
	if (type == UV_CLOCK_FAST)
		clock_id = fast_clock_id;

	if (clock_gettime(clock_id, &t))
		return 0;  /* Not really possible. */

	return t.tv_sec * (uint64_t) 1e9 + t.tv_nsec;
}

int uv_thread_create(uv_thread_t *tid, void (*entry)(void *arg), void *arg) {
  int err;
  pthread_attr_t* attr;
#if defined(__APPLE__)
  pthread_attr_t attr_storage;
  struct rlimit lim;
#endif

  /* On OSX threads other than the main thread are created with a reduced stack
   * size by default, adjust it to RLIMIT_STACK.
   */
#if defined(__APPLE__)
  if (getrlimit(RLIMIT_STACK, &lim)){
    ABORTL(__func__);
  }

  attr = &attr_storage;
  if (pthread_attr_init(attr)){
    ABORTL(__func__);
  }

  if (lim.rlim_cur != RLIM_INFINITY) {
    /* pthread_attr_setstacksize() expects page-aligned values. */
    lim.rlim_cur -= lim.rlim_cur % (rlim_t) getpagesize();

    if (lim.rlim_cur >= PTHREAD_STACK_MIN)
      if (pthread_attr_setstacksize(attr, lim.rlim_cur)){
        ABORTL(__func__);
      }
  }
#else
  attr = NULL;
#endif

  err = pthread_create(tid, attr, (void*(*)(void*)) entry, arg);

  if (attr != NULL)
    pthread_attr_destroy(attr);

  return -err;
}


uv_thread_t uv_thread_self(void) {
  return pthread_self();
}

int uv_thread_join(uv_thread_t *tid) {
  return -pthread_join(*tid, NULL);
}


int uv_thread_equal(const uv_thread_t* t1, const uv_thread_t* t2) {
  return pthread_equal(*t1, *t2);
}


int uv_mutex_init(uv_mutex_t* mutex) {
#if defined(NDEBUG) || !defined(PTHREAD_MUTEX_ERRORCHECK)
  return -pthread_mutex_init(mutex, NULL);
#else
  pthread_mutexattr_t attr;
  int err;

  if (pthread_mutexattr_init(&attr)){
	  ABORTL(__func__);
  }

  if (pthread_mutexattr_settype(&attr, PTHREAD_MUTEX_ERRORCHECK)){
	  ABORTL(__func__);
  }

  err = pthread_mutex_init(mutex, &attr);

  if (pthread_mutexattr_destroy(&attr)){
	  ABORTL(__func__);
  }

  return -err;
#endif
}


void uv_mutex_destroy(uv_mutex_t* mutex) {
  if (pthread_mutex_destroy(mutex)){
    ABORTL(__func__);
  }
}


void uv_mutex_lock(uv_mutex_t* mutex) {
  if (pthread_mutex_lock(mutex)){
    ABORTL(__func__);
  }
}


int uv_mutex_trylock(uv_mutex_t* mutex) {
  int err;

  err = pthread_mutex_trylock(mutex);
  if (err) {
    if (err != EBUSY && err != EAGAIN){
      ABORTL(__func__);
    }
    return -EBUSY;
  }

  return 0;
}


void uv_mutex_unlock(uv_mutex_t* mutex) {
  if (pthread_mutex_unlock(mutex)){
    ABORTL(__func__);
  }
}


int uv_rwlock_init(uv_rwlock_t* rwlock) {
  return -pthread_rwlock_init(rwlock, NULL);
}


void uv_rwlock_destroy(uv_rwlock_t* rwlock) {
  if (pthread_rwlock_destroy(rwlock)){
    ABORTL(__func__);
  }
}


void uv_rwlock_rdlock(uv_rwlock_t* rwlock) {
  if (pthread_rwlock_rdlock(rwlock)){
    ABORTL(__func__);
  }
}


int uv_rwlock_tryrdlock(uv_rwlock_t* rwlock) {
  int err;

  err = pthread_rwlock_tryrdlock(rwlock);
  if (err) {
    if (err != EBUSY && err != EAGAIN){
      ABORTL(__func__);
    }
    return -EBUSY;
  }

  return 0;
}


void uv_rwlock_rdunlock(uv_rwlock_t* rwlock) {
  if (pthread_rwlock_unlock(rwlock)){
    ABORTL(__func__);
  }
}


void uv_rwlock_wrlock(uv_rwlock_t* rwlock) {
  if (pthread_rwlock_wrlock(rwlock)){
    ABORTL(__func__);
  }
}


int uv_rwlock_trywrlock(uv_rwlock_t* rwlock) {
  int err;

  err = pthread_rwlock_trywrlock(rwlock);
  if (err) {
    if (err != EBUSY && err != EAGAIN){
      ABORTL(__func__);
    }
    return -EBUSY;
  }

  return 0;
}


void uv_rwlock_wrunlock(uv_rwlock_t* rwlock) {
  if (pthread_rwlock_unlock(rwlock)){
    ABORTL(__func__);
  }
}


void uv_once(uv_once_t* guard, void (*callback)(void)) {
  if (pthread_once(guard, callback)){
    ABORTL(__func__);
  }
}

#if defined(__APPLE__) && defined(__MACH__)
#ifdef TARGET_OS_MAC
int uv_sem_init( uv_sem_t* sem, unsigned int value )
{
  long ptr = (long)sem;
  sem->name = (char*)malloc(9);
  sprintf(sem->name,"%ld",ptr);
  sem->sem = sem_open( sem->name, O_CREAT, 0644, value );

  if( sem->sem == SEM_FAILED )
  {
    switch( errno )
    {
      case EEXIST:
        // VLOGE("sem_open:EEXIST(%s)",sem->name);
        break;

      default:
        // VLOGE("sem_open:%d",errno);
        break;
    }
    return -EINVAL;
  }

  return 0;
}


int uv_sem__delete(const char * name)
{
    int ret = sem_unlink(name);
    if (ret == -1) {
        // VLOGE("sem_unlink:%d",errno);
        return -1;
    }
    return 0;
}

void uv_sem_destroy( uv_sem_t * sem )
{
  int ret = sem_close( sem->sem );
  // VASSERT(sem->name != NULL);
  uv_sem__delete(sem->name);
  free(sem->name);
  if( ret == -1 )
  {
    switch( errno )
    {
            case EINVAL:
                // VLOGE("sem_close:EINVAL");
                break;

            default:
                // VLOGE("sem_close:%d",errno);
                break;
    }
  }
}

void uv_sem_post( uv_sem_t * sem )
{
  int ret = sem_post( sem->sem );
  // LOGA(ret != 0,"sem_post:%d",errno)
}

void uv_sem_wait( uv_sem_t * sem )
{
  int ret = sem_wait( sem->sem );
  // LOGA(ret != 0,"sem_wait:%d",errno)
}

int uv_sem_trywait( uv_sem_t * sem )
{
  int retErr = sem_trywait( sem->sem );
  if( retErr == -1 )
  {
    // LOGA(errno != EAGAIN,"sem_trywait:%d",errno);
    return -1;
  }
  return 0;
}
#else
int uv_sem_init(uv_sem_t* sem, unsigned int value) {
  kern_return_t err;

  err = semaphore_create(mach_task_self(), sem, SYNC_POLICY_FIFO, value);
  if (err == KERN_SUCCESS)
    return 0;
  if (err == KERN_INVALID_ARGUMENT)
    return -EINVAL;
  if (err == KERN_RESOURCE_SHORTAGE)
    return -ENOMEM;

  ABORTL(__func__);
  return -EINVAL;  /* Satisfy the compiler. */
}


void uv_sem_destroy(uv_sem_t* sem) {
  if (semaphore_destroy(mach_task_self(), *sem)){
    ABORTL(__func__);
  }
}


void uv_sem_post(uv_sem_t* sem) {
  if (semaphore_signal(*sem)){
    ABORTL(__func__);
  }
}


void uv_sem_wait(uv_sem_t* sem) {
  int r;

  do
    r = semaphore_wait(*sem);
  while (r == KERN_ABORTED);

  if (r != KERN_SUCCESS){
    ABORTL(__func__);
  }
}


int uv_sem_trywait(uv_sem_t* sem) {
  mach_timespec_t interval;
  kern_return_t err;

  interval.tv_sec = 0;
  interval.tv_nsec = 0;

  err = semaphore_timedwait(*sem, interval);
  if (err == KERN_SUCCESS)
    return 0;
  if (err == KERN_OPERATION_TIMED_OUT)
    return -EAGAIN;

  ABORTL();
  return -EINVAL;  /* Satisfy the compiler. */
}
#endif
#elif defined(__MVS__)

int uv_sem_init(uv_sem_t* sem, unsigned int value) {
  uv_sem_t semid;
  struct sembuf buf;
  int err;

  buf.sem_num = 0;
  buf.sem_op = value;
  buf.sem_flg = 0;

  semid = semget(IPC_PRIVATE, 1, S_IRUSR | S_IWUSR);
  if (semid == -1)
    return -errno;

  if (-1 == semop(semid, &buf, 1)) {
    err = errno;
    if (-1 == semctl(*sem, 0, IPC_RMID)){
      ABORTL(__func__);
    }
    return -err;
  }

  *sem = semid;
  return 0;
}

void uv_sem_destroy(uv_sem_t* sem) {
  if (-1 == semctl(*sem, 0, IPC_RMID)){
    ABORTL(__func__);
  }
}

void uv_sem_post(uv_sem_t* sem) {
  struct sembuf buf;

  buf.sem_num = 0;
  buf.sem_op = 1;
  buf.sem_flg = 0;

  if (-1 == semop(*sem, &buf, 1)){
    ABORTL(__func__);
  }
}

void uv_sem_wait(uv_sem_t* sem) {
  struct sembuf buf;
  int op_status;

  buf.sem_num = 0;
  buf.sem_op = -1;
  buf.sem_flg = 0;

  do
    op_status = semop(*sem, &buf, 1);
  while (op_status == -1 && errno == EINTR);

  if (op_status){
    ABORTL(__func__);
  }
}

int uv_sem_trywait(uv_sem_t* sem) {
  struct sembuf buf;
  int op_status;

  buf.sem_num = 0;
  buf.sem_op = -1;
  buf.sem_flg = IPC_NOWAIT;

  do
    op_status = semop(*sem, &buf, 1);
  while (op_status == -1 && errno == EINTR);

  if (op_status) {
    if (errno == EAGAIN)
      return -EAGAIN;
    ABORTL(__func__);
  }

  return 0;
}

#else /* !(defined(__APPLE__) && defined(__MACH__)) */

int uv_sem_init(uv_sem_t* sem, unsigned int value) {
  if (sem_init(sem, 0, value))
    return -errno;
  return 0;
}


void uv_sem_destroy(uv_sem_t* sem) {
  if (sem_destroy(sem)){
    ABORTL(__func__);
  }
}


void uv_sem_post(uv_sem_t* sem) {
  if (sem_post(sem)){
    ABORTL(__func__);
  }
}


void uv_sem_wait(uv_sem_t* sem) {
  int r;

  do
    r = sem_wait(sem);
  while (r == -1 && errno == EINTR);

  if (r){
    ABORTL(__func__);
  }
}


int uv_sem_trywait(uv_sem_t* sem) {
  int r;

  do
    r = sem_trywait(sem);
  while (r == -1 && errno == EINTR);

  if (r) {
    if (errno == EAGAIN)
      return -EAGAIN;
    ABORTL(__func__);
  }

  return 0;
}

#endif /* defined(__APPLE__) && defined(__MACH__) */


#if defined(__APPLE__) && defined(__MACH__) || defined(__MVS__)

int uv_cond_init(uv_cond_t* cond) {
  return -pthread_cond_init(cond, NULL);
}

#else /* !(defined(__APPLE__) && defined(__MACH__)) */

int uv_cond_init(uv_cond_t* cond) {
  pthread_condattr_t attr;
  int err;

  err = pthread_condattr_init(&attr);
  if (err)
    return -err;

#if !(defined(__ANDROID__) && defined(HAVE_PTHREAD_COND_TIMEDWAIT_MONOTONIC))
  err = pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
  if (err)
    goto error2;
#endif

  err = pthread_cond_init(cond, &attr);
  if (err)
    goto error2;

  err = pthread_condattr_destroy(&attr);
  if (err)
    goto error;

  return 0;

error:
  pthread_cond_destroy(cond);
error2:
  pthread_condattr_destroy(&attr);
  return -err;
}

#endif /* defined(__APPLE__) && defined(__MACH__) */

void uv_cond_destroy(uv_cond_t* cond) {
#if defined(__APPLE__) && defined(__MACH__)
  /* It has been reported that destroying condition variables that have been
   * signalled but not waited on can sometimes result in application crashes.
   * See https://codereview.chromium.org/1323293005.
   */
  pthread_mutex_t mutex;
  struct timespec ts;
  int err;

  if (pthread_mutex_init(&mutex, NULL)){
    ABORTL(__func__);
  }

  if (pthread_mutex_lock(&mutex)){
    ABORTL(__func__);
  }

  ts.tv_sec = 0;
  ts.tv_nsec = 1;

  err = pthread_cond_timedwait_relative_np(cond, &mutex, &ts);
  if (err != 0 && err != ETIMEDOUT){
    ABORTL(__func__);
  }

  if (pthread_mutex_unlock(&mutex)){
    ABORTL(__func__);
  }

  if (pthread_mutex_destroy(&mutex)){
    ABORTL(__func__);
  }
#endif /* defined(__APPLE__) && defined(__MACH__) */

  if (pthread_cond_destroy(cond)){
    ABORTL(__func__);
  }
}

void uv_cond_signal(uv_cond_t* cond) {
  if (pthread_cond_signal(cond)){
    ABORTL(__func__);
  }
}

void uv_cond_broadcast(uv_cond_t* cond) {
  if (pthread_cond_broadcast(cond)){
    ABORTL(__func__);
  }
}

void uv_cond_wait(uv_cond_t* cond, uv_mutex_t* mutex) {
  if (pthread_cond_wait(cond, mutex)){
    ABORTL(__func__);
  }
}


int uv_cond_timedwait(uv_cond_t* cond, uv_mutex_t* mutex, uint64_t timeout) {
  int r;
  struct timespec ts;

#if defined(__APPLE__) && defined(__MACH__)
  ts.tv_sec = timeout / NANOSEC;
  ts.tv_nsec = timeout % NANOSEC;
  r = pthread_cond_timedwait_relative_np(cond, mutex, &ts);
#else
  timeout += uv__hrtime(UV_CLOCK_PRECISE);
  ts.tv_sec = timeout / NANOSEC;
  ts.tv_nsec = timeout % NANOSEC;
#if defined(__ANDROID__) && defined(HAVE_PTHREAD_COND_TIMEDWAIT_MONOTONIC)
  /*
   * The bionic pthread implementation doesn't support CLOCK_MONOTONIC,
   * but has this alternative function instead.
   */
  r = pthread_cond_timedwait_monotonic_np(cond, mutex, &ts);
#else
  r = pthread_cond_timedwait(cond, mutex, &ts);
#endif /* __ANDROID__ */
#endif


  if (r == 0)
    return 0;

  if (r == ETIMEDOUT)
    return -ETIMEDOUT;

  ABORTL(__func__);
  return -EINVAL;  /* Satisfy the compiler. */
}

#ifndef USE_USERDEFINED_BARRIER
int uv_barrier_init(uv_barrier_t* barrier, unsigned int count) {
  return -pthread_barrier_init(barrier, NULL, count);
}


void uv_barrier_destroy(uv_barrier_t* barrier) {
  if (pthread_barrier_destroy(barrier)){
	ABORTL(__func__);
  }
}


int uv_barrier_wait(uv_barrier_t* barrier) {
  int r = pthread_barrier_wait(barrier);
  if (r && r != PTHREAD_BARRIER_SERIAL_THREAD){
    ABORTL(__func__);
  }
  return r == PTHREAD_BARRIER_SERIAL_THREAD;
}
#endif

int uv_key_create(uv_key_t* key) {
  return -pthread_key_create(key, NULL);
}


void uv_key_delete(uv_key_t* key) {
  if (pthread_key_delete(*key)){
    ABORTL(__func__);
  }
}


void* uv_key_get(uv_key_t* key) {
  return pthread_getspecific(*key);
}


void uv_key_set(uv_key_t* key, void* value) {
  if (pthread_setspecific(*key, value)){
    ABORTL(__func__);
  }
}

#endif

#ifdef USE_USERDEFINED_BARRIER
int uv_barrier_init(uv_barrier_t* barrier, unsigned int count) {
  int err;

  barrier->n = count;
  barrier->count = 0;

  err = uv_mutex_init(&barrier->mutex);
  if (err)
    return err;

  err = uv_sem_init(&barrier->turnstile1, 0);
  if (err)
    goto error2;

  err = uv_sem_init(&barrier->turnstile2, 1);
  if (err)
    goto error;

  return 0;

error:
  uv_sem_destroy(&barrier->turnstile1);
error2:
  uv_mutex_destroy(&barrier->mutex);
  return err;

}


void uv_barrier_destroy(uv_barrier_t* barrier) {
  uv_sem_destroy(&barrier->turnstile2);
  uv_sem_destroy(&barrier->turnstile1);
  uv_mutex_destroy(&barrier->mutex);
}


int uv_barrier_wait(uv_barrier_t* barrier) {
  int serial_thread;

  uv_mutex_lock(&barrier->mutex);
  if (++barrier->count == barrier->n) {
    uv_sem_wait(&barrier->turnstile2);
    uv_sem_post(&barrier->turnstile1);
  }
  uv_mutex_unlock(&barrier->mutex);

  uv_sem_wait(&barrier->turnstile1);
  uv_sem_post(&barrier->turnstile1);

  uv_mutex_lock(&barrier->mutex);
  serial_thread = (--barrier->count == 0);
  if (serial_thread) {
    uv_sem_wait(&barrier->turnstile1);
    uv_sem_post(&barrier->turnstile2);
  }
  uv_mutex_unlock(&barrier->mutex);

  uv_sem_wait(&barrier->turnstile2);
  uv_sem_post(&barrier->turnstile2);
  return serial_thread;
}
#endif
