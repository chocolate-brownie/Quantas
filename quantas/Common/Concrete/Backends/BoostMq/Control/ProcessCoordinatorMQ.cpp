#include "quantas/Common/Concrete/Backends/BoostMq/Control/ProcessCoordinatorMQ.hpp"
#include "quantas/Common/Concrete/Backends/BoostMq/Control/QueueConfig.hpp"
#include "quantas/Common/Concrete/Runtime/Topology/PeerAssignment.hpp"
#include "quantas/Common/Logger.hpp"
#include <algorithm>
#include <atomic>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
#include <boost/date_time/posix_time/posix_time_duration.hpp>
#include <boost/date_time/posix_time/posix_time_types.hpp>
#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/exceptions.hpp>
#include <boost/interprocess/interprocess_fwd.hpp>
#include <boost/interprocess/streams/bufferstream.hpp>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <mutex>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_set>
#include <vector>

/*
 * BoostMQ control protocol:
 * - leader creates shared control queues;
 * - peers create per-peer inboxes and send ready;
 * - leader sends topology assignments, start, and stop signals;
 * - peers send done notifications so the leader can build completion evidence.
 */

using namespace boost::interprocess;

namespace quantas {
namespace {
constexpr unsigned int kStartTrigger = 2;
constexpr unsigned int kStopTrigger = 3;
constexpr unsigned int kDoneTrigger = 4;
constexpr int kControlSendAttempts = 200;
constexpr int kControlSendWaitMs = 10;
std::mutex gCompletedMutex;
std::unordered_set<interfaceId> gCompletedPeers;

/* Utility: Build the exact BoostMQ queue name used for one peer's control messages, such as
 * assignment, start, and stop. */
std::string peerControlQueueName(interfaceId id) {
    return "peer_" + std::to_string(id) + "_control";
}

/* Utility: Build the exact BoostMQ queue name used for one peer's algorithm packets. */
std::string peerDataQueueName(interfaceId id) { return "peer_" + std::to_string(id) + "_data"; }

/* Utility: Try to place one control message on a queue before a short deadline. Returns false when
 * the queue stays full until that deadline. */
bool timedSendControlMessage(message_queue& mq, const void* message, size_t messageSize,
                             int waitMs) {
    const auto deadline = boost::posix_time::microsec_clock::universal_time() +
                          boost::posix_time::milliseconds(waitMs);
    return mq.timed_send(message, messageSize, 0, deadline);
}

/* Utility: Check whether a named BoostMQ queue can currently be opened. */
bool queueExists(const std::string& queueName) {
    try {
        message_queue mq(open_only, queueName.c_str());
        return true;
    } catch (const interprocess_exception&) {
        return false;
    }
}

/* Utility: Wait for the leader to remove a peer queue during shutdown. Give up with an error
 * instead of waiting forever. */
void waitForQueueRemoval(const std::string& queueName) {
    for (int attempt = 1; attempt <= kControlSendAttempts; ++attempt) {
        if (!queueExists(queueName)) {
            return;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kControlSendWaitMs));
    }

    throw std::runtime_error("Timed out waiting for queue removal: " + queueName);
}
} // namespace

/* Utility: Return the single coordinator object used inside this process. Each leader or peer
 * process gets its own independent singleton. */
ProcessCoordinatorMQ& ProcessCoordinatorMQ::instance() {
    static ProcessCoordinatorMQ coordinator;
    return coordinator;
}

/* Major operation: Load all coordinator state for one experiment. This sets the process role, peer
 * identity, peer count, queue settings, and stop mode, then clears state left by the previous test
 * or experiment. It does not create any operating-system queues. */
void ProcessCoordinatorMQ::configureExperiment(size_t experimentIndex, const std::string& peerType,
                                               bool isLeader, size_t totalPeers, interfaceId myId,
                                               const std::string& logFileBase, StopMode stopMode,
                                               const BoostMqQueueConfig& queueConfig) {
    _queueConfig = queueConfig;
    _experimentIndex = experimentIndex;
    _peerType = peerType;
    _isLeader = isLeader;
    _totalPeers = totalPeers;
    _myId = myId;
    _configured = true;
    _logFileBase = logFileBase;
    _stopMode = stopMode;
    _stopSignal = false;
    {
        std::scoped_lock lock(gCompletedMutex);
        gCompletedPeers.clear();
    }
    _myBarrier.reset();
    _myControlInbox.reset();
}

/* Utility: Configure only the basic process role, peer count, and peer ID. This wrapper supplies
 * default experiment settings and delegates to the full configuration function. */
