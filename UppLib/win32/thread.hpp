#pragma once

#include "../utility/datatypes.hpp"

struct Thread {
    void* handle;
};

typedef unsigned long (__stdcall *thread_start_fn) (void*);
Thread thread_create(thread_start_fn start_fn, void* userdata);
bool thread_is_finished(Thread thread);
void wait_for_thread_to_finish(Thread thread);
void thread_destroy(Thread thread);

struct Semaphore {
    void* handle;
};

Semaphore semaphore_create(int initial_count, int max_count);
void semaphore_destroy(Semaphore semaphore);
void semaphore_wait(Semaphore semaphore);
bool semaphore_try_wait(Semaphore semaphore); // Returns true if semaphore was aquired (count decremented)
void semaphore_increment(Semaphore semaphore, int count);

