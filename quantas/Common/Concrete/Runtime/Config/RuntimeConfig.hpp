#ifndef QUANTAS_COMMON_RUNTIME_CONFIG_RUNTIMECONFIG_HPP
#define QUANTAS_COMMON_RUNTIME_CONFIG_RUNTIMECONFIG_HPP

#include "quantas/Common/Json.hpp"
#include <cstddef>
#include <optional>
#include <string>

namespace quantas {

struct RuntimeExperimentConfig {
    int initialPeers{0};
    std::string initialPeerType;
    nlohmann::json topology;
    int rounds{0};
    int tests{1};
    int doneTimeoutMs{30000};
};

std::optional<nlohmann::json> loadRuntimeConfig(const std::string& jsonPath);

RuntimeExperimentConfig parseRuntimeExperiment(
    const nlohmann::json& config,
    size_t expIndex,
    const std::optional<int>& roundsOverride = std::nullopt
);

} // namespace quantas

#endif
