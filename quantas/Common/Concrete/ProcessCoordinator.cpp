#include "ProcessCoordinator.hpp"

#include <arpa/inet.h>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <chrono>
#include <numeric>
#include <random>
#include <sstream>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstdint>
#include <stdexcept>

#include "NetworkInterfaceConcrete.hpp"
#include "ipUtil.hpp"
#include "../LoggingSupport.hpp"
#include "../Logger.hpp"
#include "../RoundManager.hpp"

namespace quantas {

namespace {

constexpr const char* kTypeRegister = "register";
constexpr const char* kTypeAssignment = "assignment";
constexpr const char* kTypeReady = "ready";
constexpr const char* kTypeStart = "start";
constexpr const char* kTypeStop = "stop";
constexpr const char* kTypePeerDone = "peer_done";
constexpr const char* kTypeMessage = "message";

std::string localHostname() {
    char buffer[256]{};
    if (gethostname(buffer, sizeof(buffer) - 1) != 0) {
        return "unknown-host";
    }
    return std::string(buffer);
}

std::string getenvOrFallback(const char* name, const std::string& fallback) {
    if (name == nullptr) {
        return fallback;
    }
    const char* value = std::getenv(name);
    if (value == nullptr || *value == '\0') {
        return fallback;
    }
    return std::string(value);
}

std::string readSocketMessage(int socketFd) {
    std::string data;
    char buffer[4096];
    for (;;) {
        ssize_t bytes = recv(socketFd, buffer, sizeof(buffer), 0);
        if (bytes <= 0) {
            break;
        }
        data.append(buffer, static_cast<size_t>(bytes));
        if (bytes < static_cast<ssize_t>(sizeof(buffer))) {
            break;
        }
    }
    return data;
}

bool recvAll(int socketFd, void* buffer, size_t length) {
    auto* bytes = static_cast<char*>(buffer);
    size_t totalRead = 0;
    while (totalRead < length) {
        const ssize_t received = recv(socketFd, bytes + totalRead, length - totalRead, 0);
        if (received <= 0) {
            return false;
        }
        totalRead += static_cast<size_t>(received);
    }
    return true;
}

bool sendAll(int socketFd, const char* buffer, size_t length) {
    size_t totalSent = 0;
    while (totalSent < length) {
        const ssize_t sent = send(socketFd, buffer + totalSent, length - totalSent, 0);
        if (sent <= 0) {
            return false;
        }
        totalSent += static_cast<size_t>(sent);
    }
    return true;
}

std::optional<std::string> readFramedMessage(int socketFd) {
    uint32_t networkLength = 0;
    if (!recvAll(socketFd, &networkLength, sizeof(networkLength))) {
        return std::nullopt;
    }

    const uint32_t payloadLength = ntohl(networkLength);
    std::string payload(payloadLength, '\0');
    if (payloadLength > 0 && !recvAll(socketFd, payload.data(), payload.size())) {
        return std::nullopt;
    }
    return payload;
}

struct TopologyResult {
    std::vector<ProcessCoordinator::PeerAssignment> assignments;
};

TopologyResult buildTopology(const nlohmann::json& topology) {
    TopologyResult result;
    const int initialPeers = topology.value("initialPeers", 0);
    result.assignments.resize(static_cast<size_t>(initialPeers));
    if (initialPeers <= 0) {
        return result;
    }

    std::vector<interfaceId> ids(static_cast<size_t>(initialPeers));
    std::iota(ids.begin(), ids.end(), 0);

    if (topology.value("identifiers", "") == "random") {
        static thread_local std::mt19937 rng(std::random_device{}());
        std::shuffle(ids.begin(), ids.end(), rng);
    }

    auto addUndirectedEdge = [&](interfaceId a, interfaceId b) {
        if (a == b || a < 0 || b < 0 || a >= initialPeers || b >= initialPeers) return;
        result.assignments[static_cast<size_t>(a)].id = a;
        result.assignments[static_cast<size_t>(b)].id = b;
        result.assignments[static_cast<size_t>(a)].neighbors.insert(b);
        result.assignments[static_cast<size_t>(b)].neighbors.insert(a);
    };

    auto addDirectedEdge = [&](interfaceId from, interfaceId to) {
        if (from < 0 || to < 0 || from >= initialPeers || to >= initialPeers) return;
        result.assignments[static_cast<size_t>(from)].id = from;
        result.assignments[static_cast<size_t>(from)].neighbors.insert(to);
    };

    const std::string type = topology.value("type", "");
    if (type == "complete") {
        for (int i = 0; i < initialPeers; ++i) {
            for (int j = i + 1; j < initialPeers; ++j) {
                interfaceId a = ids[static_cast<size_t>(i)];
                interfaceId b = ids[static_cast<size_t>(j)];
                addUndirectedEdge(a, b);
            }
        }
    } else if (type == "star") {
        for (int i = 1; i < initialPeers; ++i) {
            interfaceId center = ids[0];
            interfaceId leaf = ids[static_cast<size_t>(i)];
            addUndirectedEdge(center, leaf);
        }
    } else if (type == "grid") {
        int height = topology.value("height", 1);
        int width = topology.value("width", 1);
        if (height * width != initialPeers) {
            width = initialPeers;
            height = 1;
        }
        for (int i = 0; i < height; ++i) {
            for (int j = 0; j < width; ++j) {
                int idx = i * width + j;
                interfaceId current = ids[static_cast<size_t>(idx)];
                if (j + 1 < width) {
                    interfaceId right = ids[static_cast<size_t>(idx + 1)];
                    addUndirectedEdge(current, right);
                }
                if (i + 1 < height) {
                    interfaceId down = ids[static_cast<size_t>(idx + width)];
                    addUndirectedEdge(current, down);
                }
            }
        }
    } else if (type == "torus") {
        int height = topology.value("height", 1);
        int width = topology.value("width", 1);
        if (height * width != initialPeers) {
            width = initialPeers;
            height = 1;
        }
        for (int i = 0; i < height; ++i) {
            for (int j = 0; j < width; ++j) {
                int idx = i * width + j;
                interfaceId current = ids[static_cast<size_t>(idx)];
                interfaceId right = ids[static_cast<size_t>(i * width + ((j + 1) % width))];
                interfaceId down = ids[static_cast<size_t>(((i + 1) % height) * width + j)];
                addUndirectedEdge(current, right);
                addUndirectedEdge(current, down);
            }
        }
    } else if (type == "chain") {
        for (int i = 0; i < initialPeers - 1; ++i) {
            interfaceId a = ids[static_cast<size_t>(i)];
            interfaceId b = ids[static_cast<size_t>(i + 1)];
            addUndirectedEdge(a, b);
        }
    } else if (type == "ring") {
        for (int i = 0; i < initialPeers; ++i) {
            interfaceId a = ids[static_cast<size_t>(i)];
            interfaceId b = ids[static_cast<size_t>((i + 1) % initialPeers)];
            addUndirectedEdge(a, b);
        }
    } else if (type == "unidirectionalRing") {
        for (int i = 0; i < initialPeers; ++i) {
            interfaceId a = ids[static_cast<size_t>(i)];
            interfaceId b = ids[static_cast<size_t>((i + 1) % initialPeers)];
            addDirectedEdge(a, b);
        }
    } else if (type == "userList") {
        const auto it = topology.find("list");
        if (it != topology.end() && it->is_object()) {
            for (int i = 0; i < initialPeers; ++i) {
                interfaceId id = ids[static_cast<size_t>(i)];
                result.assignments[static_cast<size_t>(id)].id = id;
            }
            for (const auto& [key, value] : it->items()) {
                int idx = std::stoi(key);
                if (idx < 0 || idx >= initialPeers) continue;
                interfaceId src = ids[static_cast<size_t>(idx)];
                if (!value.is_array()) continue;
                for (const auto& destValue : value) {
                    int neighborIndex = destValue.get<int>();
                    if (neighborIndex < 0 || neighborIndex >= initialPeers) continue;
                    interfaceId dest = ids[static_cast<size_t>(neighborIndex)];
                    addDirectedEdge(src, dest);
                }
            }
        }
    } else {
        // default: fully disconnected but ensure ids set
        for (interfaceId id : ids) {
            result.assignments[static_cast<size_t>(id)].id = id;
        }
    }

    // ensure ids assigned even if no edges
    for (interfaceId id = 0; id < initialPeers; ++id) {
        result.assignments[static_cast<size_t>(id)].id = id;
    }
    return result;
}

} // namespace

ProcessCoordinator& ProcessCoordinator::instance() {
    static ProcessCoordinator coordinator;
    return coordinator;
}

ProcessCoordinator::ProcessCoordinator() = default;

ProcessCoordinator::~ProcessCoordinator() {
    _shutdownRequested = true;
    closeAllConnections();
    if (_serverFd >= 0) {
        close(_serverFd);
    }
    if (_listenerThread.joinable()) {
        _listenerThread.join();
    }
    if (_stopTimerThread.joinable()) {
        _stopTimerActive = false;
        _stopTimerThread.join();
    }
}

void ProcessCoordinator::configureProcess(const nlohmann::json& rootConfig,
                                          const nlohmann::json& experimentConfig,
                                          const std::string& peerType,
                                          int peersPerProcessOverride,
                                          bool isLeader,
                                          std::optional<int> explicitPort,
                                          size_t experimentIndex,
                                          const std::string& logFileBase) {
    std::scoped_lock configLock(_configurationMutex);
    _rootConfig = rootConfig;
    _experimentConfig = experimentConfig;
    _peerType = peerType;
    _experimentIndex = experimentIndex;
    _totalPeers = experimentConfig["topology"].value("initialPeers", 0);

    _assignmentsReady = false;
    _startSignal = false;
    _stopSignal = false;
    _shutdownRequested = false;
    {
        std::scoped_lock lock(_completedMutex);
        _completedPeers.clear();
    }
    {
        std::scoped_lock lock(_assignmentMutex);
        _localAssignments.clear();
    }
    {
        std::scoped_lock lock(_endpointMutex);
        _allPeers.clear();
        _peerRanges.clear();
    }
    {
        std::scoped_lock lock(_interfaceMutex);
        _localInterfaces.clear();
    }
    {
        std::scoped_lock lock(_inboundMutex);
        _inboundQueues.clear();
    }
    {
        std::scoped_lock lock(_processMutex);
        _processes.clear();
        _registrationOrder.clear();
    }

    const auto concreteIt = experimentConfig.find("concrete");
    if (concreteIt == experimentConfig.end() && !rootConfig.contains("concrete")) {
        throw std::runtime_error("Field 'concrete' missing in input file. Add to this experiment or to the root of the config if applied to all experiments.");
    }

    if (peersPerProcessOverride > 0) {
        _peersPerProcess = peersPerProcessOverride;
    } else if (concreteIt != experimentConfig.end() && concreteIt->contains("peersPerProcess")) {
        _peersPerProcess = concreteIt->value("peersPerProcess", 1);
    } else if (rootConfig.contains("concrete") && rootConfig["concrete"].contains("peersPerProcess")) {
        _peersPerProcess = rootConfig["concrete"].value("peersPerProcess", 1);
    } else {
        _peersPerProcess = 1;
    }

    StopCondition condition;

    if (concreteIt != experimentConfig.end() && concreteIt->contains("stopCondition")) {
        const auto& stopJson = (*concreteIt)["stopCondition"];
        std::string typeStr = stopJson.value("type", "peerSignals");
        if (typeStr == "time") {
            condition.type = StopType::Time;
            int durationMs = stopJson.value("milliseconds", 0);
            if (durationMs < 0) durationMs = 0;
            condition.duration = std::chrono::milliseconds(durationMs);
        } else {
            condition.type = StopType::PeerSignals;
        }
    } else if (rootConfig.contains("concrete") && rootConfig["concrete"].contains("stopCondition")) {
        const auto& stopJson = rootConfig["concrete"]["stopCondition"];
        std::string typeStr = stopJson.value("type", "peerSignals");
        if (typeStr == "time") {
            condition.type = StopType::Time;
            int durationMs = stopJson.value("milliseconds", 0);
            if (durationMs < 0) durationMs = 0;
            condition.duration = std::chrono::milliseconds(durationMs);
        } else {
            condition.type = StopType::PeerSignals;
        }
    } else {
        condition.type = StopType::PeerSignals;
    }
    _stopCondition = condition;

    nlohmann::json leaderJson;
    if (concreteIt != experimentConfig.end() && concreteIt->contains("leader")) {
        leaderJson = (*concreteIt)["leader"];
    } else if (rootConfig.contains("concrete") && rootConfig["concrete"].contains("leader")) {
        leaderJson = rootConfig["concrete"]["leader"];
    } else if (rootConfig.contains("leader")) {
        leaderJson = rootConfig["leader"];
    }
    _leaderIp = leaderJson.value("ip", "");
    _leaderPort = leaderJson.value("port", -1);
    _leaderId = leaderJson.value("id", 0);

    _myIp = get_local_ip();
    if (_myIp.empty()) {
        throw std::runtime_error("Unable to determine IP address.");
    }

    if (explicitPort.has_value()) {
        _myPort = explicitPort.value();
    } else if (_myPort > 0) {
        // Reuse the existing listener port across experiments so remote peers
        // keep a stable endpoint for this process.
    } else {
        _myPort = get_unused_port();
    }

    if (_myPort <= 0) {
        throw std::runtime_error("Unable to determine listening port for process.");
    }

    _isLeader = isLeader;
    if (!_leaderIp.empty() && _myIp == _leaderIp && _myPort == _leaderPort) {
        _isLeader = true;
    }

    const std::string hostname = getenvOrFallback("QUANTAS_HOSTNAME", localHostname());
    const std::string ipForLogs = getenvOrFallback("QUANTAS_MACHINE_IP", _myIp);
    const std::string roleForLogs = getenvOrFallback("QUANTAS_PROCESS_ROLE", _isLeader ? "leader" : "follower");
    setenv("QUANTAS_MACHINE_IP", ipForLogs.c_str(), 1);
    setenv("QUANTAS_HOSTNAME", hostname.c_str(), 1);
    setenv("QUANTAS_PROCESS_ROLE", roleForLogs.c_str(), 1);
    {
        const std::string portValue = std::to_string(_myPort);
        setenv("QUANTAS_PROCESS_PORT", portValue.c_str(), 1);
    }

    std::optional<int> loggerPort;
    if (_myPort > 0) {
        loggerPort = _myPort;
    }
    LoggerActivation activation = configureLoggerForExperiment(_rootConfig,
                                                               _experimentConfig,
                                                               experimentIndex,
                                                               logFileBase,
                                                               loggerPort);
    if (activation.enabled) {
        QUANTAS_LOG_DEBUG("coord") << "log destination set to "
                                   << (activation.destination.empty() ? "disabled" : activation.destination);
    }
    const std::string runDirectory = getenvOrFallback("QUANTAS_RUN_DIR", "");
    QUANTAS_LOG_DEBUG("coord") << "run directory = "
                               << (runDirectory.empty() ? "<unset>" : runDirectory);

    QUANTAS_LOG_INFO("coord") << "configuring process. totalPeers=" << _totalPeers
                              << " peersPerProcess=" << _peersPerProcess;
    QUANTAS_LOG_DEBUG("coord") << "local IP resolved to " << _myIp;
    QUANTAS_LOG_INFO("coord") << "binding listener on port " << _myPort;

    QUANTAS_LOG_INFO("coord") << "role = " << (_isLeader ? "leader" : "follower")
                              << " (host=" << hostname
                              << ", ip=" << ipForLogs
                              << ", leader ip=" << _leaderIp
                              << " port=" << _leaderPort << ")";

    {
        std::scoped_lock lock(_processMutex);
        ProcessRecord record;
        record.ip = _myIp;
        record.port = _myPort;
        record.requestedPeers = _peersPerProcess;
        _processes.emplace(processKey(_myIp, _myPort), record);
        _registrationOrder.push_back(processKey(_myIp, _myPort));
    }

    ensureListenerStarted();

    install_socket_safety();

    if (!_isLeader) {
        sendRegistrationToLeader();
    } else {
        distributeAssignmentsIfReady();
    }
}

void ProcessCoordinator::ensureListenerStarted() {
    std::scoped_lock lock(_listenerMutex);
    if (_listenerStarted) {
        if (_listenerThread.joinable()) {
            return;
        }
        _listenerStarted = false;
    }

    _serverFd = socket(AF_INET, SOCK_STREAM, 0);
    if (_serverFd < 0) {
        throw std::runtime_error("Failed to create server socket");
    }

    int opt = 1;
    setsockopt(_serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = INADDR_ANY;
    addr.sin_port = htons(_myPort);
    if (bind(_serverFd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
        close(_serverFd);
        _serverFd = -1;
        throw std::runtime_error("Failed to bind server socket");
    }

    if (listen(_serverFd, 512) < 0) {
        close(_serverFd);
        _serverFd = -1;
        throw std::runtime_error("Failed to listen on server socket");
    }

    _listenerThread = std::thread(&ProcessCoordinator::listenerLoop, this);
    _listenerStarted = true;
}

void ProcessCoordinator::listenerLoop() {
    while (!_shutdownRequested.load()) {
        sockaddr_in clientAddr{};
        socklen_t addrLen = sizeof(clientAddr);
        int clientFd = accept(_serverFd, reinterpret_cast<sockaddr*>(&clientAddr), &addrLen);
        if (clientFd < 0) {
            if (_shutdownRequested.load()) {
                break;
            }
            continue;
        }

        char addrBuffer[INET_ADDRSTRLEN];
        inet_ntop(AF_INET, &(clientAddr.sin_addr), addrBuffer, sizeof(addrBuffer));
        std::string remoteIp(addrBuffer);
        int remotePort = ntohs(clientAddr.sin_port);
        {
            std::scoped_lock lock(_inboundConnectionMutex);
            _inboundConnectionFds.insert(clientFd);
        }

        std::thread(&ProcessCoordinator::connectionLoop, this, clientFd, remoteIp, remotePort).detach();
    }
}

void ProcessCoordinator::connectionLoop(int clientFd, const std::string& remoteIp, int remotePort) {
    for (;;) {
        const auto payload = readFramedMessage(clientFd);
        if (!payload.has_value()) {
            break;
        }
        if (payload->empty()) {
            continue;
        }

        try {
            auto msg = nlohmann::json::parse(*payload);
            processMessage(remoteIp, remotePort, msg);
        } catch (const std::exception&) {
            continue;
        }
    }

    {
        std::scoped_lock lock(_inboundConnectionMutex);
        _inboundConnectionFds.erase(clientFd);
    }
    close(clientFd);
}

void ProcessCoordinator::processMessage(const std::string& remoteIp, int remotePort, const nlohmann::json& msg) {
    std::scoped_lock configLock(_configurationMutex);
    const std::string type = msg.value("type", "");
    const size_t messageExperimentIndex = msg.value("experimentIndex", _experimentIndex);
    if (messageExperimentIndex != _experimentIndex) {
        if (type != kTypeMessage) {
            QUANTAS_LOG_DEBUG("coord") << "ignoring stale message type=" << type
                                       << " for experimentIndex=" << messageExperimentIndex
                                       << " while current experimentIndex=" << _experimentIndex;
        }
        return;
    }
    if (type == kTypeRegister && _isLeader) {
        handleRegister(remoteIp, remotePort, msg);
    } else if (type == kTypeAssignment && !_isLeader) {
        handleAssignment(msg);
    } else if (type == kTypeReady && _isLeader) {
        handleReady(msg);
    } else if (type == kTypeStart) {
        handleStart();
    } else if (type == kTypeStop) {
        handleStop();
    } else if (type == kTypePeerDone && _isLeader) {
        interfaceId peerId = msg.value("peerId", static_cast<interfaceId>(NO_PEER_ID));
        handlePeerDone(remoteIp, remotePort, peerId);
    } else if (type == kTypeMessage) {
        handleInboundMessage(msg);
    }
}

void ProcessCoordinator::handleRegister(const std::string& remoteIp, int remotePort, const nlohmann::json& msg) {
    ProcessRecord record;
    record.ip = msg.value("ip", remoteIp);
    record.port = msg.value("port", remotePort);
    record.requestedPeers = msg.value("requestedPeers", _peersPerProcess);

    const std::string key = processKey(record.ip, record.port);
    bool knownProcess = false;
    bool canResendAssignment = false;
    {
        std::scoped_lock lock(_processMutex);
        auto existing = _processes.find(key);
        if (existing == _processes.end()) {
            _processes.emplace(key, record);
            _registrationOrder.push_back(key);
        } else {
            knownProcess = true;
            record.assignedPeers = existing->second.assignedPeers;
            record.ready = existing->second.ready;
            existing->second = record;
            canResendAssignment = !_startSignal.load() &&
                                  static_cast<int>(_allPeers.size()) >= _totalPeers &&
                                  !record.assignedPeers.empty();
        }

        if (!knownProcess) {
            auto inserted = _processes.find(key);
            if (inserted != _processes.end()) {
                inserted->second = record;
            }
        }
    }
    QUANTAS_LOG_DEBUG("coord") << "registration received from " << key
                               << " requestedPeers=" << record.requestedPeers;
    if (knownProcess && canResendAssignment) {
        QUANTAS_LOG_INFO("coord") << "re-sending assignment to " << key;
        sendAssignmentToProcess(key, record);
        return;
    }
    distributeAssignmentsIfReady();
}

void ProcessCoordinator::handleReady(const nlohmann::json& msg) {
    const std::string ip = msg.value("ip", "");
    const int port = msg.value("port", -1);
    if (ip.empty() || port <= 0) return;

    const std::string key = processKey(ip, port);
    {
        std::scoped_lock lock(_processMutex);
        auto it = _processes.find(key);
        if (it != _processes.end()) {
            it->second.ready = true;
        }
    }
    QUANTAS_LOG_DEBUG("coord") << "ready received from " << key;
    broadcastStartIfReady();
}

void ProcessCoordinator::handlePeerDone(const std::string&, int, interfaceId peerId) {
    if (peerId == NO_PEER_ID) {
        return;
    }

    bool shouldBroadcast = false;
    {
        std::scoped_lock lock(_completedMutex);
        _completedPeers.insert(peerId);
        if (static_cast<int>(_completedPeers.size()) >= _totalPeers) {
            shouldBroadcast = true;
        }
    }

    if (shouldBroadcast) {
        broadcastStop("peer_done");
    }
}

void ProcessCoordinator::distributeAssignmentsIfReady() {
    std::unordered_map<std::string, ProcessRecord> snapshot;
    std::vector<std::string> order;
    {
        std::scoped_lock lock(_processMutex);
        snapshot = _processes;
        order = _registrationOrder;
    }

    int totalRequested = 0;
    for (const auto& key : order) {
        const auto it = snapshot.find(key);
        if (it != snapshot.end()) {
            totalRequested += it->second.requestedPeers;
        }
    }
    QUANTAS_LOG_DEBUG("coord") << "distributeAssignments check. totalRequested=" << totalRequested
                               << " required=" << _totalPeers;
    if (totalRequested < _totalPeers) {
        return;
    }

    TopologyResult topology = buildTopology(_experimentConfig["topology"]);
    std::unordered_map<interfaceId, PeerEndpoint> endpoints;
    endpoints.reserve(_totalPeers);
    std::vector<ProcessEndpointRange> ranges;
    ranges.reserve(order.size());

    int nextId = 0;
    for (const auto& key : order) {
        auto it = snapshot.find(key);
        if (it == snapshot.end()) continue;
        it->second.assignedPeers.clear();
        const int firstAssigned = nextId;
        int available = std::min(it->second.requestedPeers, _totalPeers - nextId);
        for (int i = 0; i < available; ++i) {
            interfaceId assigned = static_cast<interfaceId>(nextId++);
            it->second.assignedPeers.push_back(assigned);
            PeerEndpoint endpoint;
            endpoint.id = assigned;
            endpoint.ip = it->second.ip;
            endpoint.port = it->second.port;
            endpoints.emplace(endpoint.id, endpoint);
        }
        if (available > 0) {
            ProcessEndpointRange range;
            range.firstId = static_cast<interfaceId>(firstAssigned);
            range.lastId = static_cast<interfaceId>(nextId - 1);
            range.ip = it->second.ip;
            range.port = it->second.port;
            ranges.push_back(range);
        }
        if (nextId >= _totalPeers) break;
    }

    {
        std::scoped_lock lock(_processMutex);
        for (const auto& key : order) {
            auto it = _processes.find(key);
            auto snapshotIt = snapshot.find(key);
            if (it != _processes.end() && snapshotIt != snapshot.end()) {
                it->second.assignedPeers = snapshotIt->second.assignedPeers;
            }
        }
    }

    {
        std::scoped_lock lock(_endpointMutex);
        _allPeers = endpoints;
        _peerRanges = ranges;
    }

    for (const auto& key : order) {
        auto it = snapshot.find(key);
        if (it == snapshot.end()) continue;
        sendAssignmentToProcess(key, it->second);
    }
}

void ProcessCoordinator::sendAssignmentToProcess(const std::string& key, const ProcessRecord& record) {
    TopologyResult topology = buildTopology(_experimentConfig["topology"]);

    std::vector<ProcessEndpointRange> rangeSnapshot;
    {
        std::scoped_lock lock(_endpointMutex);
        rangeSnapshot = _peerRanges;
    }

    nlohmann::json peersJson = nlohmann::json::array();
    for (interfaceId id : record.assignedPeers) {
        nlohmann::json entry;
        entry["id"] = id;
        entry["neighbors"] = topology.assignments[static_cast<size_t>(id)].neighbors;
        peersJson.push_back(entry);
    }

    nlohmann::json rangesJson = nlohmann::json::array();
    for (const auto& range : rangeSnapshot) {
        nlohmann::json e;
        e["firstId"] = range.firstId;
        e["lastId"] = range.lastId;
        e["ip"] = range.ip;
        e["port"] = range.port;
        rangesJson.push_back(e);
    }

    nlohmann::json message = {
        {"type", kTypeAssignment},
        {"experimentIndex", _experimentIndex},
        {"peers", peersJson},
        {"peerRanges", rangesJson}
    };

    const std::string leaderKey = processKey(_myIp, _myPort);
    if (key == leaderKey) {
        handleAssignment(message);
    } else {
        sendJson(record.ip, record.port, message);
    }
}

void ProcessCoordinator::handleAssignment(const nlohmann::json& msg) {
    std::vector<PeerAssignment> assignments;
    if (msg.contains("peers") && msg["peers"].is_array()) {
        for (const auto& item : msg["peers"]) {
            PeerAssignment assignment;
            assignment.id = item.value("id", static_cast<interfaceId>(NO_PEER_ID));
            if (item.contains("neighbors")) {
                for (const auto& neighbor : item["neighbors"]) {
                    assignment.neighbors.insert(neighbor.get<interfaceId>());
                }
            }
            assignments.push_back(std::move(assignment));
        }
    }

    if (msg.contains("peerRanges") && msg["peerRanges"].is_array()) {
        std::scoped_lock lock(_endpointMutex);
        _allPeers.clear();
        _peerRanges.clear();
        for (const auto& item : msg["peerRanges"]) {
            ProcessEndpointRange range;
            range.firstId = item.value("firstId", static_cast<interfaceId>(NO_PEER_ID));
            range.lastId = item.value("lastId", static_cast<interfaceId>(NO_PEER_ID));
            range.ip = item.value("ip", "");
            range.port = item.value("port", -1);
            _peerRanges.push_back(range);
        }

        for (const auto& assignment : assignments) {
            for (const auto& range : _peerRanges) {
                if (assignment.id < range.firstId || assignment.id > range.lastId) {
                    continue;
                }
                PeerEndpoint endpoint;
                endpoint.id = assignment.id;
                endpoint.ip = range.ip;
                endpoint.port = range.port;
                _allPeers.emplace(endpoint.id, endpoint);
                break;
            }
        }
    }

    {
        std::scoped_lock lock(_assignmentMutex);
        _localAssignments = assignments;
        _assignmentsReady = true;
    }
    QUANTAS_LOG_INFO("coord") << "assignment received with " << _localAssignments.size() << " peers.";
    _assignmentCv.notify_all();
}

std::vector<ProcessCoordinator::PeerAssignment> ProcessCoordinator::waitForAssignments() {
    std::unique_lock lock(_assignmentMutex);
    _assignmentCv.wait(lock, [&]() { return _assignmentsReady.load(); });
    QUANTAS_LOG_DEBUG("coord") << "waitForAssignments returning "
                               << _localAssignments.size() << " assignments.";
    return _localAssignments;
}

void ProcessCoordinator::registerLocalAssignments(const std::vector<PeerAssignment>& assignments) {
    std::scoped_lock lock(_assignmentMutex);
    _localAssignments = assignments;
    _assignmentsReady = true;
    _assignmentCv.notify_all();
}

void ProcessCoordinator::markReady() {
    if (_isLeader) {
        {
            std::scoped_lock lock(_processMutex);
            auto it = _processes.find(processKey(_myIp, _myPort));
            if (it != _processes.end()) {
                it->second.ready = true;
            }
        }
        QUANTAS_LOG_INFO("coord") << "leader marked ready.";
        broadcastStartIfReady();
    } else {
        QUANTAS_LOG_INFO("coord") << "follower marked ready, notifying leader.";
        nlohmann::json msg = {
            {"type", kTypeReady},
            {"experimentIndex", _experimentIndex},
            {"ip", _myIp},
            {"port", _myPort}
        };
        sendJson(_leaderIp, _leaderPort, msg);
    }
}

void ProcessCoordinator::broadcastStartIfReady() {
    if (!_isLeader) return;

    bool allReady = true;
    int totalRequested = 0;
    std::vector<ProcessRecord> records;
    std::vector<std::string> order;
    std::vector<std::string> notReadyKeys;
    {
        std::scoped_lock lock(_processMutex);
        order = _registrationOrder;
        for (const auto& key : order) {
            auto it = _processes.find(key);
            if (it == _processes.end()) continue;
            records.push_back(it->second);
            totalRequested += it->second.requestedPeers;
            if (!it->second.ready) {
                allReady = false;
                notReadyKeys.push_back(key);
            }
        }
    }
    if (!allReady || totalRequested < _totalPeers || records.empty()) {
        std::ostringstream waitingOn;
        for (size_t i = 0; i < notReadyKeys.size(); ++i) {
            if (i != 0) {
                waitingOn << ", ";
            }
            waitingOn << notReadyKeys[i];
        }
        QUANTAS_LOG_DEBUG("coord") << "start gate blocked. allReady=" << (allReady ? "true" : "false")
                                   << " totalRequested=" << totalRequested
                                   << " required=" << _totalPeers
                                   << " records=" << records.size()
                                   << " waitingOn=[" << waitingOn.str() << "]";
        return;
    }

    QUANTAS_LOG_INFO("coord") << "all processes ready, broadcasting start.";

    nlohmann::json message = {
        {"type", kTypeStart},
        {"experimentIndex", _experimentIndex}
    };

    for (const auto& record : records) {
        if (record.ip == _myIp && record.port == _myPort) {
            handleStart();
        } else {
            sendJson(record.ip, record.port, message);
        }
    }

    if (_stopCondition.type == StopType::Time && !_stopTimerActive.exchange(true)) {
        QUANTAS_LOG_DEBUG("coord") << "starting stop timer thread.";
        _stopTimerThread = std::thread(&ProcessCoordinator::startStopTimer, this);
    }
}

void ProcessCoordinator::handleStart() {
    QUANTAS_LOG_INFO("coord") << "received start signal.";
    {
        std::lock_guard lock(_startMutex);
        _startSignal = true;
    }
    _startCv.notify_all();
}

void ProcessCoordinator::waitForStartSignal() {
    QUANTAS_LOG_INFO("coord") << "waiting for start signal...";
    std::unique_lock lock(_startMutex);
    _startCv.wait(lock, [&]() { return _startSignal.load(); });
}

bool ProcessCoordinator::shouldStop() const {
    return _stopSignal.load();
}

void ProcessCoordinator::waitForStop() {
    QUANTAS_LOG_INFO("coord") << "waiting for stop signal...";
    std::unique_lock lock(_stopMutex);
    _stopCv.wait(lock, [&]() { return _stopSignal.load(); });
}

void ProcessCoordinator::notifyPeerStopped(interfaceId id) {
    if (id == NO_PEER_ID) {
        return;
    }

    if (_isLeader) {
        bool shouldBroadcast = false;
        {
            std::scoped_lock lock(_completedMutex);
            _completedPeers.insert(id);
            if (static_cast<int>(_completedPeers.size()) >= _totalPeers) {
                shouldBroadcast = true;
            }
        }
        if (shouldBroadcast) {
            broadcastStop("peer_done");
        }
    } else {
        nlohmann::json msg = {
            {"type", kTypePeerDone},
            {"experimentIndex", _experimentIndex},
            {"peerId", id}
        };
        sendJson(_leaderIp, _leaderPort, msg);
    }
}

void ProcessCoordinator::handleStop() {
    QUANTAS_LOG_INFO("coord") << "received stop signal.";
    {
        std::lock_guard lock(_stopMutex);
        _stopSignal = true;
    }
    _stopCv.notify_all();
}

void ProcessCoordinator::broadcastStop(const std::string& reason) {
    if (_stopSignal.exchange(true)) {
        return;
    }

    QUANTAS_LOG_INFO("coord") << "broadcasting stop: " << reason;

    std::vector<ProcessRecord> records;
    {
        std::scoped_lock lock(_processMutex);
        for (const auto& key : _registrationOrder) {
            auto it = _processes.find(key);
            if (it != _processes.end()) {
                records.push_back(it->second);
            }
        }
    }

    nlohmann::json message = {
        {"type", kTypeStop},
        {"experimentIndex", _experimentIndex},
        {"reason", reason}
    };

    for (const auto& record : records) {
        if (record.ip == _myIp && record.port == _myPort) {
            handleStop();
        } else {
            sendJson(record.ip, record.port, message);
        }
    }

    _stopCv.notify_all();
}

void ProcessCoordinator::startStopTimer() {
    if (_stopCondition.type != StopType::Time) return;
    QUANTAS_LOG_DEBUG("coord") << "stop timer started for " << _stopCondition.duration.count() << "ms";
    std::this_thread::sleep_for(_stopCondition.duration);
    if (!_stopSignal.load()) {
        broadcastStop("time");
    }
}

void ProcessCoordinator::registerInterface(interfaceId id, NetworkInterfaceConcrete* iface) {
    std::scoped_lock lock(_interfaceMutex);
    _localInterfaces[id] = iface;
    {
        std::scoped_lock queueLock(_inboundMutex);
        _inboundQueues[id]; // ensure queue exists
    }
}

void ProcessCoordinator::unregisterInterface(interfaceId id) {
    std::scoped_lock lock(_interfaceMutex);
    _localInterfaces.erase(id);
    {
        std::scoped_lock queueLock(_inboundMutex);
        _inboundQueues.erase(id);
    }
}

void ProcessCoordinator::enqueueInbound(interfaceId to, interfaceId from, const nlohmann::json& body) {
    Packet packet(to, from, body);
    {
        std::scoped_lock lock(_inboundMutex);
        _inboundQueues[to].push_back(packet);
    }
}

void ProcessCoordinator::drainInbound(interfaceId id, std::deque<Packet>& target) {
    std::scoped_lock lock(_inboundMutex);
    auto it = _inboundQueues.find(id);
    if (it == _inboundQueues.end()) {
        return;
    }
    auto& queue = it->second;
    while (!queue.empty()) {
        target.push_back(std::move(queue.front()));
        queue.pop_front();
    }
}

void ProcessCoordinator::handleInboundMessage(const nlohmann::json& msg) {
    interfaceId to = msg.value("to_id", static_cast<interfaceId>(NO_PEER_ID));
    interfaceId from = msg.value("from_id", static_cast<interfaceId>(NO_PEER_ID));
    nlohmann::json body = msg.value("body", nlohmann::json::object());

    {
        std::scoped_lock lock(_interfaceMutex);
        auto it = _localInterfaces.find(to);
        if (it != _localInterfaces.end()) {
            enqueueInbound(to, from, body);
        }
    }
}

void ProcessCoordinator::unicast(interfaceId from, interfaceId to, const nlohmann::json& body) {
    if (_stopSignal.load()) {
        return;
    }

    {
        std::scoped_lock lock(_interfaceMutex);
        auto it = _localInterfaces.find(to);
        if (it != _localInterfaces.end()) {
            enqueueInbound(to, from, body);
            return;
        }
    }

    const auto endpoint = endpointForPeer(to);
    if (!endpoint.has_value()) {
        return;
    }

    nlohmann::json msg = {
        {"type", kTypeMessage},
        {"experimentIndex", _experimentIndex},
        {"from_id", from},
        {"to_id", to},
        {"body", body}
    };
    sendJson(endpoint->ip, endpoint->port, msg);
}

bool ProcessCoordinator::ownsPeer(interfaceId id) const {
    std::scoped_lock lock(_interfaceMutex);
    return _localInterfaces.find(id) != _localInterfaces.end();
}

ProcessCoordinator::StopCondition ProcessCoordinator::stopCondition() const {
    return _stopCondition;
}

const std::unordered_map<interfaceId, ProcessCoordinator::PeerEndpoint>& ProcessCoordinator::endpoints() const {
    return _allPeers;
}

const std::vector<ProcessCoordinator::PeerAssignment>& ProcessCoordinator::localAssignments() const {
    return _localAssignments;
}

void ProcessCoordinator::cleanupExperiment() {
    _assignmentsReady = false;
    _startSignal = false;
    _stopSignal = false;
    {
        std::scoped_lock lock(_completedMutex);
        _completedPeers.clear();
    }
    {
        std::scoped_lock lock(_assignmentMutex);
        _localAssignments.clear();
    }
    {
        std::scoped_lock lock(_interfaceMutex);
        _localInterfaces.clear();
    }
    {
        std::scoped_lock lock(_inboundMutex);
        _inboundQueues.clear();
    }
    if (_stopTimerThread.joinable()) {
        _stopTimerActive = false;
        _stopTimerThread.join();
    }
}

void ProcessCoordinator::shutdown() {
    {
        std::scoped_lock lock(_listenerMutex);
        if (!_listenerStarted) {
            return;
        }
    }

    _shutdownRequested = true;
    closeAllConnections();

    int serverFdCopy = -1;
    {
        std::scoped_lock lock(_listenerMutex);
        serverFdCopy = _serverFd;
    }

    if (serverFdCopy >= 0) {
        int dummy = socket(AF_INET, SOCK_STREAM, 0);
        if (dummy >= 0) {
            sockaddr_in addr{};
            addr.sin_family = AF_INET;
            addr.sin_port = htons(_myPort);
            addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
            connect(dummy, reinterpret_cast<sockaddr*>(&addr), sizeof(addr));
#ifdef _WIN32
            closesocket(dummy);
#else
            close(dummy);
#endif
        }

#ifdef _WIN32
        ::shutdown(serverFdCopy, SD_BOTH);
        closesocket(serverFdCopy);
#else
        ::shutdown(serverFdCopy, SHUT_RDWR);
        close(serverFdCopy);
#endif

        std::scoped_lock lockFD(_listenerMutex);
        _serverFd = -1;
    }

    if (_listenerThread.joinable()) {
        _listenerThread.join();
    }

    if (_stopTimerThread.joinable()) {
        _stopTimerActive = false;
        _stopTimerThread.join();
    }

    {
        std::scoped_lock lock(_listenerMutex);
        _listenerStarted = false;
    }
}

void ProcessCoordinator::sendJson(const std::string& ip, int port, const nlohmann::json& msg) {
    constexpr int kMaxAttempts = 25;
    constexpr std::chrono::milliseconds kRetryDelay(100);
    const std::string key = processKey(ip, port);
    std::string payload = msg.dump();
    const uint32_t networkLength = htonl(static_cast<uint32_t>(payload.size()));
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        auto connection = getOrCreateOutboundConnection(ip, port);
        if (!connection || connection->fd < 0) {
            std::this_thread::sleep_for(kRetryDelay);
            continue;
        }

        bool sendFailed = false;
        {
            std::scoped_lock lock(connection->mutex);
            if (connection->fd < 0 ||
                !sendAll(connection->fd, reinterpret_cast<const char*>(&networkLength), sizeof(networkLength)) ||
                !sendAll(connection->fd, payload.data(), payload.size())) {
                sendFailed = true;
            }
        }

        if (!sendFailed) {
            return;
        }

        closeOutboundConnection(key);
        std::this_thread::sleep_for(kRetryDelay);
    }
}

void ProcessCoordinator::sendRegistrationToLeader() {
    nlohmann::json msg = {
        {"type", kTypeRegister},
        {"experimentIndex", _experimentIndex},
        {"ip", _myIp},
        {"port", _myPort},
        {"requestedPeers", _peersPerProcess}
    };
    sendJson(_leaderIp, _leaderPort, msg);
    QUANTAS_LOG_DEBUG("coord") << "follower sent registration to leader.";
}

std::optional<ProcessCoordinator::PeerEndpoint> ProcessCoordinator::endpointForPeer(interfaceId id) {
    std::scoped_lock lock(_endpointMutex);
    auto direct = _allPeers.find(id);
    if (direct != _allPeers.end()) {
        return direct->second;
    }

    for (const auto& range : _peerRanges) {
        if (range.firstId == NO_PEER_ID || range.lastId == NO_PEER_ID) {
            continue;
        }
        if (id < range.firstId || id > range.lastId) {
            continue;
        }
        PeerEndpoint endpoint;
        endpoint.id = id;
        endpoint.ip = range.ip;
        endpoint.port = range.port;
        return endpoint;
    }

    return std::nullopt;
}

std::shared_ptr<ProcessCoordinator::OutboundConnection> ProcessCoordinator::getOrCreateOutboundConnection(const std::string& ip, int port) {
    const std::string key = processKey(ip, port);
    {
        std::scoped_lock lock(_outboundMutex);
        auto it = _outboundConnections.find(key);
        if (it != _outboundConnections.end() && it->second && it->second->fd >= 0) {
            return it->second;
        }
    }

    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return nullptr;
    }

    sockaddr_in addr{};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, ip.c_str(), &addr.sin_addr);

