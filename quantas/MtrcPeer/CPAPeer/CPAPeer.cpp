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

#include <string>
#include <iostream>
#include "CPAPeer.hpp" 
#include "results.hpp" 

namespace quantas {

int count(const std::unordered_map<interfaceId, int> &receivedMessages, int m) {
    int cnt = 0;
    for (const auto &entry : receivedMessages){
        if (entry.second == m) {
            cnt++;
        }
    }
    return cnt;
}

vector<interfaceId> getPeersWithMsg(const std::unordered_map<interfaceId, int> &receivedMessages, int m) {
    vector<interfaceId> peers;
    for (const auto &entry : receivedMessages){
        if (entry.second == m) {
            peers.push_back(entry.first);
        }
    }
    return peers;
}

static bool registerCPAPeer = []() {
    return PeerRegistry::registerPeerType("CPAPeer", [](interfaceId pubId) {
        return new CPAPeer(new NetworkInterfaceAbstract(pubId));
    });
}();

CPAPeer::CPAPeer(NetworkInterface *networkInterface): Peer(networkInterface) {}

CPAPeer::CPAPeer(const CPAPeer &rhs) : Peer(rhs) {
    msgsSent = rhs.msgsSent;
    changePeerType = rhs.changePeerType;
    ts = rhs.ts;
    tl = rhs.tl;
    sender = rhs.sender;
    receivedMessages = rhs.receivedMessages;
    isByzantine = rhs.isByzantine;
    delivered = rhs.delivered;
    deliveryRound = rhs.deliveryRound;
    mDelivered = rhs.mDelivered;
}

CPAPeer::~CPAPeer() = default;

void CPAPeer::initParameters(const std::vector<Peer *> &peers, json parameters) {
    ts = parameters.value("ts", 100);
    tl = parameters.value("tl", 100);
    sender = parameters.value("sender", 0);
    vector<int> byzantines = parameters.value("byzantines", vector<int>{});
    if (std::find(byzantines.begin(), byzantines.end(), publicId()) != byzantines.end()) {
        isByzantine = true;
    }
}

void CPAPeer::performComputation() {

    if (isByzantine) {
        byzantineBehavior();
    } else {
        correctBehavior();
    }
    
}

void CPAPeer::endOfRound(std::vector<Peer *> &peers) {
    
    if (RoundManager::currentRound() == RoundManager::lastRound() && publicId() == 0) {
        json results = saveResults(peers);
        LogWriter::pushValue("results", results);
        cout << "[CPA] finished saving results for test" << endl;
    }

}

NetworkInterface *CPAPeer::releaseNetworkInterface() {
    NetworkInterface *iface = _networkInterface;
    _networkInterface = nullptr;
    return iface;
}

void CPAPeer::checkInStrm() {
    while (!inStreamEmpty()) {
        Packet packet = popInStream();
        interfaceId srcId = packet.sourceId();
        json msg = packet.getMessage();

        int m = msg["m"];

        // check if we already delivered than do nothing
        if (delivered) continue; 

        // add message to receivedMessages
        receivedMessages[srcId] = m;

        if (srcId == sender) {
            delivered = true;
            deliveryRound = RoundManager::currentRound();
            mDelivered = m;
            propagateMsg(m, getPeersWithMsg(receivedMessages, m));
            // cout << "Peer " << publicId() << " delivered message " << m << " in round " << deliveryRound << endl;
            continue;
        }

        // check if we can deliver the message
        if (count(receivedMessages, m) >= ts+1) {
            delivered = true;
            deliveryRound = RoundManager::currentRound();
            mDelivered = m;
            propagateMsg(m, getPeersWithMsg(receivedMessages, m));
            // cout << "Peer " << publicId() << " delivered message " << m << " in round " << deliveryRound << endl;
        }

    }
}

json CPAPeer::buildMsg(int m) const {
    json payload;
    payload["m"] = m;
    return payload;
}

void CPAPeer::propagateMsg(int m, vector<interfaceId> excludePeers) {
    for (interfaceId peerId : neighbors()) {
        if (peerId == publicId()) continue;
        if (std::find(excludePeers.begin(), excludePeers.end(), peerId) != excludePeers.end()) continue;
        // cout << "Peer " << publicId() << " propagating message " << m << " to peer " << peerId << endl;
        json newMsg = buildMsg(m);
        unicastTo(newMsg, peerId);
        msgsSent++;
    }
}


void CPAPeer::correctBehavior() {
    
    // if sender and first round then propagate message 
    if (RoundManager::currentRound() == 1 && publicId() == sender) {
        propagateMsg(0, {});

        delivered = true;
        deliveryRound = RoundManager::currentRound();
        mDelivered = 0;
        //cout << "Sender " << publicId() << " delivered message 0 in round " << deliveryRound << endl;
    }

    // check messages in inStream and propagate if valid
    checkInStrm();
}

void CPAPeer::byzantineBehavior() {
    // Implement byzantine behavior logic here

    // propagate a fake message in the first round
    if (RoundManager::currentRound() == 1) {
        propagateMsg(1, {}); // fake message with m=1 instead of m=0
        //cout << "Byzantine peer " << publicId() << " sent message 1" << endl;
    }
    
}

} // namespace quantas
