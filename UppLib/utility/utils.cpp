#include "utils.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <cstring>
#include <Windows.h>

/*
    LOGGER
*/
static void logger_default_log_function(const char* message) {
    printf("%s", message);
}

static void logger_default_panic_function(const char* message) {
    printf("\n\nSYSTEM_PANIC %s", message);
    printf("\n\n");
    system("pause");
    //exit(-1);
}

static custom_log_fn logger_custom_log_fn = &logger_default_log_function;
static custom_panic_fn logger_custom_panic_fn = &logger_default_panic_function;

void logger_set_options(custom_log_fn custom_log_fn, custom_panic_fn custom_panic_fn) {
    if (custom_log_fn != 0) {
        logger_custom_log_fn = custom_log_fn;
    }
    else {
        logger_custom_log_fn = logger_default_log_function;
    }
    if (custom_panic_fn != 0) {
        logger_custom_panic_fn = custom_panic_fn;
    }
    else {
        logger_custom_panic_fn = logger_default_panic_function;
    }
}

static char* logger_message_buffer = nullptr;
static int logger_message_buffer_length = 0;
static const char* LOGGER_PREFIX_FORMAT = "%-10s %04d: ";
bool logger_log_prefix = false;

void logger_log(const char* file_name, int line_number, const char* message_format, ...) 
{
    file_name = strrchr(file_name, '\\') ? strrchr(file_name, '\\') + 1 : file_name;

    va_list variadic_arguments;
    va_start(variadic_arguments, message_format);
    int prefix_length = snprintf(0, 0, LOGGER_PREFIX_FORMAT, file_name, line_number);
    int message_length = vsnprintf(0, 0, message_format, variadic_arguments);

    // Allocate buffer
    const int required_length = prefix_length + message_length + 1;
    if (logger_message_buffer_length < required_length) {
        logger_message_buffer_length = required_length;
        if (logger_message_buffer != nullptr) {
            delete[] logger_message_buffer;
        }
        logger_message_buffer = new char[logger_message_buffer_length+10];
    }

    // Fill buffer
    if (logger_log_prefix) {
        snprintf(logger_message_buffer, logger_message_buffer_length, LOGGER_PREFIX_FORMAT, file_name, line_number);
        const int offset = prefix_length;
        vsnprintf(logger_message_buffer + offset, logger_message_buffer_length - offset, message_format, variadic_arguments);
    }
    else {
        vsnprintf(logger_message_buffer, logger_message_buffer_length, message_format, variadic_arguments);
    }
    va_end(variadic_arguments);

    // Send to custom logging function
    logger_custom_log_fn(logger_message_buffer);
}

void logger_panic(const char* file_name, int line_number, const char* message_format, ...) 
{
    va_list variadic_arguments;
    va_start(variadic_arguments, message_format);
    int prefix_length = snprintf(0, 0, LOGGER_PREFIX_FORMAT, file_name, line_number);
    int message_length = vsnprintf(0, 0, message_format, variadic_arguments);

    // Allocate buffer
    const int required_length = prefix_length + message_length + 1;
    if (logger_message_buffer_length < required_length) {
        logger_message_buffer_length = required_length;
        if (logger_message_buffer != nullptr) {
            delete[] logger_message_buffer;
        }
        logger_message_buffer = new char[logger_message_buffer_length + 10];
    }

    // Fill buffer
    snprintf(logger_message_buffer, logger_message_buffer_length, LOGGER_PREFIX_FORMAT, file_name, line_number);
    const int offset = prefix_length;
    vsnprintf(logger_message_buffer + offset, logger_message_buffer_length - offset, message_format, variadic_arguments);
    va_end(variadic_arguments);

    // Send to custom panic function
   logger_custom_panic_fn(logger_message_buffer);
    __debugbreak();
}

void assert_function(bool condition, const char* condition_as_string, const char* file_name, int line_number, const char* message, ...) {
    if (!condition) {
        file_name = strrchr(file_name, '\\') ? strrchr(file_name, '\\') + 1 : file_name;
        printf("\n\nASSERTION FAILED (%s %4d): \"%s\"\n", file_name, line_number, condition_as_string);
        va_list args;
        va_start(args, message);
        printf("\tMsg: ");
        vprintf(message, args);
        va_end(args);
        __debugbreak();
        panic("ASSERTION FAILED");
    }
}

void memory_copy(void* destination, void* source, u64 size) {
    memcpy(destination, source, size);
}

void memory_set_bytes(void* destination, u64 size, byte value) {
    memset(destination, value, size);
}

bool memory_is_readable(void* destination, u64 read_size)
{
    if (IsBadReadPtr(destination, read_size)) { return false; }
    else {
        return true;
    }
}