void ProcessCoordinatorMQ::configureProcess(bool isLeader, size_t totalPeers, interfaceId myId) {
    BoostMqQueueConfig queueConfig;
    queueConfig.controlQueueCapacity = totalPeers;
    configureExperiment(0, "", isLeader, totalPeers, myId, "", StopMode::FixedRounds, queueConfig);
}

/* Major operation: Create the two leader-owned shared queues for one test. Peers send their IDs to
 * `mq_barrier` when ready and to `mq_done` when their work is complete. Old queues with those exact
 * names are removed first. */
void ProcessCoordinatorMQ::createBarrier() {
    if (!_isLeader)
        return;

    QUANTAS_LOG_INFO("coord") << "creating barrier queue mq_barrier";
    message_queue::remove("mq_barrier");
    message_queue::remove("mq_done");

    try {
        // The queue that peers sends the "I am ready" signal to
        _myBarrier.emplace(create_only, "mq_barrier", _queueConfig.controlQueueCapacity,
                           sizeof(interfaceId));

        // The queue that peers send their id and success state to
        message_queue doneQueue(create_only, "mq_done", _queueConfig.controlQueueCapacity,
                                sizeof(PeerCompletionMessage));
    } catch (const interprocess_exception& ex) {
        throw std::runtime_error(std::string("Failed to ::createBarrier queue: ") + ex.what());
    }
}

/* Major operation: Create this peer's two private inboxes. The control inbox receives framework
 * messages; the data inbox receives algorithm packets. The leader does not create a peer inbox for
 * itself. */
void ProcessCoordinatorMQ::createInbox() {
    if (_isLeader)
        return;

    std::string controlQueueName = peerControlQueueName(_myId);
    std::string dataQueueName = peerDataQueueName(_myId);

    QUANTAS_LOG_INFO("coord") << "peer " << _myId << " creating control inbox " << controlQueueName
                              << " and data inbox " << dataQueueName;

    message_queue::remove(controlQueueName.c_str());
    message_queue::remove(dataQueueName.c_str());

    try {
        _myControlInbox.emplace(create_only, controlQueueName.c_str(),
                                _queueConfig.controlQueueCapacity,
                                _queueConfig.maxMessageSizeBytes);
        message_queue dataInbox(create_only, dataQueueName.c_str(), _queueConfig.dataQueueCapacity,
                                _queueConfig.maxMessageSizeBytes);
    } catch (const interprocess_exception& ex) {
        throw std::runtime_error("Failed to ::createInbox queues for peer " +
                                 std::to_string(_myId) + ": " + ex.what());
    }
}

/* Major operation: Tell the leader that this peer has created its queues and is ready for
 * assignment and start. The peer sends its own ID and retries short timed sends so a temporarily
 * full queue does not block forever. */
void ProcessCoordinatorMQ::sendReady() {
    if (_isLeader)
        return;

    for (int attempt = 1; attempt <= kControlSendAttempts; ++attempt) {
        try {
            message_queue mq(open_only, "mq_barrier");
            const interfaceId readyPeerId = _myId;
            if (timedSendControlMessage(mq, &readyPeerId, sizeof(readyPeerId),
                                        kControlSendWaitMs)) {
                QUANTAS_LOG_INFO("coord") << "peer " << _myId << " sent ready to barrier";
                return;
            }
        } catch (const interprocess_exception& ex) {
            if (attempt == kControlSendAttempts) {
                throw std::runtime_error("Failed to ::sendReady queue for peer " +
                                         std::to_string(_myId) + ": " + ex.what());
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kControlSendWaitMs));
    }

    throw std::runtime_error("Timed out sending ready for peer " + std::to_string(_myId) +
                             " to mq_barrier");
}

/* Major operation: Hold the leader at the startup barrier until every expected peer ID has reported
 * ready exactly once. Invalid IDs or message sizes fail startup, while duplicate IDs are logged and
 * ignored. */
