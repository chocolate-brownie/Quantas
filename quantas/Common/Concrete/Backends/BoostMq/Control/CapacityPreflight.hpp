#ifndef QUANTAS_COMMON_CONCRETE_BACKENDS_BOOSTMQ_CONTROL_CAPACITYPREFLIGHT_HPP
#define QUANTAS_COMMON_CONCRETE_BACKENDS_BOOSTMQ_CONTROL_CAPACITYPREFLIGHT_HPP

#include "quantas/Common/Concrete/Runtime/Config/RuntimeConfig.hpp"
#include "quantas/Common/Json.hpp"
#include <optional>

namespace quantas {

class CapacityPreflight {
  public:
    bool validate(const std::optional<nlohmann::json> &config) const;

  private:
    unsigned int readSystemCapacity() const;
    void validateExperimentCapacity(
        const RuntimeExperimentConfig &exp, size_t expIndex, unsigned int &capacity
    ) const;
    bool promptForTune() const;
    void tuneSystemCapacity(unsigned int requiredCapacity) const;
};

} // namespace quantas

#endif
