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
#include "DolevPeer.hpp"
#include "results.hpp"

namespace quantas {

std::string pathToString(const std::unordered_set<interfaceId>& path) {
    std::string result = "{";
    bool first = true;

    for (const auto& id : path) {
        if (!first) {
            result += ", ";
        }
        result += std::to_string(id);
        first = false;
    }

    result += "}";
    return result;
}

static bool registerDolevPeer = []() {
    return PeerRegistry::registerPeerType("DolevPeer", [](interfaceId pubId) {
        return new DolevPeer(new NetworkInterfaceAbstract(pubId));
    });
}();

DolevPeer::DolevPeer(NetworkInterface *networkInterface): Peer(networkInterface) {}

DolevPeer::DolevPeer(const DolevPeer &rhs) : Peer(rhs) {
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

DolevPeer::~DolevPeer() = default;

void DolevPeer::initParameters(const std::vector<Peer *> &peers, json parameters) {
    ts = parameters.value("ts", 100);
    tl = parameters.value("tl", 100);
    sender = parameters.value("sender", 0);
    vector<int> byzantines = parameters.value("byzantines", vector<int>{});
    if (std::find(byzantines.begin(), byzantines.end(), publicId()) != byzantines.end()) {
        isByzantine = true;
    }
}

void DolevPeer::performComputation() {

    if (isByzantine) {
        byzantineBehavior();
    } else {
        correctBehavior();
    }
    
}

void DolevPeer::endOfRound(std::vector<Peer *> &peers) {
    
    if (RoundManager::currentRound() == RoundManager::lastRound() && publicId() == 0) {
        json results = saveResults(peers);
        LogWriter::pushValue("results", results);
        cout << "[Dolev] finished saving results for test" << endl;
    }

}

NetworkInterface *DolevPeer::releaseNetworkInterface() {
    NetworkInterface *iface = _networkInterface;
    _networkInterface = nullptr;
    return iface;
}

void DolevPeer::checkInStrm() {
    while (!inStreamEmpty()) {
        Packet packet = popInStream();
        interfaceId srcId = packet.sourceId();
        json msg = packet.getMessage();

        int m = msg["m"];
        unordered_set<interfaceId> path = msg["path"].get<unordered_set<interfaceId>>();

        // already sent this message, don't process again to avoid cycles
        if (path.find(publicId()) != path.end()) continue; 

        // if this message is directly from the sender, deliver immediately
        if (packet.sourceId() == sender && path.size() == 0) {
            delivered = true;
            deliveryRound = RoundManager::currentRound();
            mDelivered = m;
            //cout << "[DIRECT] Peer " << publicId() << " delivered message " << m << " in round " << deliveryRound << endl;
        }

        // insert the source of the message into the path and store it as a received message, then propagate to neighbors
        //cout << "Round: " << RoundManager::currentRound() << ": - Peer " << publicId() << "  <--- " << msg.dump() << " from " << packet.sourceId() << endl;
        path.insert(srcId);
        receivedMessages[m].push_back(path);
        propagateMsg(m, path, srcId);

        // check if we can deliver the message now by checking if we have received it from ts+1 disjoint paths (removing the source)
        vector<unordered_set<long>> modified_sets = remove_src(receivedMessages[m], 0);
        if (!delivered && k_disjoint(modified_sets, ts+1)) {
            delivered = true;
            deliveryRound = RoundManager::currentRound();
            mDelivered = m;
            //cout << "Peer " << publicId() << " original sets" << endl;
            //print_list_sets(modified_sets);
            //cout << "[DISJOINT] Peer " << publicId() << " delivered message " << m << " in round " << deliveryRound << endl;
        }

    }
}

json DolevPeer::buildMsg(int m, const unordered_set<interfaceId> &path) const {
    json payload;
    payload["m"] = m;
    payload["path"] = path;
    return payload;
}

void DolevPeer::propagateMsg(int m, const unordered_set<interfaceId> &path, interfaceId srcId) {
    for (interfaceId peerId : path) {
        if (peerId == publicId()) {
            // already sent this message, don't propagate again to avoid cycles
            return; 
        }
    }

    json newMsg = buildMsg(m, path);
    broadcast(newMsg);
    //cout << "Round: " << RoundManager::currentRound() << ": - Peer " << publicId() << " ---> " << m << " with path " << pathToString(path) << endl;
    msgsSent += static_cast<int>(neighbors().size());
}


void DolevPeer::correctBehavior() {
    
    // if sender and first round then propagate message 
    if (RoundManager::currentRound() == 1 && publicId() == sender) {
        json payload = buildMsg(0, {});
        broadcast(payload);
        //cout << "Sender " << publicId() << " sent first message" << endl;
        msgsSent += static_cast<int>(neighbors().size());

        delivered = true;
        deliveryRound = RoundManager::currentRound();
        mDelivered = 0;
        //cout << "Sender " << publicId() << " delivered message 0 in round " << deliveryRound << endl;
    }

    // check messages in inStream and propagate if valid
    checkInStrm();
}

void DolevPeer::byzantineBehavior() {
    // Implement byzantine behavior logic here

    // propagate a fake message in the first round
    if (RoundManager::currentRound() == 1) {
        json payload = buildMsg(1, {});
        broadcast(payload);
        //cout << "Byzantine peer " << publicId() << " sent message 1" << endl;
        msgsSent += static_cast<int>(neighbors().size());
    }
    
}

} // namespace quantas