void ProcessCoordinatorMQ::waitForAllReady(std::vector<interfaceId>& readyPeerIds,
                                           bool& readyTimedOut) {
    if (!_isLeader)
        return;

    readyPeerIds.clear();
    readyTimedOut = false;
    std::unordered_set<interfaceId> uniqueReadyPeers;

    QUANTAS_LOG_INFO("coord") << "leader waiting for " << _totalPeers << " unique ready peers";

    try {
        const auto deadline = boost::posix_time::microsec_clock::universal_time() +
                              boost::posix_time::milliseconds(_queueConfig.readyTimeoutMs);

        while (uniqueReadyPeers.size() < _totalPeers) {
            unsigned int priority = 0;
            interfaceId readyPeerId = NO_PEER_ID;
            message_queue::size_type recvd_size;

            const bool received = _myBarrier->timed_receive(&readyPeerId, sizeof(readyPeerId),
                                                            recvd_size, priority, deadline);

            if (!received) {
                readyPeerIds.assign(uniqueReadyPeers.begin(), uniqueReadyPeers.end());
                std::sort(readyPeerIds.begin(), readyPeerIds.end());
                readyTimedOut = true;
                return;
            }

            if (recvd_size != sizeof(readyPeerId)) {
                throw std::runtime_error("Invalid ready message size: expected " +
                                         std::to_string(sizeof(readyPeerId)) + " bytes, received " +
                                         std::to_string(recvd_size));
            }

            if (readyPeerId < 0 || static_cast<size_t>(readyPeerId) >= _totalPeers) {
                throw std::runtime_error("Invalid ready peer ID " + std::to_string(readyPeerId) +
                                         "; expected 0 to " + std::to_string(_totalPeers - 1));
            }

            const bool inserted = uniqueReadyPeers.insert(readyPeerId).second;

            if (!inserted) {
                QUANTAS_LOG_WARN("coord")
                    << "leader ignored duplicate ready from peer " << readyPeerId;
                continue;
            }

            QUANTAS_LOG_INFO("coord") << "leader accepted ready from peer " << readyPeerId;
        }

        readyPeerIds.assign(uniqueReadyPeers.begin(), uniqueReadyPeers.end());
        std::sort(readyPeerIds.begin(), readyPeerIds.end());
        QUANTAS_LOG_INFO("coord") << "leader received ready from all expected peers";
    } catch (const interprocess_exception& ex) {
        throw std::runtime_error("Failed to ::waitForAllReady for peer " + std::to_string(_myId) +
                                 ": " + ex.what());
    }
}

/* Major operation: Send the normal start signal to every peer control inbox. Peers begin their
 * algorithm work only after receiving this signal. */
void ProcessCoordinatorMQ::broadcastStart() {
    if (!_isLeader)
        return;

    QUANTAS_LOG_INFO("coord") << "leader broadcasting start to " << _totalPeers << " peers";

    for (size_t i = 0; i < _totalPeers; ++i) {
        const std::string queueName = peerControlQueueName(static_cast<interfaceId>(i));
        const auto deadline = boost::posix_time::microsec_clock::universal_time() +
                              boost::posix_time::milliseconds(_queueConfig.controlSendTimeoutMs);
        try {
            message_queue mq(open_only, queueName.c_str());
            const unsigned int trigger = kStartTrigger;
            if (!mq.timed_send(&trigger, sizeof(trigger), 0, deadline)) {
                throw std::runtime_error("Timed out sending start to peer " + std::to_string(i) +
                                         " after " +
                                         std::to_string(_queueConfig.controlSendTimeoutMs) + " ms");
            }
        } catch (const interprocess_exception& ex) {
            throw std::runtime_error(
                "Failed sending start to peer " + std::to_string(i) + " within " +
                std::to_string(_queueConfig.controlSendTimeoutMs) + " ms: " + ex.what());
        }
    }
}

/* Major operation: Send the normal stop signal to every peer after the leader has received all
 * expected done notifications.  */
void ProcessCoordinatorMQ::broadcastStop() {
    if (!_isLeader)
        return;

    QUANTAS_LOG_INFO("coord") << "leader broadcasting stop to " << _totalPeers << " peers";

    for (size_t i = 0; i < _totalPeers; ++i) {
        const std::string queueName = peerControlQueueName(static_cast<interfaceId>(i));
        const auto deadline = boost::posix_time::microsec_clock::universal_time() +
                              boost::posix_time::milliseconds(_queueConfig.controlSendTimeoutMs);
        try {
            message_queue mq(open_only, queueName.c_str());
            const unsigned int trigger = kStopTrigger;
            if (!mq.timed_send(&trigger, sizeof(trigger), 0, deadline)) {
                throw std::runtime_error("Timed out sending stop to peer " + std::to_string(i) +
                                         " after " +
                                         std::to_string(_queueConfig.controlSendTimeoutMs) + " ms");
            }
        } catch (const interprocess_exception& ex) {
            throw std::runtime_error(
                "Failed sending stop to peer " + std::to_string(i) + " within " +
                std::to_string(_queueConfig.controlSendTimeoutMs) + " ms: " + ex.what());
        }
    }
}

