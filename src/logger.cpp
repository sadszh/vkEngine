#include <logger.hpp>
#include <spdlog/sinks/callback_sink.h>
#include <spdlog/sinks/stdout_color_sinks.h>

namespace vke_common
{
    Logger *Logger::instance = nullptr;

    Logger *Logger::Init(LoggerOutput output)
    {
        if (instance == nullptr)
        {
            instance = new Logger();
            instance->init(output);
        }
        return instance;
    }

    Logger *Logger::GetInstance()
    {
        if (instance == nullptr)
            Init(LoggerOutput::Console);
        return instance;
    }

    void Logger::Dispose()
    {
        if (instance == nullptr)
            return;
        spdlog::drop("vkEngine");
        delete instance;
        instance = nullptr;
    }

    LogID Logger::GetLatestEntryID()
    {
        Logger *loggerInstance = GetInstance();
        std::lock_guard<std::mutex> lock(loggerInstance->entriesMutex);
        return loggerInstance->nextEntryID - 1;
    }

    void Logger::ClearEntries()
    {
        Logger *loggerInstance = GetInstance();
        std::lock_guard<std::mutex> lock(loggerInstance->entriesMutex);
        loggerInstance->entries.clear();
        loggerInstance->nextEntryIndex = 0;
    }

    void Logger::init(LoggerOutput output)
    {
        entries.reserve(MaxBufferedEntries);
        outputSink = std::make_shared<spdlog::sinks::dist_sink_mt>();
        setOutput(output);
        logger = std::make_shared<spdlog::logger>("vkEngine", outputSink);
        spdlog::register_logger(logger);

#ifdef VKE_DEBUG
        logger->set_level(spdlog::level::debug);
#endif
    }

    void Logger::SetOutput(LoggerOutput output)
    {
        GetInstance()->setOutput(output);
    }

    void Logger::setOutput(LoggerOutput output)
    {
        spdlog::sink_ptr sink;
        if (output == LoggerOutput::EngineUI)
        {
            sink = std::make_shared<spdlog::sinks::callback_sink_mt>(
                [this](const spdlog::details::log_msg &message)
                { appendEntry(message); });
        }
        else
        {
            sink = std::make_shared<spdlog::sinks::stdout_color_sink_mt>();
        }
        outputSink->set_sinks({std::move(sink)});
    }

    void Logger::appendEntry(const spdlog::details::log_msg &message)
    {
        std::lock_guard<std::mutex> lock(entriesMutex);
        LogEntry entry{nextEntryID++, message.time, message.level,
                       std::string(message.payload.data(), message.payload.size())};

        if (entries.size() < MaxBufferedEntries)
            entries.push_back(std::move(entry));
        else
            entries[nextEntryIndex] = std::move(entry);

        nextEntryIndex = (nextEntryIndex + 1) % MaxBufferedEntries;
    }
}
