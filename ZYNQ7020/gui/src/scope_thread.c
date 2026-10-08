#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "scope_thread.h"
#ifdef _WIN32
void scope_mutex_init(ScopeMutex *m) { InitializeCriticalSection(m); }
void scope_mutex_destroy(ScopeMutex *m) { DeleteCriticalSection(m); }
void scope_mutex_lock(ScopeMutex *m) { EnterCriticalSection(m); }
void scope_mutex_unlock(ScopeMutex *m) { LeaveCriticalSection(m); }
void scope_condition_init(ScopeCondition *c) { InitializeConditionVariable(c); }
void scope_condition_destroy(ScopeCondition *c) { (void)c; }
void scope_condition_signal(ScopeCondition *c) { WakeAllConditionVariable(c); }
void scope_condition_wait(ScopeCondition *c, ScopeMutex *m, unsigned ms)
{
    SleepConditionVariableCS(c, m, ms);
}
static DWORD WINAPI run_thread(LPVOID context)
{
    ScopeThread *t = context;
    t->function(t->context);
    return 0;
}
int scope_thread_start(ScopeThread *t, void (*f)(void *), void *c)
{
    t->function = f;
    t->context = c;
    t->handle = CreateThread(NULL, 0, run_thread, t, 0, NULL);
    return t->handle != NULL;
}
void scope_thread_join(ScopeThread *t)
{
    WaitForSingleObject(t->handle, INFINITE);
    CloseHandle(t->handle);
}
uint64_t scope_clock_ns(void)
{
    LARGE_INTEGER count, frequency;
    QueryPerformanceCounter(&count);
    QueryPerformanceFrequency(&frequency);
    return (uint64_t)(count.QuadPart / frequency.QuadPart) * UINT64_C(1000000000) +
           (uint64_t)(count.QuadPart % frequency.QuadPart) * UINT64_C(1000000000) /
               (uint64_t)frequency.QuadPart;
}
uint64_t scope_process_cpu_ns(void)
{
    FILETIME creation, exit, kernel, user;
    ULARGE_INTEGER k, u;
    if (!GetProcessTimes(GetCurrentProcess(), &creation, &exit, &kernel, &user))
        return 0;
    k.LowPart = kernel.dwLowDateTime;
    k.HighPart = kernel.dwHighDateTime;
    u.LowPart = user.dwLowDateTime;
    u.HighPart = user.dwHighDateTime;
    return (k.QuadPart + u.QuadPart) * 100;
}
void scope_sleep_ms(unsigned ms)
{
    HANDLE timer;
    LARGE_INTEGER due;
    if (!ms) {
        Sleep(0);
        return;
    }
    timer = CreateWaitableTimerExW(NULL, NULL, 0x2, TIMER_ALL_ACCESS);
    if (!timer) {
        Sleep(ms);
        return;
    }
    due.QuadPart = -(LONGLONG)ms * 10000;
    if (SetWaitableTimer(timer, &due, 0, NULL, NULL, FALSE))
        WaitForSingleObject(timer, INFINITE);
    else
        Sleep(ms);
    CloseHandle(timer);
}
#else
#include <errno.h>
#include <time.h>
void scope_mutex_init(ScopeMutex *m) { pthread_mutex_init(m, NULL); }
void scope_mutex_destroy(ScopeMutex *m) { pthread_mutex_destroy(m); }
void scope_mutex_lock(ScopeMutex *m) { pthread_mutex_lock(m); }
void scope_mutex_unlock(ScopeMutex *m) { pthread_mutex_unlock(m); }
void scope_condition_init(ScopeCondition *c) { pthread_cond_init(c, NULL); }
void scope_condition_destroy(ScopeCondition *c) { pthread_cond_destroy(c); }
void scope_condition_signal(ScopeCondition *c) { pthread_cond_broadcast(c); }
void scope_condition_wait(ScopeCondition *c, ScopeMutex *m, unsigned ms)
{
    struct timespec end;
    clock_gettime(CLOCK_REALTIME, &end);
    end.tv_sec += ms / 1000;
    end.tv_nsec += (long)(ms % 1000) * 1000000;
    if (end.tv_nsec >= 1000000000) {
        ++end.tv_sec;
        end.tv_nsec -= 1000000000;
    }
    pthread_cond_timedwait(c, m, &end);
}
static void *run_thread(void *context)
{
    ScopeThread *t = context;
    t->function(t->context);
    return NULL;
}
int scope_thread_start(ScopeThread *t, void (*f)(void *), void *c)
{
    t->function = f;
    t->context = c;
    return pthread_create(&t->handle, NULL, run_thread, t) == 0;
}
void scope_thread_join(ScopeThread *t) { pthread_join(t->handle, NULL); }
uint64_t scope_clock_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}
uint64_t scope_process_cpu_ns(void)
{
    struct timespec now;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &now);
    return (uint64_t)now.tv_sec * UINT64_C(1000000000) + (uint64_t)now.tv_nsec;
}
void scope_sleep_ms(unsigned ms)
{
    struct timespec t = {ms / 1000, (long)(ms % 1000) * 1000000};
    while (nanosleep(&t, &t) < 0 && errno == EINTR) {
    }
}
#endif
