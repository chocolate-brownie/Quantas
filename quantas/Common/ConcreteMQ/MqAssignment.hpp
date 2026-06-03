#ifndef QUANTAS_COMMON_CONCRETEMQ_MQASSIGNMENT_HPP
#define QUANTAS_COMMON_CONCRETEMQ_MQASSIGNMENT_HPP

#include "../Packet.hpp"
#include <boost/serialization/set.hpp>
#include <set>
#include <string>

namespace quantas {

struct MqAssignment {
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