/* Major recovery operation: Try to stop every peer after a timeout or other failure. Missing or
 * full queues are logged instead of throwing, so cleanup can continue for the remaining peers. */
void ProcessCoordinatorMQ::broadcastStopBestEffort() {
    if (!_isLeader)
        return;

    QUANTAS_LOG_WARN("coord") << "leader best-effort broadcasting stop to " << _totalPeers
                              << " peers";
    for (size_t i = 0; i < _totalPeers; ++i) {
        const std::string queueName = peerControlQueueName(static_cast<interfaceId>(i));
        try {
            message_queue mq(open_only, queueName.c_str());
            const unsigned int trigger = kStopTrigger;
            const auto deadline = boost::posix_time::microsec_clock::universal_time() +
                                  boost::posix_time::milliseconds(100);
            if (!mq.timed_send(&trigger, sizeof(trigger), 0, deadline)) {
                QUANTAS_LOG_WARN("coord") << "leader timed out sending stop to peer " << i;
            }
        } catch (const interprocess_exception& ex) {
            QUANTAS_LOG_WARN("coord")
                << "leader could not send stop to peer " << i << ": " << ex.what();
        }
    }
}

/*
 * Major operation: Block a peer until its control inbox receives the start
 * signal. Reject a message with the wrong size or trigger value.
 */
void ProcessCoordinatorMQ::waitForStart() {
    if (_isLeader)
        return;

    QUANTAS_LOG_INFO("coord") << "peer " << _myId << " waiting for start signal";
    try {
        std::vector<char> buffer(_myControlInbox->get_max_msg_size());
        unsigned int priority;
        message_queue::size_type recvd_size;

        _myControlInbox->receive(buffer.data(), buffer.size(), recvd_size, priority);

        if (recvd_size != sizeof(unsigned int))
            throw std::runtime_error("Unexpected start message at ::waitForStart for peer " +
                                     std::to_string(_myId));

        unsigned int trigger;
        std::memcpy(&trigger, buffer.data(), sizeof(trigger));
        if (trigger != kStartTrigger)
            throw std::runtime_error("Unexpected start message at ::waitForStart for peer " +
                                     std::to_string(_myId));

        QUANTAS_LOG_INFO("coord") << "peer " << _myId << " received start signal";
    } catch (const interprocess_exception& ex) {
        throw std::runtime_error("Failed to ::waitForStart for peer " + std::to_string(_myId) +
                                 ": " + ex.what());
    }
}

/* Major operation: Serialize and send one topology assignment to each peer's control inbox. Fail
 * before sending an assignment that is larger than the queue's configured message size. */
void ProcessCoordinatorMQ::sendAssignments(const std::vector<PeerAssignment>& assignments) {
    if (!_isLeader)
        return;

    for (const PeerAssignment& assignment : assignments) {
        const auto deadline = boost::posix_time::microsec_clock::universal_time() +
                              boost::posix_time::milliseconds(_queueConfig.controlSendTimeoutMs);
        std::stringstream ss;
        boost::archive::binary_oarchive oa(ss);
        oa << assignment;
        const std::string bytes = ss.str();
        const std::string queueName = peerControlQueueName(assignment.id);

        try {
            message_queue peerInbox(open_only, queueName.c_str());
            if (bytes.size() > peerInbox.get_max_msg_size()) {
                throw std::runtime_error("Assignment for peer " + std::to_string(assignment.id) +
                                         " exceeds queue max message size");
            }

            if (!peerInbox.timed_send(bytes.data(), bytes.size(), 0, deadline)) {
                throw std::runtime_error("Timed out sending assignment to peer " +
                                         std::to_string(assignment.id) + " after " +
                                         std::to_string(_queueConfig.controlSendTimeoutMs) + " ms");
            }
        } catch (const interprocess_exception& ex) {
            throw std::runtime_error(
                "Failed sending assignment to peer " + std::to_string(assignment.id) + " within " +
                std::to_string(_queueConfig.controlSendTimeoutMs) + " ms: " + ex.what());
        }
    }

    const std::string topologyType =
        assignments.empty() ? "unknown" : assignments.front().topologyType;
    QUANTAS_LOG_INFO("coord") << "leader sent assignments topology=" << topologyType
                              << " peers=" << assignments.size();
}

/*
 * Major operation: Wait for this peer's serialized topology assignment,
 * decode it, and return it as a one-item list for the peer startup code.
 */
