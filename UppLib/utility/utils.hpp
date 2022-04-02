#pragma once

#include "datatypes.hpp"

/*
    LOGGING
*/
#define logg(message_format, ...) logger_log(__FILE__, __LINE__, message_format, __VA_ARGS__)
#define panic(message_format, ...) logger_panic(__FILE__, __LINE__, message_format, __VA_ARGS__)

typedef void(*custom_log_fn)(const char* message);
typedef void(*custom_panic_fn)(const char* message);

void logger_set_options(custom_log_fn custom_log_fn, custom_panic_fn custom_panic_fn);
void logger_log(const char* file_name, int line_number, const char* message_format, ...);
void logger_panic(const char* file_name, int line_number, const char* message_format, ...);

/*
    ASSERTIONS
*/
#define assert(condition, format, ...) assert_function(condition, #condition, __FILE__, __LINE__, format, __VA_ARGS__)
void assert_function(bool condition, const char* condition_as_string, const char* file_name, int line_number, const char* message, ...);

/*
    SCOPE_EXIT
*/
template <typename F>
struct ScopeExit {
    ScopeExit(F f) : f(f) {}
    ~ScopeExit() { f(); }
    F f;
};

template <typename F>
ScopeExit<F> makeScopeExit(F f) {
    return ScopeExit<F>(f);
}

#define STRING_JOIN2(arg1, arg2) DO_STRING_JOIN2(arg1, arg2)
#define DO_STRING_JOIN2(arg1, arg2) arg1 ## arg2
#define SCOPE_EXIT(code) \
    auto STRING_JOIN2(_scope_exit_, __LINE__) = makeScopeExit([&](){code;})

/*
    Optional
*/
template <typename T>
struct Optional {
    bool available;
    T value;
};

template <typename T>
T optional_unwrap(Optional<T> optional) {
    if (!optional.available) {
        panic("Optional was not available");
    }
    return optional.value;
}

template <typename T>
Optional<T> optional_make_failure() {
    Optional<T> result;
    result.available = false;
    return result;
}

template <typename T>
Optional<T> optional_make_success(T value) {
    Optional<T> result;
    result.value = value;
    result.available = true;
    return result;
}


/*
    MEMORY STUFF
*/
void memory_copy(void* destination, void* source, u64 size);
void memory_set_bytes(void* destination, u64 size, byte value);
bool memory_is_readable(void* destination, u64 read_size);
bool memory_compare(void* memory_a, void* memory_b, u64 compare_size);

template<typename T>
void memory_zero(T* ptr) {
    memory_set_bytes(ptr, sizeof(T), 0);
}


