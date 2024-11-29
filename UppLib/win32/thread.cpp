#include "thread.hpp"

#include <Windows.h>
#include "../utility/utils.hpp"

Thread thread_create(thread_start_fn start_fn, void* userdata)
{
    Thread result;
    LPTHREAD_START_ROUTINE start_routine = start_fn;
    result.handle = CreateThread(
        NULL,
        0,
        start_fn,
        userdata,
        0,
        NULL
    );
    assert(result.handle != NULL, "");
    return result;
}

bool thread_is_finished(Thread thread) {
    DWORD result = WaitForSingleObject(thread.handle, 0);
    if (result == WAIT_OBJECT_0) {
        return true;
    }
    return false;
}

void wait_for_thread_to_finish(Thread thread) {
    WaitForSingleObject(thread.handle, INFINITE);
}

void thread_destroy(Thread thread) {
    CloseHandle(thread.handle);
}

Semaphore semaphore_create(int initial_count, int max_count)
{
    Semaphore result;
    result.handle = CreateSemaphoreA(
        NULL,
        initial_count,
        max_count,
        NULL
    );
    assert(result.handle != NULL, "");
    return result;
}

void semaphore_destroy(Semaphore semaphore) {
    CloseHandle(semaphore.handle);
}

void semaphore_wait(Semaphore semaphore) {
    WaitForSingleObject(semaphore.handle, INFINITE);
}

bool semaphore_try_wait(Semaphore semaphore) {
    DWORD result = WaitForSingleObject(semaphore.handle, 0);
    if (result == WAIT_OBJECT_0) {
        return true;
    }
    return false;
}

void semaphore_increment(Semaphore semaphore, int count) {
    ReleaseSemaphore(semaphore.handle, count, NULL);
}