std::vector<PeerAssignment> ProcessCoordinatorMQ::waitForAssignments() {
    if (_isLeader)
        return {};

    try {
        std::vector<char> buffer(_myControlInbox->get_max_msg_size());
        unsigned int priority;
        message_queue::size_type recvdSize;

        _myControlInbox->receive(buffer.data(), buffer.size(), recvdSize, priority);

        std::stringstream ss(std::string(buffer.data(), recvdSize));
        boost::archive::binary_iarchive ia(ss);

        PeerAssignment assignment;
        ia >> assignment;

        return {assignment};
    } catch (const interprocess_exception& ex) {
        throw std::runtime_error("Failed to ::waitForAssignments for peer " +
                                 std::to_string(_myId) + ": " + ex.what());
    }
}

/*
 * Major operation: Keep a completed peer alive until the leader sends stop.
 * Other control messages are ignored. After stop arrives, the peer waits for
 * the leader to remove its control queue before releasing its local handle.
 */
void ProcessCoordinatorMQ::waitForStop() {
    if (_isLeader)
        return;

    QUANTAS_LOG_INFO("coord") << "peer " << _myId << " waiting for stop signal";
    try {
        std::vector<char> buffer(_myControlInbox->get_max_msg_size());
        unsigned int priority;
        message_queue::size_type recvd_size;

        while (true) {
            _myControlInbox->receive(buffer.data(), buffer.size(), recvd_size, priority);
            if (recvd_size != sizeof(unsigned int)) {
                continue;
            }

            unsigned int trigger = 0;
            std::memcpy(&trigger, buffer.data(), sizeof(trigger));
            if (trigger != kStopTrigger) {
                continue;
            }

            QUANTAS_LOG_INFO("coord") << "peer " << _myId << " received stop signal";
            _stopSignal = true;
            waitForQueueRemoval(peerControlQueueName(_myId));
            _myControlInbox.reset();
            break;
        }
    } catch (const interprocess_exception& ex) {
        throw std::runtime_error("Failed to ::waitForStop for peer " + std::to_string(_myId) +
                                 ": " + ex.what());
    }
}

/*
 * Major operation: Remove QUANTAS queue names after a test. The leader removes
 * all shared and peer queues; a peer may remove only its own queues. Completion
 * tracking is also reset so it cannot leak into the next test.
 */
void ProcessCoordinatorMQ::cleanUp() {
    if (_isLeader) { // only leader should remove mq_barrier and all peer queues
        message_queue::remove("mq_barrier");
        message_queue::remove("mq_done");
        for (size_t i = 0; i < _totalPeers; ++i) {
            const auto peerId = static_cast<interfaceId>(i);
            message_queue::remove(peerControlQueueName(peerId).c_str());
            message_queue::remove(peerDataQueueName(peerId).c_str());
        }
    } else { // a follower should only remove its own
        message_queue::remove(peerControlQueueName(_myId).c_str());
        message_queue::remove(peerDataQueueName(_myId).c_str());
    }
    {
        std::scoped_lock lock(gCompletedMutex);
        gCompletedPeers.clear();
    }
}

/*
 * Utility: Report whether this process has been asked to stop its current
 * work.
 */
bool ProcessCoordinatorMQ::shouldStop() const { return _stopSignal.load(); }

/*
 * Utility: Return the stop policy selected for the current experiment.
 */
StopMode ProcessCoordinatorMQ::stopMode() const { return _stopMode; }

/*
 * Major operation: Mark this process as stopping. The atomic exchange makes
 * repeated requests harmless, and the optional reason is written to the log.
 */
void ProcessCoordinatorMQ::requestStop(const std::string& reason) {
    const bool alreadyStopping = _stopSignal.exchange(true);
    if (alreadyStopping)
        return;

    if (!reason.empty())
        QUANTAS_LOG_INFO("coord") << "peer " << _myId << " stop requested: " << reason;
    else
        QUANTAS_LOG_INFO("coord") << "peer " << _myId << " stop requested";
}

/*
 * Major operation: Handle peer completion on either side of the protocol. A
 * peer sends its ID to `mq_done`. The leader records unique completed IDs and
 * broadcasts the normal stop signal after every expected peer is complete.
 */
