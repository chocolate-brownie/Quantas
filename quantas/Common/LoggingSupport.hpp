#ifndef QUANTAS_COMMON_LOGGING_SUPPORT_HPP
#define QUANTAS_COMMON_LOGGING_SUPPORT_HPP

#include <optional>
#include <string>
#include <string_view>

#include "Json.hpp"

namespace quantas {

struct LoggerActivation {
    bool enabled{false};
    bool append{false};
    std::string destination;
};

std::string chooseLogFileBase(const nlohmann::json& rootConfig,
                              const nlohmann::json& experimentConfig);

std::string makeExperimentFileName(const std::string& base,
                                   size_t experimentIndex,
                                   std::optional<int> port,
                                   std::string_view defaultExtension);

LoggerActivation configureLoggerForExperiment(const nlohmann::json& rootConfig,
                                              const nlohmann::json& experimentConfig,
                                              size_t experimentIndex,
                                              const std::string& logFileBase,
                                              std::optional<int> port,
                                              bool defaultAppend = false);

} // namespace quantas

#endif // QUANTAS_COMMON_LOGGING_SUPPORT_HPP
