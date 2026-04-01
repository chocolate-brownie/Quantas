#include "Logger.hpp"

#include <cctype>
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <streambuf>

namespace quantas {

namespace {

constexpr const char* levelName(LogLevel level) noexcept {
    switch (level) {
    case LogLevel::Error: return "ERROR";
    case LogLevel::Warn: return "WARN";
    case LogLevel::Info: return "INFO";
    case LogLevel::Debug: return "DEBUG";
    case LogLevel::Trace: return "TRACE";
    case LogLevel::Off: break;
    }
    return "OFF";
}

std::string formatTimestamp() {
    using namespace std::chrono;
    const auto now = system_clock::now();
    const auto truncated = time_point_cast<seconds>(now);
    const auto micros = duration_cast<microseconds>(now - truncated).count();

    std::time_t raw = system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    localtime_s(&tm, &raw);
#else
    localtime_r(&raw, &tm);
#endif

    std::ostringstream oss;
    oss << std::put_time(&tm, "%Y-%m-%dT%H:%M:%S")
        << '.' << std::setfill('0') << std::setw(6) << micros;
    return oss.str();
}

} // namespace

Logger& Logger::instance() {
    static Logger s_instance;
    return s_instance;
}

Logger::Logger()
    : _globalLevel(static_cast<int>(LogLevel::Info)),
      _stream(&std::cout) {
}

Logger::~Logger() {
    std::lock_guard<std::mutex> lock(_writeMutex);
    if (_fileStream.is_open()) {
        _fileStream.flush();
        _fileStream.close();
    }
}

void Logger::setDestination(const std::string& path, bool append) {
    Logger& inst = instance();
    std::lock_guard<std::mutex> lock(inst._writeMutex);

    if (inst._fileStream.is_open()) {
        inst._fileStream.flush();
        inst._fileStream.close();
    }

    if (path.empty()) {
        inst._stream = nullptr;
        return;
    }

    if (path == "cout") {
        inst._stream = &std::cout;
        return;
    }

    if (path == "cerr") {
        inst._stream = &std::cerr;
        return;
    }

    std::ios::openmode mode = std::ios::out | std::ios::binary;
    mode |= append ? std::ios::app : std::ios::trunc;

    std::filesystem::path destination(path);
    if (destination.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(destination.parent_path(), ec);
    }

    inst._fileStream.open(path, mode);
    if (inst._fileStream.is_open()) {
        inst._stream = &inst._fileStream;
    } else {
        std::cerr << "[logger] failed to open log file '" << path << "', falling back to std::cout\n";
        inst._stream = &std::cout;
    }
}

void Logger::setGlobalLevel(LogLevel level) {
    instance()._globalLevel.store(static_cast<int>(level), std::memory_order_relaxed);
}

void Logger::setCategoryLevel(const std::string& category, LogLevel level) {
    Logger& inst = instance();
    std::unique_lock<std::shared_mutex> lock(inst._configMutex);
    inst._categoryLevels[category] = level;
}

void Logger::clearCategoryLevel(const std::string& category) {
    Logger& inst = instance();
    std::unique_lock<std::shared_mutex> lock(inst._configMutex);
    inst._categoryLevels.erase(category);
}

void Logger::clearAllCategoryLevels() {
    Logger& inst = instance();
    std::unique_lock<std::shared_mutex> lock(inst._configMutex);
    inst._categoryLevels.clear();
}

void Logger::disable() {
    Logger& inst = instance();
    {
        std::unique_lock<std::shared_mutex> lock(inst._configMutex);
        inst._categoryLevels.clear();
        inst._globalLevel.store(static_cast<int>(LogLevel::Off), std::memory_order_relaxed);
    }
    std::lock_guard<std::mutex> lock(inst._writeMutex);
    if (inst._fileStream.is_open()) {
        inst._fileStream.flush();
        inst._fileStream.close();
    }
    inst._stream = nullptr;
}

bool Logger::shouldLog(std::string_view category, LogLevel level) noexcept {
    return instance().shouldLogImpl(category, level);
}

void Logger::log(LogLevel level, std::string_view category, std::string_view message) {
    Logger& inst = instance();
    if (!inst.shouldLogImpl(category, level)) {
        return;
    }
    inst.write(category, level, message);
}

LogLine Logger::line(LogLevel level, std::string_view category) {
    Logger& inst = instance();
    if (!inst.shouldLogImpl(category, level)) {
        return LogLine(inst, std::string{}, level, false);
    }
    return LogLine(inst, std::string(category), level, true);
}

std::string_view Logger::levelToString(LogLevel level) noexcept {
    return levelName(level);
}

std::optional<LogLevel> Logger::levelFromString(std::string_view name) noexcept {
    if (name.empty()) {
        return std::nullopt;
    }

    std::string normalized;
    normalized.reserve(name.size());
    for (char c : name) {
        normalized.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }

    if (normalized == "error" || normalized == "err" || normalized == "e") {
        return LogLevel::Error;
    }
    if (normalized == "warn" || normalized == "warning" || normalized == "w") {
        return LogLevel::Warn;
    }
    if (normalized == "info" || normalized == "information" || normalized == "i") {
        return LogLevel::Info;
    }
    if (normalized == "debug" || normalized == "d") {
        return LogLevel::Debug;
    }
    if (normalized == "trace" || normalized == "t") {
        return LogLevel::Trace;
    }
    if (normalized == "off" || normalized == "none") {
        return LogLevel::Off;
    }

    return std::nullopt;
}

bool Logger::shouldLogImpl(std::string_view category, LogLevel level) const noexcept {
    if (level == LogLevel::Off) {
        return false;
    }

    LogLevel threshold = static_cast<LogLevel>(_globalLevel.load(std::memory_order_relaxed));

    {
        std::shared_lock<std::shared_mutex> lock(_configMutex);
        if (!_categoryLevels.empty()) {
            const auto it = _categoryLevels.find(std::string(category));
            if (it != _categoryLevels.end()) {
                threshold = it->second;
            }
        }
    }

    if (threshold == LogLevel::Off) {
        return false;
    }

    return static_cast<int>(level) <= static_cast<int>(threshold);
}

void Logger::write(std::string_view category, LogLevel level, std::string_view message) {
    std::lock_guard<std::mutex> lock(_writeMutex);
    std::ostream* sink = _stream;
    if (!sink) {
        return;
    }
    (*sink) << formatTimestamp() << ' '
            << '[' << category << "][" << levelName(level) << "] "
            << message << '\n';
    sink->flush();
}

LogLine::LogLine(Logger& logger, std::string category, LogLevel level, bool active) noexcept
    : _logger(active ? &logger : nullptr),
      _category(std::move(category)),
      _level(level),
      _active(active) {
    if (_active) {
        _buffer = std::make_unique<std::ostringstream>();
    }
}

LogLine::LogLine(LogLine&& other) noexcept
    : _logger(other._logger),
      _category(std::move(other._category)),
      _level(other._level),
      _active(other._active),
      _buffer(std::move(other._buffer)) {
    other._logger = nullptr;
    other._active = false;
}

LogLine& LogLine::operator=(LogLine&& other) noexcept {
    if (this == &other) {
        return *this;
    }

    if (_active && _logger && _buffer) {
        _logger->write(_category, _level, _buffer->str());
    }

    _logger = other._logger;
    _category = std::move(other._category);
    _level = other._level;
    _active = other._active;
    _buffer = std::move(other._buffer);

    other._logger = nullptr;
    other._active = false;

    return *this;
}

LogLine::~LogLine() {
    if (_active && _logger && _buffer) {
        _logger->write(_category, _level, _buffer->str());
    }
}

std::ostream& LogLine::stream() {
    if (!_active) {
        return nullStream();
    }
    return *_buffer;
}

std::ostream& LogLine::nullStream() noexcept {
    struct NullBuffer : public std::streambuf {
        int overflow(int c) override { return c; }
    };
    static NullBuffer nullBuf;
    static std::ostream nullStream(&nullBuf);
    return nullStream;
}

} // namespace quantas
