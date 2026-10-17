#pragma once

#include <atomic>
#include <cstdarg>
#include <cstdio>
#include <mutex>

namespace anyslider
{
inline std::atomic_bool debugLoggingEnabled = false;

inline void SetDebugLoggingEnabled(bool enabled)
{
    debugLoggingEnabled.store(enabled, std::memory_order_relaxed);
}

inline bool IsDebugLoggingEnabled()
{
    return debugLoggingEnabled.load(std::memory_order_relaxed);
}

inline void LogV(const char* prefix, const char* format, va_list args)
{
    // Serialize whole records across game, device and diagnostic threads.
    static std::mutex mutex;
    std::lock_guard lock(mutex);
    va_list copy;
    va_copy(copy, args);
    std::fputs(prefix, stdout);
    std::vfprintf(stdout, format, copy);
    va_end(copy);
    std::fputc('\n', stdout);
    std::fflush(stdout);

    struct LogFile
    {
        FILE* handle = nullptr;

        LogFile()
        {
            // 打开失败仍保留stdout日志；不要在日志初始化中递归记录错误。
            // 固定路径和模式不涉及外部缓冲区；保留标准fopen的行为和可移植性。
            // 仅此调用忽略微软CRT弃用提示，不隐藏其他调用的潜在问题。
#if defined(_MSC_VER) && defined(_WIN32)
#pragma warning(push)
#pragma warning(disable: 4996)
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
#endif
#endif
            handle = std::fopen("anyslider.log", "a");
#if defined(_MSC_VER) && defined(_WIN32)
#if defined(__clang__)
#pragma clang diagnostic pop
#endif
#pragma warning(pop)
#endif
        }

        ~LogFile()
        {
            if (handle)
            {
                std::fclose(handle);
            }
        }
    };
    static LogFile file;
    if (file.handle)
    {
        va_copy(copy, args);
        std::fputs(prefix, file.handle);
        std::vfprintf(file.handle, format, copy);
        va_end(copy);
        std::fputc('\n', file.handle);
        std::fflush(file.handle);
    }
}

inline void Log(const char* format, ...)
{
    va_list args;
    va_start(args, format);
    LogV("[AnySlider] ", format, args);
    va_end(args);
}

inline void DebugLog(const char* format, ...)
{
    if (!IsDebugLoggingEnabled())
    {
        return;
    }
    va_list args;
    va_start(args, format);
    LogV("[AnySlider Debug] ", format, args);
    va_end(args);
}
}
