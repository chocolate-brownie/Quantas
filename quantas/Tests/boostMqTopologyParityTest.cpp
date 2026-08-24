#include "quantas/Common/Abstract/Network.hpp"
#include "quantas/Common/Concrete/Runtime/Topology/TopologyPlanner.hpp"
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

class TopologyTestPeer final : public quantas::Peer {
  public:
    explicit TopologyTestPeer(quantas::interfaceId id)
        : Peer(new quantas::NetworkInterfaceAbstract(id)) {}

    void performComputation() override {}
};

using NeighborSets = std::vector<std::set<quantas::interfaceId>>;

NeighborSets abstractNeighbors(const nlohmann::json& topology) {
    static const bool registered = quantas::PeerRegistry::registerPeerType(
        "TopologyTestPeer", [](quantas::interfaceId id) { return new TopologyTestPeer(id); });
    (void)registered;

    quantas::Network network;
    network.setDistribution(nlohmann::json::object());
    network.initNetwork(topology);

    const int peerCount = topology.at("initialPeers").get<int>();
    NeighborSets result(static_cast<size_t>(peerCount));
    for (int index = 0; index < peerCount; ++index) {
        const auto* peer = network[index];
        result[static_cast<size_t>(peer->publicId())] = peer->neighbors();
    }
    return result;
}

NeighborSets boostMqNeighbors(const nlohmann::json& topology) {
    const auto result = quantas::buildTopology(topology);
    NeighborSets neighbors(result.assignments.size());
    for (const auto& assignment : result.assignments) {
        neighbors.at(static_cast<size_t>(assignment.id)) = assignment.neighbors;
    }
    return neighbors;
}

void verifyTopology(const std::string& name, const nlohmann::json& topology,
                    const NeighborSets& expected) {
    const auto abstract = abstractNeighbors(topology);
    const auto boostMq = boostMqNeighbors(topology);
    if (abstract != expected)
        throw std::runtime_error(name + ": Abstract neighbours differ from the expected topology");
    if (boostMq != expected)
        throw std::runtime_error(name + ": BoostMQ neighbours differ from the expected topology");
}

void expectTopologyError(const nlohmann::json& topology, const std::string& expectedMessage) {
    try {
        (void)quantas::buildTopology(topology);
    } catch (const std::runtime_error& error) {
        if (std::string(error.what()).find(expectedMessage) != std::string::npos)
            return;
        throw std::runtime_error("wrong topology error: " + std::string(error.what()));
    }
    throw std::runtime_error("invalid topology was accepted");
}

nlohmann::json topology(const std::string& type, int peerCount) {
    return {{"type", type}, {"initialPeers", peerCount}, {"initialPeerType", "TopologyTestPeer"}};
}

} // namespace

int main() {
    try {
        verifyTopology("complete", topology("complete", 4),
                       {{1, 2, 3}, {0, 2, 3}, {0, 1, 3}, {0, 1, 2}});
        verifyTopology("star", topology("star", 4), {{1, 2, 3}, {0}, {0}, {0}});

        auto grid = topology("grid", 6);
        grid["height"] = 2;
        grid["width"] = 3;
        verifyTopology("grid", grid, {{1, 3}, {0, 2, 4}, {1, 5}, {0, 4}, {1, 3, 5}, {2, 4}});

        auto torus = topology("torus", 9);
        torus["height"] = 3;
        torus["width"] = 3;
        verifyTopology("torus", torus,
                       {{1, 2, 3, 6},
                        {0, 2, 4, 7},
                        {0, 1, 5, 8},
                        {0, 4, 5, 6},
                        {1, 3, 5, 7},
                        {2, 3, 4, 8},
                        {0, 3, 7, 8},
                        {1, 4, 6, 8},
                        {2, 5, 6, 7}});

        verifyTopology("chain", topology("chain", 4), {{1}, {0, 2}, {1, 3}, {2}});
        verifyTopology("ring", topology("ring", 4), {{1, 3}, {0, 2}, {1, 3}, {0, 2}});
        verifyTopology("unidirectionalRing", topology("unidirectionalRing", 4),
                       {{1}, {2}, {3}, {0}});

        auto userList = topology("userList", 4);
        userList["list"] = {{"0", {1, 2}}, {"1", {2}}, {"2", {3}}, {"3", {0}}};
        verifyTopology("userList", userList, {{1, 2}, {2}, {3}, {0}});

        expectTopologyError(nlohmann::json::object(), "topology.initialPeers");
        expectTopologyError({{"initialPeers", 2}}, "topology.type");

        auto badGrid = topology("grid", 4);
        badGrid["width"] = 2;
        expectTopologyError(badGrid, "topology.height");
        badGrid["height"] = 3;
        expectTopologyError(badGrid, "must equal topology.initialPeers");

        auto badTorus = topology("torus", 2);
        badTorus["height"] = 1;
        badTorus["width"] = 2;
        expectTopologyError(badTorus, "at least 2");

        auto badList = topology("userList", 3);
        expectTopologyError(badList, "must be an object");
        badList["list"] = {{"bad", {1}}};
        expectTopologyError(badList, "invalid peer ID");
        badList["list"] = {{"0", 1}};
        expectTopologyError(badList, "must be an array");
        badList["list"] = {{"0", {"1"}}};
        expectTopologyError(badList, "non-integer neighbour ID");
        badList["list"] = {{"0", {3}}};
        expectTopologyError(badList, "out-of-range neighbour ID");
        badList["list"] = {{"0", {0}}};
        expectTopologyError(badList, "own peer ID");
    } catch (const std::exception& error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }

    std::cout << "PASS: fixed-ID Abstract and BoostMQ topology neighbours match.\n";
    return 0;
}
