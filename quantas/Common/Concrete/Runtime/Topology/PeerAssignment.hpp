#ifndef QUANTAS_COMMON_RUNTIME_TOPOLOGY_PEERASSIGNMENT_HPP
#define QUANTAS_COMMON_RUNTIME_TOPOLOGY_PEERASSIGNMENT_HPP

#include "quantas/Common/Packet.hpp"
#include <boost/serialization/set.hpp>
#include <set>
#include <string>

namespace quantas {

struct PeerAssignment {
    interfaceId id{NO_PEER_ID};
    std::string topologyType;
    std::set<interfaceId> neighbors;

  private:
    friend class boost::serialization::access;

    template <class Archive> void serialize(Archive &ar, const unsigned int) {
        ar & id;
        ar & topologyType;
        ar & neighbors;
    }
};

} // namespace quantas

#endif
