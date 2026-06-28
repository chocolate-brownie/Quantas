#ifndef QUANTAS_COMMON_RUNTIME_TOPOLOGY_TOPOLOGYPLANNER_HPP
#define QUANTAS_COMMON_RUNTIME_TOPOLOGY_TOPOLOGYPLANNER_HPP

#include "quantas/Common/Concrete/Runtime/Topology/PeerAssignment.hpp"
#include <vector>

namespace quantas {

struct TopologyResult {
    std::vector<PeerAssignment> assignments;
};

TopologyResult buildTopology(const nlohmann::json &topology);

} // namespace quantas

#endif
