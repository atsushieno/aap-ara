#pragma once
#include <android/log.h>
#include <stdarg.h>

// Use the SDK's documented logging hook; keep imported sources unchanged.
static inline void araExamplesLog(int level, const char* file, int line, const char* format, ...) {
    const int priority = level == 2 ? ANDROID_LOG_ERROR : level == 1 ? ANDROID_LOG_WARN : ANDROID_LOG_INFO;
    if (level) __android_log_print(priority, "ARAExamples", "%s:%d", file, line);
    va_list args;
    va_start(args, format);
    __android_log_vprint(priority, "ARAExamples", format, args);
    va_end(args);
}
#define ARA_DEBUG_MESSAGE(level, file, line, ...) araExamplesLog((int)(ARA_NAMESPACE level), file, line, __VA_ARGS__)
