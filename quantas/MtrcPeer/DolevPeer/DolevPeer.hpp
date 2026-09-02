/*
Copyright 2024

This file is part of QUANTAS.
QUANTAS is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. QUANTAS is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with
QUANTAS. If not, see <https://www.gnu.org/licenses/>.
*/

#ifndef DolevPeer_hpp
#define DolevPeer_hpp

#include "quantas/Common/Peer.hpp"
#include "disjoint_paths.hpp"
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <map>
#include <iostream>

using namespace std;

namespace quantas {

class Packet;

class DolevPeer : public Peer {
  public:
    DolevPeer(NetworkInterface *networkInterface);
    DolevPeer(const DolevPeer &rhs);
    ~DolevPeer() override;

    void initParameters(const std::vector<Peer *> &peers, json parameters) override;
    void performComputation() override;
    void endOfRound(std::vector<Peer *> &peers) override;

    NetworkInterface *releaseNetworkInterface();

    int msgsSent = 0;
    bool changePeerType = false;
    int ts = 0; // threshold safty
    int tl = 0; // threshold liveness
    int sender = 0; // id of sender in initial round
    bool isByzantine = false; // whether this peer is byzantine or not

    bool delivered = false; // whether this peer has delivered the message or not
    int deliveryRound = -1; // round in which this peer delivered the message
    int mDelivered = -1; // message that this peer delivered

    unordered_map<int, vector<unordered_set<interfaceId>>> receivedMessages = {}; // m -> list of path vectors from which this peer has received m


  private:
    void checkInStrm();

    json buildMsg(int m, const unordered_set<interfaceId> &path) const;
    void propagateMsg(int m, const unordered_set<interfaceId> &path, interfaceId srcId = -1);

    void correctBehavior();
    void byzantineBehavior();
};

} // namespace quantas

#endif /* DolevPeer_hpp */
