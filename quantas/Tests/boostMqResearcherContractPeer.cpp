#include "quantas/Common/LogWriter.hpp"
#include "quantas/Common/Peer.hpp"
#include <string>
#include <vector>

namespace quantas {
namespace {

class BoostMqResearcherContractPeer final : public Peer {
  public:
    explicit BoostMqResearcherContractPeer(NetworkInterface* networkInterface)
        : Peer(networkInterface) {}

    void initParameters(const std::vector<Peer*>& peers, json parameters) override {
        ++_initParametersCalls;
        _parameterMarker = parameters.value("marker", "missing");
        observeLocalPeers(peers);
    }

    void performComputation() override { ++_performComputationCalls; }

    void endOfRound(std::vector<Peer*>& peers) override {
        ++_endOfRoundCalls;
        observeLocalPeers(peers);
    }

    void endOfExperiment(std::vector<Peer*>& peers) override {
        ++_endOfExperimentCalls;
        observeLocalPeers(peers);

        const auto assignedNeighbors = neighbors();
        LogWriter::setValue("localPeerId", publicId());
        LogWriter::setValue("localPeerCount", _localPeerCount);
        LogWriter::setValue("localHookVectors", _localHookVectors);
        LogWriter::setValue("assignedNeighbors", std::vector<interfaceId>(assignedNeighbors.begin(),
                                                                          assignedNeighbors.end()));
        LogWriter::setValue("parameterMarker", _parameterMarker);
        LogWriter::setValue("initParametersCalls", _initParametersCalls);
        LogWriter::setValue("performComputationCalls", _performComputationCalls);
        LogWriter::setValue("endOfRoundCalls", _endOfRoundCalls);
        LogWriter::setValue("endOfExperimentCalls", _endOfExperimentCalls);
    }

  private:
    void observeLocalPeers(const std::vector<Peer*>& peers) {
        _localPeerCount = peers.size();
        _localHookVectors = _localHookVectors && peers.size() == 1 && peers.front() == this;
    }

    size_t _localPeerCount{0};
    int _initParametersCalls{0};
    int _performComputationCalls{0};
    int _endOfRoundCalls{0};
    int _endOfExperimentCalls{0};
    bool _localHookVectors{true};
    std::string _parameterMarker;
};

const bool registered =
    PeerRegistry::registerPeerType("BoostMqResearcherContractPeer", [](interfaceId peerId) {
        return new BoostMqResearcherContractPeer(new NetworkInterfaceAbstract(peerId));
    });

} // namespace
} // namespace quantas
