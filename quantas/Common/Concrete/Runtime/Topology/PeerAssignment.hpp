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
    /* Allow Boost.Serialization to access this private function. It writes or reads the peer ID,
     * topology type, and neighbour IDs so the assignment can cross process boundaries. */
    friend class boost::serialization::access;

    /* Tell Boost.Serialization which fields to write when sending and read when receiving. The
     * fields must be processed in the same order on both sides. */
    template <class Archive> void serialize(Archive &ar, const unsigned int) {
        ar & id;
        ar & topologyType;
        ar & neighbors;
    }
};

} // namespace quantas

#endif
