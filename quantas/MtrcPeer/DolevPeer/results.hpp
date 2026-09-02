#include <iostream>
#include <vector>
#include <unordered_set>
#include <algorithm>

#include "quantas/Common/Peer.hpp"
#include "DolevPeer.hpp"

using namespace std;

namespace quantas {

    inline double getSafety(const vector<Peer *> &peers) {
        map<int, int> deliveryCount;
        int totalDelivered = 0;
        for (Peer *peerPtr : peers) {
            if (auto *dolevpeer = dynamic_cast<DolevPeer *>(peerPtr)) {
                if (dolevpeer->delivered && !dolevpeer->isByzantine) {
                    deliveryCount[dolevpeer->mDelivered]++;
                    totalDelivered++;
                }
            }
        }

        double safety = 0.0;
        for (const auto &entry : deliveryCount) {
            double perc = static_cast<double>(entry.second) / totalDelivered;
            safety = max(safety, perc);
        }

        safety *= 100.0; // convert to percentage
        safety = round(safety * 100.0) / 100.0; // round to 2 decimal places
        return safety;
    }

    inline double getTermination(const vector<Peer *> &peers) {
        int cntCorrectDelivered = 0;
        int cntCorrect = 0;
        for (Peer *peerPtr : peers) {
            if (auto *dolevpeer = dynamic_cast<DolevPeer *>(peerPtr)) {
                if (!dolevpeer->isByzantine) {
                    cntCorrect++;
                    if (dolevpeer->delivered) {
                        cntCorrectDelivered++;
                    }
                }
            }
        }
        double avgCorrectDelivered = 0.0;
        if (cntCorrect > 0) {
            avgCorrectDelivered = static_cast<double>(cntCorrectDelivered) / cntCorrect;
            avgCorrectDelivered *= 100.0; // convert to percentage
            avgCorrectDelivered = round(avgCorrectDelivered * 100.0) / 100.0; // round to 2 decimal places
        }
        return avgCorrectDelivered;
    }

    inline double getAvgDeliveryRound(const vector<Peer *> &peers) {
        int cntDelivered = 0;
        int cntDeliveredRound = 0;
        for (Peer *peerPtr : peers) {
            if (auto *dolevpeer = dynamic_cast<DolevPeer *>(peerPtr)) {
                if (dolevpeer->delivered) {
                    cntDelivered++;
                    cntDeliveredRound += dolevpeer->deliveryRound;
                }
            }
        }
        double avgDeliveredRound = 0.0;
        if (cntDelivered > 0) {
            avgDeliveredRound = static_cast<double>(cntDeliveredRound) / cntDelivered;
        }
        return avgDeliveredRound;
    }

    inline int getTotalMessagesSent(const vector<Peer *> &peers) {
        int total = 0;
        for (Peer *peerPtr : peers) {
            if (auto *dolevpeer = dynamic_cast<DolevPeer *>(peerPtr)) {
                total += dolevpeer->msgsSent;
            }
        }
        return total;
    }

    inline double getAvgMessagesSent(const vector<Peer *> &peers) {
        int total = getTotalMessagesSent(peers);
        int correctNodes = 0;
        for (Peer *peerPtr : peers) {
            if (auto *dolevpeer = dynamic_cast<DolevPeer *>(peerPtr)) {
                if (!dolevpeer->isByzantine) {
                    correctNodes++;
                }
            }
        }
        double avg = 0.0;
        if (correctNodes > 0) {
            avg = static_cast<double>(total) / correctNodes;
            avg = round(avg * 100.0) / 100.0; // round to 2 decimal places
        }
        return avg;
    }

    inline json saveResults(const vector<Peer *> &peers) {
        double avgDeliveryRound = getAvgDeliveryRound(peers);
        double avgTermination = getTermination(peers);
        double avgSafety = getSafety(peers);
        int totalMessagesSent = getTotalMessagesSent(peers);
        double avgMessagesSent = getAvgMessagesSent(peers);

        //cout << "Average Delivery Round: " << avgDeliveryRound << endl;
        //cout << "Average Termination: " << avgTermination << "%" << endl;
        //cout << "Average Safety: " << avgSafety << "%" << endl;
        //cout << "Total Messages Sent: " << totalMessagesSent << endl;
        //cout << "Average Messages Sent by Correct Nodes: " << avgMessagesSent << endl;
        return {{"avgDeliveryRound", avgDeliveryRound},
                {"avgTermination", avgTermination},
                {"avgSafety", avgSafety},
                {"totalMessagesSent", totalMessagesSent},
                {"avgMessagesSent", avgMessagesSent}
        };
    }
}
