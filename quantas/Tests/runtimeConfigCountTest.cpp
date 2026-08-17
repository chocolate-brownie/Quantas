#include "quantas/Common/Concrete/Runtime/Config/RuntimeConfig.hpp"
#include <cassert>
#include <functional>
#include <stdexcept>
#include <string>

namespace {

nlohmann::json validConfig() {
    return {
        {"experiments",
         {{{"topology",
            {{"type", "complete"}, {"initialPeers", 2}, {"initialPeerType", "ExamplePeer"}}},
           {"tests", 1},
           {"rounds", 2}}}}
    };
}

void expectInvalid(const std::function<void(nlohmann::json&)>& change) {
    nlohmann::json config = validConfig();
    change(config);

    bool threw = false;
    try {
        static_cast<void>(quantas::parseRuntimeExperiment(config, 0));
    } catch (const std::exception&) {
        threw = true;
    }
    assert(threw);
}

} // namespace

int main() {
    const quantas::RuntimeExperimentConfig valid = quantas::parseRuntimeExperiment(
        validConfig(),
        0
    );
    assert(valid.initialPeers == 2);
    assert(valid.tests == 1);
    assert(valid.rounds == 2);

    for (const char* field : {"initialPeers", "tests", "rounds"}) {
        expectInvalid([field](nlohmann::json& config) {
            if (std::string(field) == "initialPeers") {
                config["experiments"][0]["topology"].erase(field);
            } else {
                config["experiments"][0].erase(field);
            }
        });
        expectInvalid([field](nlohmann::json& config) {
            nlohmann::json& value = std::string(field) == "initialPeers"
                                        ? config["experiments"][0]["topology"][field]
                                        : config["experiments"][0][field];
            value = 0;
        });
        expectInvalid([field](nlohmann::json& config) {
            nlohmann::json& value = std::string(field) == "initialPeers"
                                        ? config["experiments"][0]["topology"][field]
                                        : config["experiments"][0][field];
            value = -1;
        });
        expectInvalid([field](nlohmann::json& config) {
            nlohmann::json& value = std::string(field) == "initialPeers"
                                        ? config["experiments"][0]["topology"][field]
                                        : config["experiments"][0][field];
            value = "invalid";
        });
    }

    return 0;
}