void ProcessCoordinatorMQ::notifyPeerFinished(interfaceId id, bool succeeded) {
    if (id == NO_PEER_ID)
        return;

    // Follower path: send this peer's completion status to the leader-owned queue.
    if (!_isLeader) {
        const PeerCompletionMessage completion{id, succeeded};
        for (int attempt = 1; attempt <= kControlSendAttempts; ++attempt) {
            try {
                message_queue doneQueue(open_only, "mq_done");
                if (timedSendControlMessage(doneQueue, &completion, sizeof(completion),
                                            kControlSendWaitMs)) {
                    QUANTAS_LOG_INFO("coord")
                        << "peer " << _myId << " reported " << (succeeded ? "success" : "failure")
                        << " for peer " << id;

                    return;
                }
            } catch (const interprocess_exception& ex) {
                if (attempt == kControlSendAttempts) {
                    throw std::runtime_error("Failed to ::notifyPeerStopped for peer " +
                                             std::to_string(_myId) + ": " + ex.what());
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kControlSendWaitMs));
        }

        throw std::runtime_error("Timed out sending completion status for peer " +
                                 std::to_string(_myId) + " to mq_done");
    }

    // Leader path: count unique completed peers and broadcast stop once all are
    // done.
    bool shouldBroadcast = false;
    {
        std::scoped_lock lock(gCompletedMutex);
        gCompletedPeers.insert(id);
        shouldBroadcast = gCompletedPeers.size() >= _totalPeers;
    }

    QUANTAS_LOG_INFO("coord") << "leader recorded done from peer " << id;

    if (shouldBroadcast)
        broadcastStop();
}

void ProcessCoordinatorMQ::notifyPeerStopped(interfaceId id) { notifyPeerFinished(id, true); }

void ProcessCoordinatorMQ::notifyPeerFailed(interfaceId id) { notifyPeerFinished(id, false); }

/*
 * Major operation: Wait until every expected peer reports done or one reports
 * failure. Invalid and duplicate IDs are ignored. The returned value lists the
 * completed and failed peers and says whether the wait timed out.
 */
PeerCompletionResult ProcessCoordinatorMQ::waitForAllDone(std::chrono::milliseconds timeout) {
    PeerCompletionResult result;

    if (!_isLeader)
        return result;

    std::unordered_set<interfaceId> seenPeers;

    QUANTAS_LOG_INFO("coord") << "leader waiting up to " << timeout.count() << " ms for "
                              << _totalPeers << " done messages";

    try {
        message_queue doneQueue(open_only, "mq_done");
        const auto deadline = boost::posix_time::microsec_clock::universal_time() +
                              boost::posix_time::milliseconds(timeout.count());

        while (result.completedPeers.size() + result.failedPeers.size() < _totalPeers) {
            PeerCompletionMessage completion;
            unsigned int priority = 0;
            message_queue::size_type recvd_size = 0;

            if (!doneQueue.timed_receive(&completion, sizeof(completion), recvd_size, priority,
                                         deadline)) {
                result.timedOut = true;
                QUANTAS_LOG_WARN("coord")
                    << "leader completion wait timed out after receiving "
                    << result.completedPeers.size() << " of " << _totalPeers << " peers";
                break;
            }

            if (recvd_size != sizeof(completion)) {
                throw std::runtime_error("Unexpected done message size at ::waitForAllDone for "
                                         "leader " +
                                         std::to_string(_myId));
            }

            if (completion.peerId < 0 || static_cast<size_t>(completion.peerId) >= _totalPeers) {
                QUANTAS_LOG_WARN("coord")
                    << "leader ignored invalid completion peer id " << completion.peerId;
                continue;
            }

            /* avoid reporting duplicate peer IDs if something goes wrong and a
             * peer sends more than one completion message. */
            if (seenPeers.insert(completion.peerId).second) {
                if (completion.succeeded) {
                    result.completedPeers.push_back(completion.peerId);
                    notifyPeerStopped(completion.peerId);
                } else {
                    result.failedPeers.push_back(completion.peerId);
                    QUANTAS_LOG_ERROR("coord")
                        << "leader recorded failure from peer " << completion.peerId;
                    break;
                }
            } else {
                QUANTAS_LOG_WARN("coord")
                    << "leader ignored duplicate completion from peer " << completion.peerId;
            }
        }
    } catch (const interprocess_exception& ex) {
        throw std::runtime_error("Failed to ::waitForAllDone for leader " + std::to_string(_myId) +
                                 ": " + ex.what());
    }

    return result;
}

/*
 * Utility: Destroy this process's coordinator object. Queue removal is handled
 * explicitly by `cleanUp()`, so the destructor has no extra work.
 */
ProcessCoordinatorMQ::~ProcessCoordinatorMQ() {}

} // namespace quantas
