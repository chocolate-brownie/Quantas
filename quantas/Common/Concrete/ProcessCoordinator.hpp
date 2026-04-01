#ifndef QUANTAS_CONCRETE_PROCESS_COORDINATOR_HPP
#define QUANTAS_CONCRETE_PROCESS_COORDINATOR_HPP

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <deque>
#include <map>
#include <mutex>
#include <optional>
#include <set>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "../Json.hpp"
#include "../Packet.hpp"

namespace quantas {

class NetworkInterfaceConcrete;

class ProcessCoordinator {
public:
    struct PeerAssignment {
        interfaceId id{NO_PEER_ID};
        std::set<interfaceId> neighbors;
    };

    struct PeerEndpoint {
        interfaceId id{NO_PEER_ID};
        std::string ip;
        int port{-1};
    };

    enum class StopType {
        PeerSignals,
        Time
    };

    struct StopCondition {
        StopType type{StopType::PeerSignals};
        std::chrono::milliseconds duration{0};
    };

    static ProcessCoordinator& instance();

    void configureProcess(const nlohmann::json& rootConfig,
                          const nlohmann::json& experimentConfig,
                          const std::string& peerType,
                          int peersPerProcessOverride,
                          bool isLeader,
                          std::optional<int> explicitPort,
                          size_t experimentIndex,
                          const std::string& logFileBase);

    std::vector<PeerAssignment> waitForAssignments();
    void registerLocalAssignments(const std::vector<PeerAssignment>& assignments);

    void markReady();
    void waitForStartSignal();

    bool shouldStop() const;
    void waitForStop();

    void notifyPeerStopped(interfaceId id);

    void registerInterface(interfaceId id, NetworkInterfaceConcrete* iface);
    void unregisterInterface(interfaceId id);

    void enqueueInbound(interfaceId to, interfaceId from, const nlohmann::json& body);
    void unicast(interfaceId from, interfaceId to, const nlohmann::json& body);
    void drainInbound(interfaceId id, std::deque<Packet>& target);

    bool ownsPeer(interfaceId id) const;
    StopCondition stopCondition() const;

    const std::unordered_map<interfaceId, PeerEndpoint>& endpoints() const;
    const std::vector<PeerAssignment>& localAssignments() const;

    void cleanupExperiment();
    void shutdown();

private:
    ProcessCoordinator();
    ~ProcessCoordinator();

    ProcessCoordinator(const ProcessCoordinator&) = delete;
    ProcessCoordinator& operator=(const ProcessCoordinator&) = delete;

    struct ProcessRecord {
        std::string ip;
        int port{-1};
        int requestedPeers{0};
        std::vector<interfaceId> assignedPeers;
        bool ready{false};
    };

    void ensureListenerStarted();
    void listenerLoop();
    void processMessage(const std::string& remoteIp, int remotePort, const nlohmann::json& msg);

    // leader message handlers
    void handleRegister(const std::string& remoteIp, int remotePort, const nlohmann::json& msg);
    void handleReady(const nlohmann::json& msg);
    void handlePeerDone(const std::string& remoteIp, int remotePort, interfaceId peerId);
    void distributeAssignmentsIfReady();
    void broadcastStartIfReady();
    void broadcastStop(const std::string& reason);

    // follower handlers
    void handleAssignment(const nlohmann::json& msg);
    void handleStart();
    void handleStop();
    void handleInboundMessage(const nlohmann::json& msg);

    void sendJson(const std::string& ip, int port, const nlohmann::json& msg);

    std::string processKey(const std::string& ip, int port) const;

    std::vector<PeerAssignment> computeAssignments(const std::unordered_map<std::string, ProcessRecord>& registrations) const;
    std::unordered_map<interfaceId, PeerEndpoint> computeEndpoints(const std::unordered_map<std::string, ProcessRecord>& registrations) const;
    std::vector<PeerAssignment> buildTopologyAssignments(int totalPeers) const;

    void startStopTimer();

    // configuration
    nlohmann::json _rootConfig;
    nlohmann::json _experimentConfig;
    std::string _peerType;
    std::string _leaderIp;
    int _leaderPort{-1};
    interfaceId _leaderId{0};
    int _totalPeers{0};
    int _peersPerProcess{1};
    bool _isLeader{false};

    std::string _myIp;
    int _myPort{-1};

    StopCondition _stopCondition;

    // listener/socket
    std::mutex _listenerMutex;
    bool _listenerStarted{false};
    int _serverFd{-1};
    std::thread _listenerThread;

    // state
    std::atomic<bool> _assignmentsReady{false};
    std::atomic<bool> _startSignal{false};
    std::atomic<bool> _stopSignal{false};
    std::atomic<bool> _shutdownRequested{false};

    std::mutex _assignmentMutex;
    std::condition_variable _assignmentCv;
    std::vector<PeerAssignment> _localAssignments;

    std::mutex _startMutex;
    std::condition_variable _startCv;

    std::mutex _stopMutex;
    std::condition_variable _stopCv;

    std::mutex _processMutex;
    std::unordered_map<std::string, ProcessRecord> _processes;
    std::vector<std::string> _registrationOrder;

    std::mutex _endpointMutex;
    std::unordered_map<interfaceId, PeerEndpoint> _allPeers;

    mutable std::mutex _interfaceMutex;
    std::unordered_map<interfaceId, NetworkInterfaceConcrete*> _localInterfaces;

    std::mutex _inboundMutex;
    std::unordered_map<interfaceId, std::deque<Packet>> _inboundQueues;

    std::mutex _completedMutex;
    std::unordered_set<interfaceId> _completedPeers;

    std::thread _stopTimerThread;
    std::atomic<bool> _stopTimerActive{false};
};

} // namespace quantas

#endif // QUANTAS_CONCRETE_PROCESS_COORDINATOR_HPP
