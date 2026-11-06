#ifndef LOGGER_H
#define LOGGER_H

#include <spdlog/spdlog.h>
#include <spdlog/sinks/dist_sink.h>
#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <utility>
#include <vector>

namespace vke_common
{
    using LogID = uint64_t;

    enum class LoggerOutput
    {
        Console,
        EngineUI
    };

    struct LogEntry
    {
        LogID id;
        std::chrono::system_clock::time_point timestamp;
        spdlog::level::level_enum level;
        std::string message;
    };

    class Logger
    {
    private:
        static Logger *instance;
        Logger() = default;
        ~Logger() = default;
        Logger(const Logger &) = delete;
        Logger &operator=(const Logger &) = delete;

    public:
        static Logger *Init(LoggerOutput output);
        static Logger *GetInstance();
        static void Dispose();
        static void SetOutput(LoggerOutput output);
        static LogID GetLatestEntryID();
        static void ClearEntries();

        // Visits retained entries in ascending ID order. Entries older than the
        // ring buffer's current oldest entry are silently skipped. The callback
        // runs while the log buffer is locked and must not log or change output.
        template <typename Operation>
        static size_t IterateEntries(LogID startID, LogID endID, Operation &&operation)
        {
            Logger *loggerInstance = GetInstance();
            std::lock_guard<std::mutex> lock(loggerInstance->entriesMutex);
            const std::vector<LogEntry> &entries = loggerInstance->entries;
            if (entries.empty() || startID > endID)
                return 0;

            const size_t oldestIndex = entries.size() == MaxBufferedEntries
                                           ? loggerInstance->nextEntryIndex
                                           : 0;
            const LogID oldestID = entries[oldestIndex].id;
            const size_t newestIndex = (oldestIndex + entries.size() - 1) % entries.size();
            const LogID newestID = entries[newestIndex].id;
            const LogID visitStartID = startID < oldestID ? oldestID : startID;
            const LogID visitEndID = endID > newestID ? newestID : endID;
            if (visitStartID > visitEndID)
                return 0;

            const size_t startOffset = static_cast<size_t>(visitStartID - oldestID);
            const size_t visitCount = static_cast<size_t>(visitEndID - visitStartID + 1);
            for (size_t i = 0; i < visitCount; ++i)
                operation(entries[(oldestIndex + startOffset + i) % entries.size()]);
            return visitCount;
        }

        template <typename... Args>
        static void Trace(spdlog::format_string_t<Args...> fmt, Args &&...args)
        {
            GetInstance()->logger->trace(fmt, std::forward<Args>(args)...);
        }

        template <typename... Args>
        static void Debug(spdlog::format_string_t<Args...> fmt, Args &&...args)
        {
            GetInstance()->logger->debug(fmt, std::forward<Args>(args)...);
        }

        template <typename... Args>
        static void Info(spdlog::format_string_t<Args...> fmt, Args &&...args)
        {
            GetInstance()->logger->info(fmt, std::forward<Args>(args)...);
        }

        template <typename... Args>
        static void Warn(spdlog::format_string_t<Args...> fmt, Args &&...args)
        {
            GetInstance()->logger->warn(fmt, std::forward<Args>(args)...);
        }

        template <typename... Args>
        static void Error(spdlog::format_string_t<Args...> fmt, Args &&...args)
        {
            GetInstance()->logger->error(fmt, std::forward<Args>(args)...);
        }

        template <typename T>
        static void Trace(const T &msg) { GetInstance()->logger->trace(msg); }
        template <typename T>
        static void Debug(const T &msg) { GetInstance()->logger->debug(msg); }
        template <typename T>
        static void Info(const T &msg) { GetInstance()->logger->info(msg); }
        template <typename T>
        static void Warn(const T &msg) { GetInstance()->logger->warn(msg); }
        template <typename T>
        static void Error(const T &msg) { GetInstance()->logger->error(msg); }

    private:
        static constexpr size_t MaxBufferedEntries = 8192;

        std::shared_ptr<spdlog::logger> logger;
        std::shared_ptr<spdlog::sinks::dist_sink_mt> outputSink;
        std::mutex entriesMutex;
        std::vector<LogEntry> entries;
        size_t nextEntryIndex = 0;
        LogID nextEntryID = 1;

        void init(LoggerOutput output);
        void setOutput(LoggerOutput output);
        void appendEntry(const spdlog::details::log_msg &message);
    };

#define VKE_LOG_TRACE(...) vke_common::Logger::Trace(__VA_ARGS__);
#define VKE_LOG_DEBUG(...) vke_common::Logger::Debug(__VA_ARGS__);
#define VKE_LOG_INFO(...) vke_common::Logger::Info(__VA_ARGS__);
#define VKE_LOG_WARN(...) vke_common::Logger::Warn(__VA_ARGS__);
#define VKE_LOG_ERROR(...) vke_common::Logger::Error(__VA_ARGS__);
}

#endif
