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

#ifndef CPAPeer_hpp
#define CPAPeer_hpp

#include "quantas/Common/Peer.hpp"
#include <iostream>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <vector>

using namespace std;

namespace quantas {

class Packet;

class CPAPeer : public Peer {
  public:
    CPAPeer(NetworkInterface *networkInterface);
    CPAPeer(const CPAPeer &rhs);
    ~CPAPeer() override;

    void
    initParameters(const std::vector<Peer *> &peers, json parameters) override;
    void performComputation() override;
    void endOfRound(std::vector<Peer *> &peers) override;

    NetworkInterface *releaseNetworkInterface();

    int msgsSent = 0;
    bool changePeerType = false;
    int ts = 0;               // threshold safty
    int tl = 0;               // threshold liveness
    int sender = 0;           // id of sender in initial round
    bool isByzantine = false; // whether this peer is byzantine or not

    bool delivered = false; // whether this peer has delivered the message or not
    int deliveryRound = -1; // round in which this peer delivered the message
	  int mDelivered = -1; // the message that this peer delivered

    unordered_map<interfaceId, int> receivedMessages;

  private:
    void checkInStrm();

    json buildMsg(int m) const;
    void propagateMsg(int m, vector<interfaceId> excludePeers);

    void correctBehavior();
    void byzantineBehavior();
};

} // namespace quantas

#endif /* CPAPeer_hpp */
