#ifndef SCOPE_THREAD_H
#define SCOPE_THREAD_H
#include <stdint.h>
#ifdef _WIN32
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0602
#endif
#include <windows.h>
typedef CRITICAL_SECTION ScopeMutex;
typedef CONDITION_VARIABLE ScopeCondition;
typedef struct {
    HANDLE handle;
    void (*function)(void *);
    void *context;
} ScopeThread;
#else
#include <pthread.h>
typedef pthread_mutex_t ScopeMutex;
typedef pthread_cond_t ScopeCondition;
typedef struct {
    pthread_t handle;
    void (*function)(void *);
    void *context;
} ScopeThread;
#endif
void scope_mutex_init(ScopeMutex *mutex);
void scope_mutex_destroy(ScopeMutex *mutex);
void scope_mutex_lock(ScopeMutex *mutex);
void scope_mutex_unlock(ScopeMutex *mutex);
void scope_condition_init(ScopeCondition *condition);
void scope_condition_destroy(ScopeCondition *condition);
void scope_condition_signal(ScopeCondition *condition);
void scope_condition_wait(ScopeCondition *condition, ScopeMutex *mutex, unsigned timeout_ms);
int scope_thread_start(ScopeThread *thread, void (*function)(void *), void *context);
void scope_thread_join(ScopeThread *thread);
uint64_t scope_clock_ns(void);
uint64_t scope_process_cpu_ns(void);
void scope_sleep_ms(unsigned milliseconds);
#endif
