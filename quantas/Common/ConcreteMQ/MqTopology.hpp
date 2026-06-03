#ifndef QUANTAS_COMMON_CONCRETEMQ_MQTOPOLOGY_HPP
#define QUANTAS_COMMON_CONCRETEMQ_MQTOPOLOGY_HPP

#include "MqAssignment.hpp"
#include <vector>

namespace quantas {

struct TopologyResult {
    std::vector<MqAssignment> assignments;
};

TopologyResult buildTopology(const nlohmann::json &topology);

} // namespace quantas

#endif
