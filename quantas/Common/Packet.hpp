/*
Copyright 2022

This file is part of QUANTAS.
QUANTAS is free software: you can redistribute it and/or modify it under the terms of the GNU
General Public License as published by the Free Software Foundation, either version 3 of the
License, or (at your option) any later version. QUANTAS is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details. You should have
received a copy of the GNU General Public License along with QUANTAS. If not, see
<https://www.gnu.org/licenses/>.
*/
//
// This is the class responsible for holding messages as they are sent from one peer to another
// (similar to a packet in a real network) It is templated so that the user can define their own
// message (body, payload, etc.) they want to include in the packet. This allows them to define what
// information is allowed in a single message between peers.
//
// A packet needs a source peer id (id of the network interface), target peer id (id of the network
// interface).

#ifndef Packet_hpp
#define Packet_hpp

#include "Json.hpp"
#include "RandomUtil.hpp"
#include "RoundManager.hpp"
#include <boost/serialization/access.hpp>
#include <boost/serialization/split_member.hpp>
#include <boost/serialization/string.hpp>
#include <chrono>
#include <cstdint>
#include <memory>

namespace quantas {

using nlohmann::json;
using std::string;
using std::unique_ptr;
using namespace boost::archive;

typedef long interfaceId;

inline static const interfaceId NO_PEER_ID = -1; // used to indicate invalid peer or un init peers

// Packet Class
class Packet {
  private:
    interfaceId _targetId{NO_PEER_ID}; // Target node ID
    interfaceId _sourceId{NO_PEER_ID}; // Source node ID
    json _body;                        // Message payload
    int _delay{0};                     // Transmission delay
    int _round{-1};                    // Round message was sent

    /* ------------- Boost Serialization --------------- */
    friend class boost::serialization::access;
    std::int64_t _sendTimestamp{0};

    template <class Archive> void save(Archive &ar, const unsigned int) const {
        ar & _sourceId;
        ar & _targetId;
        ar & _sendTimestamp;

        std::string jsnBody = _body.dump();
        ar & jsnBody;
    }

    template <class Archive> void load(Archive &ar, const unsigned int) {
        ar & _sourceId;
        ar & _targetId;
        ar & _sendTimestamp;

        std::string jsnBody;
        ar & jsnBody;
        _body = nlohmann::json::parse(jsnBody);
    }

    BOOST_SERIALIZATION_SPLIT_MEMBER()
    /* ------------------------------------------------ */

  public:
    inline Packet();
    inline Packet(interfaceId to, interfaceId from, json body);
    inline Packet(const Packet &rhs);
    inline Packet &operator=(const Packet &rhs);
    ~Packet() = default;

    // Setters
    inline void setSource(interfaceId s) { _sourceId = s; }
    inline void setTarget(interfaceId t) { _targetId = t; }
    inline void setDelay(int delayMax, int delayMin = 1);
    inline void setMessage(json msg) { _body = msg; }
    inline void setSendTime() {
        _sendTimestamp = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::system_clock::now().time_since_epoch()
        )
                             .count();
    }

    // Getters
    inline interfaceId targetId() const { return _targetId; }
    inline interfaceId sourceId() const { return _sourceId; }
    inline bool hasArrived() const { return RoundManager::currentRound() >= _round + _delay; }
    inline json getMessage() const { return _body; }
    inline int getDelay() const { return _delay; }
    inline int getRoundSent() const { return _round; }
    inline std::int64_t getSendTime() const { return _sendTimestamp; }
};

// Constructor Implementations
inline Packet::Packet() { _round = RoundManager::currentRound(); }

inline Packet::Packet(interfaceId to, interfaceId from, json body)
    : _targetId(to), _sourceId(from), _body(body), _delay(0) {
    _round = RoundManager::currentRound();
}

inline Packet::Packet(const Packet &rhs)
    : _targetId(rhs._targetId), _sourceId(rhs._sourceId), _body(rhs._body), _delay(rhs._delay),
      _round(rhs._round), _sendTimestamp(rhs._sendTimestamp) {}

inline Packet &Packet::operator=(const Packet &rhs) {
    if (this == &rhs)
        return *this;
    _targetId = rhs._targetId;
    _sourceId = rhs._sourceId;
    _delay = rhs._delay;
    _round = rhs._round;
    _body = rhs._body;
    _sendTimestamp = rhs._sendTimestamp;
    return *this;
}

inline void Packet::setDelay(int maxDelay, int minDelay) {
    if (maxDelay < 1)
        maxDelay = 1;
    if (minDelay < 1)
        minDelay = 1;
    if (minDelay > maxDelay)
        minDelay = maxDelay;
    _delay = uniformInt(minDelay, maxDelay);
}

} // namespace quantas

#endif /* PACKET_HPP */