    if (connect(sock, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        close(sock);
        return nullptr;
    }

    auto connection = std::make_shared<OutboundConnection>();
    connection->fd = sock;
    {
        std::scoped_lock lock(_outboundMutex);
        auto& slot = _outboundConnections[key];
        if (!slot || slot->fd < 0) {
            slot = connection;
            return slot;
        }
        close(sock);
        return slot;
    }
}

void ProcessCoordinator::closeOutboundConnection(const std::string& key) {
    std::shared_ptr<OutboundConnection> connection;
    {
        std::scoped_lock lock(_outboundMutex);
        auto it = _outboundConnections.find(key);
        if (it == _outboundConnections.end()) {
            return;
        }
        connection = it->second;
        _outboundConnections.erase(it);
    }

    if (!connection) {
        return;
    }

    std::scoped_lock lock(connection->mutex);
    if (connection->fd >= 0) {
        close(connection->fd);
        connection->fd = -1;
    }
}

void ProcessCoordinator::closeAllConnections() {
    std::vector<std::shared_ptr<OutboundConnection>> outboundConnections;
    {
        std::scoped_lock lock(_outboundMutex);
        for (auto& [_, connection] : _outboundConnections) {
            if (connection) {
                outboundConnections.push_back(connection);
            }
        }
        _outboundConnections.clear();
    }

    for (const auto& connection : outboundConnections) {
        std::scoped_lock lock(connection->mutex);
        if (connection->fd >= 0) {
            close(connection->fd);
            connection->fd = -1;
        }
    }

    std::vector<int> inboundConnections;
    {
        std::scoped_lock lock(_inboundConnectionMutex);
        inboundConnections.assign(_inboundConnectionFds.begin(), _inboundConnectionFds.end());
        _inboundConnectionFds.clear();
    }

    for (int fd : inboundConnections) {
        if (fd >= 0) {
            ::shutdown(fd, SHUT_RDWR);
        }
    }
}

std::string ProcessCoordinator::processKey(const std::string& ip, int port) const {
    std::ostringstream oss;
    oss << ip << ":" << port;
    return oss.str();
}

} // namespace quantas
