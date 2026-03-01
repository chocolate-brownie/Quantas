#include "LoggingSupport.hpp"

#include <filesystem>
#include <sstream>

#include "Logger.hpp"

namespace quantas {

namespace {

struct LoggingOverrides {
    std::optional<std::string> level;
    std::optional<bool> append;
    std::optional<std::string> destination;
    nlohmann::json categories = nlohmann::json::object();
    bool hasCategories{false};
    bool specified{false};
};

void mergeLoggingOverrides(const nlohmann::json& node, LoggingOverrides& overrides) {
    if (!node.is_object()) {
        return;
    }
    for (const auto& [key, value] : node.items()) {
        if (key == "level" && value.is_string()) {
            overrides.level = value.get<std::string>();
            overrides.specified = true;
        } else if (key == "append" && value.is_boolean()) {
            overrides.append = value.get<bool>();
            overrides.specified = true;
        } else if (key == "destination" && value.is_string()) {
            overrides.destination = value.get<std::string>();
            overrides.specified = true;
        } else if (key == "categories" && value.is_object()) {
            overrides.specified = true;
            if (!overrides.categories.is_object()) {
                overrides.categories = nlohmann::json::object();
            }
            for (const auto& [category, levelNode] : value.items()) {
                overrides.categories[category] = levelNode;
                overrides.hasCategories = true;
            }
        }
    }
}

} // namespace

std::string chooseLogFileBase(const nlohmann::json& rootConfig,
                              const nlohmann::json& experimentConfig) {
    if (experimentConfig.contains("id") && experimentConfig["id"].is_string()) {
        return experimentConfig["id"].get<std::string>();
    }
    if (rootConfig.contains("id") && rootConfig["id"].is_string()) {
        return rootConfig["id"].get<std::string>();
    }
    if (experimentConfig.contains("id") && experimentConfig["id"].is_string()) {
        return experimentConfig["id"].get<std::string>();
    }
    return "cout";
}

std::string makeExperimentFileName(const std::string& base,
                                   size_t experimentIndex,
                                   std::optional<int> port,
                                   std::string_view defaultExtension) {
    if (base == "cout" || base == "cerr") {
        return base;
    }
    if (base.empty()) {
        return {};
    }

    std::filesystem::path path(base);
    std::string stem = path.stem().string();
    if (stem.empty()) {
        stem = "experiment";
    }

    std::string extension;
    if (path.has_extension()) {
        extension = path.extension().string();
    } else if (!defaultExtension.empty()) {
        extension.assign(defaultExtension);
    }
    if (!extension.empty() && extension.front() != '.') {
        extension.insert(extension.begin(), '.');
    }

    std::ostringstream nameBuilder;
    nameBuilder << stem << "_EXP" << (experimentIndex + 1);
    if (port.has_value()) {
        nameBuilder << "_p" << *port;
    }

    std::filesystem::path finalPath = path;
    finalPath.replace_filename(nameBuilder.str() + extension);
    return finalPath.string();
}

LoggerActivation configureLoggerForExperiment(const nlohmann::json& rootConfig,
                                              const nlohmann::json& experimentConfig,
                                              size_t experimentIndex,
                                              const std::string& logFileBase,
                                              std::optional<int> port,
                                              bool defaultAppend) {
    LoggerActivation activation;

    Logger::disable();

    LoggingOverrides overrides;
    if (auto it = rootConfig.find("logging"); it != rootConfig.end()) {
        mergeLoggingOverrides(*it, overrides);
    }
    if (rootConfig.contains("logging_level") && rootConfig["logging_level"].is_string()) {
        overrides.level = rootConfig["logging_level"].get<std::string>();
        overrides.specified = true;
    }
    if (auto it = experimentConfig.find("logging"); it != experimentConfig.end()) {
        mergeLoggingOverrides(*it, overrides);
    }
    if (experimentConfig.contains("logging_level") && experimentConfig["logging_level"].is_string()) {
        overrides.level = experimentConfig["logging_level"].get<std::string>();
        overrides.specified = true;
    }

    if (!overrides.specified) {
        return activation;
    }

    LogLevel effectiveLevel = LogLevel::Info;
    if (overrides.level.has_value()) {
        if (auto parsed = Logger::levelFromString(*overrides.level); parsed.has_value()) {
            effectiveLevel = *parsed;
        }
    }

    Logger::clearAllCategoryLevels();
    Logger::setGlobalLevel(effectiveLevel);

    bool loggingEnabled = (effectiveLevel != LogLevel::Off);

    if (overrides.hasCategories && overrides.categories.is_object()) {
        for (const auto& [categoryName, levelNode] : overrides.categories.items()) {
            if (!levelNode.is_string()) {
                continue;
            }
            if (auto parsed = Logger::levelFromString(levelNode.get<std::string>()); parsed.has_value()) {
                Logger::setCategoryLevel(categoryName, *parsed);
                if (*parsed != LogLevel::Off) {
                    loggingEnabled = true;
                }
            }
        }
    }

    if (!loggingEnabled) {
        return activation;
    }

    const bool append = overrides.append.value_or(defaultAppend);
    std::string destinationBase = overrides.destination.value_or(std::string{});
    const bool hasCustomDestination = overrides.destination.has_value();

    std::string destination;
    if (hasCustomDestination) {
        if (destinationBase.empty()) {
            return activation;
        }
        if (destinationBase == "cout" || destinationBase == "cerr") {
            destination = destinationBase;
        } else {
            destination = makeExperimentFileName(destinationBase, experimentIndex, port, ".log");
        }
    } else if (logFileBase == "cout" || logFileBase == "cerr") {
        destination = logFileBase;
    } else {
        destination = makeExperimentFileName(logFileBase, experimentIndex, port, ".log");
    }

    Logger::setDestination(destination, append);
    activation.enabled = true;
    activation.append = append;
    activation.destination = destination;
    return activation;
}

} // namespace quantas
