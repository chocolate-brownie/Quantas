#ifndef QUANTAS_COMMON_LOGGER_HPP
#define QUANTAS_COMMON_LOGGER_HPP

#include <atomic>
#include <fstream>
#include <functional>
#include <iosfwd>
#include <memory>
#include <mutex>
#include <optional>
#include <shared_mutex>
#include <sstream>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>

namespace quantas {

enum class LogLevel : int {
    Off = -1,
    Error = 0,
    Warn,
    Info,
    Debug,
    Trace
};

class LogLine;

class Logger {
public:
    static Logger& instance();

    static void setDestination(const std::string& path, bool append = true);
    static void setGlobalLevel(LogLevel level);
    static void setCategoryLevel(const std::string& category, LogLevel level);
    static void clearCategoryLevel(const std::string& category);
    static void clearAllCategoryLevels();
    static void disable();

    static bool shouldLog(std::string_view category, LogLevel level) noexcept;
    static void log(LogLevel level, std::string_view category, std::string_view message);

    template <typename MessageFn>
    static void log(LogLevel level, std::string_view category, MessageFn&& messageFn) {
        Logger& inst = instance();
        if (!inst.shouldLogImpl(category, level)) {
            return;
        }
        inst.write(level, category, std::forward<MessageFn>(messageFn));
    }

    static LogLine line(LogLevel level, std::string_view category);

    static std::string_view levelToString(LogLevel level) noexcept;
    static std::optional<LogLevel> levelFromString(std::string_view levelName) noexcept;

private:
    friend class LogLine;

    Logger();
    ~Logger();

    bool shouldLogImpl(std::string_view category, LogLevel level) const noexcept;
    void write(std::string_view category, LogLevel level, std::string_view message);

    template <typename MessageFn>
    void write(LogLevel level, std::string_view category, MessageFn&& messageFn) {
        std::string message = messageFn();
        write(category, level, message);
    }

    mutable std::shared_mutex _configMutex;
    std::unordered_map<std::string, LogLevel> _categoryLevels;
    std::atomic<int> _globalLevel;

    std::mutex _writeMutex;
    std::ofstream _fileStream;
    std::ostream* _stream;
};

class LogLine {
public:
    LogLine(Logger& logger, std::string category, LogLevel level, bool active) noexcept;
    LogLine(LogLine&& other) noexcept;
    LogLine& operator=(LogLine&& other) noexcept;

    LogLine(const LogLine&) = delete;
    LogLine& operator=(const LogLine&) = delete;

    ~LogLine();

    std::ostream& stream();
    explicit operator bool() const noexcept { return _active; }

private:
    static std::ostream& nullStream() noexcept;

    Logger* _logger{nullptr};
    std::string _category;
    LogLevel _level{LogLevel::Info};
    bool _active{false};
    std::unique_ptr<std::ostringstream> _buffer;
};

#define QUANTAS_LOG(level, category)                                                                      \
    if (auto _quantas_log_line = ::quantas::Logger::line((level), (category)); !_quantas_log_line) {       \
    } else                                                                                                 \
        _quantas_log_line.stream()

#define QUANTAS_LOG_ERROR(category) QUANTAS_LOG(::quantas::LogLevel::Error, (category))
#define QUANTAS_LOG_WARN(category) QUANTAS_LOG(::quantas::LogLevel::Warn, (category))
#define QUANTAS_LOG_INFO(category) QUANTAS_LOG(::quantas::LogLevel::Info, (category))
#define QUANTAS_LOG_DEBUG(category) QUANTAS_LOG(::quantas::LogLevel::Debug, (category))
#define QUANTAS_LOG_TRACE(category) QUANTAS_LOG(::quantas::LogLevel::Trace, (category))

} // namespace quantas

#endif // QUANTAS_COMMON_LOGGER_HPP
