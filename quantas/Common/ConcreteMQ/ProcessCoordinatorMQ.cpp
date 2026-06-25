#include "ProcessCoordinatorMQ.hpp"
#include "../Logger.hpp"
#include "MqAssignment.hpp"
#include <atomic>
#include <boost/archive/binary_iarchive.hpp>
#include <boost/archive/binary_oarchive.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>
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
 * TODO::
 *
 * The TCP coordinator's stop mechanism is quite specificm peers send a "done"
 signal to the leader, the leader counts how many have finished, then
 broadcasts "stop" to all.

 * How will it work in the MQ version?

 * for example, whether peers signal the logger directly, whether the logger
 decides when to stop based on a timer or peer count, or something else
 entirely.

 * Since the logger is the leader in our design the stop logic ties directly
 into the logger's responsibility.

 * No stop/done signal. The TCP coordinator has `notifyPeerStopped()` /
 `waitForStop()` / `broadcastStop()` — peers signal when they finish so the
 simulation knows when to shut down. The MQ version has no equivalent yet */

using namespace boost::interprocess;

namespace quantas {
namespace {
constexpr unsigned int kReadyTrigger = 1;
constexpr unsigned int kStartTrigger = 2;
constexpr unsigned int kStopTrigger = 3;
constexpr unsigned int kDoneTrigger = 4;
constexpr int kControlSendAttempts = 200;
constexpr int kControlSendWaitMs = 10;
std::mutex gCompletedMutex;
std::unordered_set<interfaceId> gCompletedPeers;

bool timedSendControlMessage(
    message_queue &mq, const void *message, size_t messageSize, int waitMs
) {
    const auto deadline = boost::posix_time::microsec_clock::universal_time() +
                          boost::posix_time::milliseconds(waitMs);
    return mq.timed_send(message, messageSize, 0, deadline);
}

bool queueExists(const std::string &queueName) {
    try {
        message_queue mq(open_only, queueName.c_str());
        return true;
    } catch (const interprocess_exception &) { return false; }
}

void waitForQueueRemoval(const std::string &queueName) {
    for (int attempt = 1; attempt <= kControlSendAttempts; ++attempt) {
        if (!queueExists(queueName)) { return; }
        std::this_thread::sleep_for(std::chrono::milliseconds(kControlSendWaitMs));
    }

    throw std::runtime_error("Timed out waiting for queue removal: " + queueName);
}
} // namespace

ProcessCoordinatorMQ &ProcessCoordinatorMQ::instance() {
    static ProcessCoordinatorMQ coordinator;
    return coordinator;
}

void ProcessCoordinatorMQ::configureExperiment(
    size_t experimentIndex, const std::string &peerType, bool isLeader, size_t totalPeers,
    interfaceId myId, const std::string &logFileBase, StopMode stopMode
) {
    // J2 skeleton: persist experiment-scoped coordinator context.
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
    // Reset transient MQ handles for a new experiment lifecycle.
    _myBarrier.reset();
    _myInbox.reset();
}

void ProcessCoordinatorMQ::configureProcess(bool isLeader, size_t totalPeers, interfaceId myId) {
    configureExperiment(0, "", isLeader, totalPeers, myId, "", StopMode::FixedRounds);
}

// 1. Leader creates quantas_barrier queue first
void ProcessCoordinatorMQ::createBarrier() {
    if (!_isLeader) return;

    QUANTAS_LOG_INFO("coord") << "creating barrier queue mq_barrier";
    message_queue::remove("mq_barrier");
    message_queue::remove("mq_done");

    // Keep shared control queues small and rely on timed sender retries while the leader drains.
    try {
        _myBarrier.emplace(create_only, "mq_barrier", 10, sizeof(unsigned int));
        message_queue doneQueue(create_only, "mq_done", 10, sizeof(interfaceId));
    } catch (const interprocess_exception &ex) {
        throw std::runtime_error(std::string("Failed to ::createBarrier queue: ") + ex.what());
    }
}

// 2. Every follower creates their own inbox
void ProcessCoordinatorMQ::createInbox() {
    if (_isLeader) return;

    std::string queueName = "peer_" + std::to_string(_myId);
    QUANTAS_LOG_INFO("coord") << "peer " << _myId << " creating inbox " << queueName;
    message_queue::remove(queueName.c_str());

    try {
        _myInbox.emplace(create_only, queueName.c_str(), 10, MAX_MSG_SIZE);
    } catch (const interprocess_exception &ex) {
        throw std::runtime_error(
            "Failed to ::createInbox queue for peer " + std::to_string(_myId) + ": " + ex.what()
        );
    }
}

// 3. Every follower sends "ready" into quantas_barrier
void ProcessCoordinatorMQ::sendReady() {
    if (_isLeader) return;

    for (int attempt = 1; attempt <= kControlSendAttempts; ++attempt) {
        try {
            message_queue mq(open_only, "mq_barrier");
            unsigned int trigger = kReadyTrigger;
            if (timedSendControlMessage(mq, &trigger, sizeof(trigger), kControlSendWaitMs)) {
                QUANTAS_LOG_INFO("coord") << "peer " << _myId << " sent ready to barrier";
                return;
            }
        } catch (const interprocess_exception &ex) {
            if (attempt == kControlSendAttempts) {
                throw std::runtime_error(
                    "Failed to ::sendReady queue for peer " + std::to_string(_myId) + ": " +
                    ex.what()
                );
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(kControlSendWaitMs));
    }

    throw std::runtime_error(
        "Timed out sending ready for peer " + std::to_string(_myId) + " to mq_barrier"
    );
}

// 4. Leader reads N "ready" messages from quantas_barrier
void ProcessCoordinatorMQ::waitForAllReady() {
    if (!_isLeader) return;

    QUANTAS_LOG_INFO("coord") << "leader waiting for " << _totalPeers << " ready messages";
    try {
        for (size_t i = 0; i < _totalPeers; ++i) {
            unsigned int priority;
            unsigned int trigger;
            message_queue::size_type recvd_size;

            _myBarrier->receive(&trigger, sizeof(trigger), recvd_size, priority);
            if (recvd_size != sizeof(trigger) || trigger != kReadyTrigger)
                throw std::runtime_error(
                    "Unexpected ready message (trigger=" + std::to_string(trigger) +
                    ", size=" + std::to_string(recvd_size) + ") at ::waitForAllReady for peer " +
                    std::to_string(_myId)
                );
        }
        QUANTAS_LOG_INFO("coord") << "leader received all ready messages";
    } catch (const interprocess_exception &ex) {
        throw std::runtime_error(
            "Failed to ::waitForAllReady for peer " + std::to_string(_myId) + ": " + ex.what()
        );
    }
}

// 5. Leader sends "start" into each peer's inbox
void ProcessCoordinatorMQ::broadcastStart() {
    if (!_isLeader) return;

    QUANTAS_LOG_INFO("coord") << "leader broadcasting start to " << _totalPeers << " peers";
    try {
        for (size_t i = 0; i < _totalPeers; ++i) {
            std::string queueName = "peer_" + std::to_string(i);
            message_queue mq(open_only, queueName.c_str());

            unsigned int trigger = kStartTrigger;
            mq.send(&trigger, sizeof(trigger), 0);
        }
    } catch (const interprocess_exception &ex) {
        throw std::runtime_error(
            "Failed to ::broadCastStart for peer " + std::to_string(_myId) + ": " + ex.what()
        );
    }
}

void ProcessCoordinatorMQ::broadcastStop() {
    if (!_isLeader) return;

    QUANTAS_LOG_INFO("coord") << "leader broadcasting stop to " << _totalPeers << " peers";
    try {
        for (size_t i = 0; i < _totalPeers; ++i) {
            std::string queueName = "peer_" + std::to_string(i);
            message_queue mq(open_only, queueName.c_str());

            unsigned int trigger = kStopTrigger;
            mq.send(&trigger, sizeof(trigger), 0);
        }
    } catch (const interprocess_exception &ex) {
        throw std::runtime_error(
            "Failed to ::broadCastStart for peer " + std::to_string(_myId) + ": " + ex.what()
        );
    }
}

void ProcessCoordinatorMQ::broadcastStopBestEffort() {
    if (!_isLeader) return;

    QUANTAS_LOG_WARN("coord") << "leader best-effort broadcasting stop to " << _totalPeers
                              << " peers";
    for (size_t i = 0; i < _totalPeers; ++i) {
        const std::string queueName = "peer_" + std::to_string(i);
        try {
            message_queue mq(open_only, queueName.c_str());
            const unsigned int trigger = kStopTrigger;
            const auto deadline = boost::posix_time::microsec_clock::universal_time() +
                                  boost::posix_time::milliseconds(100);
            if (!mq.timed_send(&trigger, sizeof(trigger), 0, deadline)) {
                QUANTAS_LOG_WARN("coord") << "leader timed out sending stop to peer " << i;
            }
        } catch (const interprocess_exception &ex) {
            QUANTAS_LOG_WARN("coord")
                << "leader could not send stop to peer " << i << ": " << ex.what();
        }
    }
}

// 6. Every follower reads "start" from their inbox → begin
void ProcessCoordinatorMQ::waitForStart() {
    if (_isLeader) return;

    QUANTAS_LOG_INFO("coord") << "peer " << _myId << " waiting for start signal";
    try {
        unsigned int priority;
        char buffer[MAX_MSG_SIZE];
        message_queue::size_type recvd_size;

        _myInbox->receive(buffer, MAX_MSG_SIZE, recvd_size, priority);

        unsigned int trigger;
        std::memcpy(&trigger, buffer, sizeof(trigger));

        if (recvd_size != sizeof(trigger) || trigger != kStartTrigger)
            throw std::runtime_error(
                "Unexpected start message at ::waitForStart for peer " + std::to_string(_myId)
            );
        QUANTAS_LOG_INFO("coord") << "peer " << _myId << " received start signal";
    } catch (const interprocess_exception &ex) {
        throw std::runtime_error(
            "Failed to ::waitForStart for peer " + std::to_string(_myId) + ": " + ex.what()
        );
    }
}

void ProcessCoordinatorMQ::sendAssignments(const std::vector<MqAssignment> &assignments) {
    if (!_isLeader) return;

    try {
        for (const MqAssignment &assignment : assignments) {
            std::stringstream ss;
            boost::archive::binary_oarchive oa(ss);
            oa << assignment;

            const std::string bytes = ss.str();

            std::string queueName = "peer_" + std::to_string(assignment.id);
            message_queue peerInbox(open_only, queueName.c_str());

            if (bytes.size() > peerInbox.get_max_msg_size()) {
                throw std::runtime_error(
                    "Assignment for peer " + std::to_string(assignment.id) +
                    " exceeds queue max message size"
                );
            }

            peerInbox.send(bytes.data(), bytes.size(), 0);
        }
        const std::string topologyType = assignments.empty() ? "unknown"
                                                             : assignments.front().topologyType;
        QUANTAS_LOG_INFO("coord") << "leader sent assignments topology=" << topologyType
                                  << " peers=" << assignments.size();
    } catch (const interprocess_exception &ex) {
        throw std::runtime_error(std::string("Failed to ::sendAssignments: ") + ex.what());
    }
}

std::vector<MqAssignment> ProcessCoordinatorMQ::waitForAssignments() {
    if (_isLeader) return {};

    try {
        std::vector<char> buffer(_myInbox->get_max_msg_size());
        unsigned int priority;
        message_queue::size_type recvdSize;

        _myInbox->receive(buffer.data(), buffer.size(), recvdSize, priority);

        std::stringstream ss(std::string(buffer.data(), recvdSize));
        boost::archive::binary_iarchive ia(ss);

        MqAssignment assignment;
        ia >> assignment;

        return {assignment};
    } catch (const interprocess_exception &ex) {
        throw std::runtime_error(
            "Failed to ::waitForAssignments for peer " + std::to_string(_myId) + ": " + ex.what()
        );
    }
}

void ProcessCoordinatorMQ::waitForStop() {
    if (_isLeader) return;

    QUANTAS_LOG_INFO("coord") << "peer " << _myId << " waiting for stop signal";
    try {
        unsigned int priority;
        char buffer[MAX_MSG_SIZE];
        message_queue::size_type recvd_size;

        while (true) {
            _myInbox->receive(buffer, MAX_MSG_SIZE, recvd_size, priority);
            if (recvd_size != sizeof(unsigned int)) { continue; }

            unsigned int trigger = 0;
            std::memcpy(&trigger, buffer, sizeof(trigger));
            if (trigger != kStopTrigger) { continue; }

            QUANTAS_LOG_INFO("coord") << "peer " << _myId << " received stop signal";
            _stopSignal = true;
            waitForQueueRemoval("peer_" + std::to_string(_myId));
            _myInbox.reset();
            break;
        }
    } catch (const interprocess_exception &ex) {
        throw std::runtime_error(
            "Failed to ::waitForStop for peer " + std::to_string(_myId) + ": " + ex.what()
        );
    }
}

// Remove all the names queues from the OS so they dont presist after the
// simulation ends
void ProcessCoordinatorMQ::cleanUp() {
    if (_isLeader) { // only leader should remove mq_barrier and all peer queues
        message_queue::remove("mq_barrier");
        message_queue::remove("mq_done");
        for (size_t i = 0; i < _totalPeers; ++i) {
            std::string queueName = "peer_" + std::to_string(i);
            message_queue::remove(queueName.c_str());
        }
    } else { // a follower should only remove its own
        std::string queueName = "peer_" + std::to_string(_myId);
        message_queue::remove(queueName.c_str());
    }
    {
        std::scoped_lock lock(gCompletedMutex);
        gCompletedPeers.clear();
    }
}

bool ProcessCoordinatorMQ::shouldStop() const { return _stopSignal.load(); }

StopMode ProcessCoordinatorMQ::stopMode() const { return _stopMode; }

void ProcessCoordinatorMQ::requestStop(const std::string &reason) {
    const bool alreadyStopping = _stopSignal.exchange(true);
    if (alreadyStopping) return;

    if (!reason.empty())
        QUANTAS_LOG_INFO("coord") << "peer " << _myId << " stop requested: " << reason;
    else QUANTAS_LOG_INFO("coord") << "peer " << _myId << " stop requested";
}

void ProcessCoordinatorMQ::notifyPeerStopped(interfaceId id) {
    if (id == NO_PEER_ID) return;

    // Follower path: send a single done notification to the leader-owned queue.
    if (!_isLeader) {
        for (int attempt = 1; attempt <= kControlSendAttempts; ++attempt) {
            try {
                message_queue doneQueue(open_only, "mq_done");
                if (timedSendControlMessage(doneQueue, &id, sizeof(id), kControlSendWaitMs)) {
                    QUANTAS_LOG_INFO("coord")
                        << "peer " << _myId << " notified done for peer " << id;
                    return;
                }
            } catch (const interprocess_exception &ex) {
                if (attempt == kControlSendAttempts) {
                    throw std::runtime_error(
                        "Failed to ::notifyPeerStopped for peer " + std::to_string(_myId) + ": " +
                        ex.what()
                    );
                }
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(kControlSendWaitMs));
        }

        throw std::runtime_error(
            "Timed out sending done for peer " + std::to_string(_myId) + " to mq_done"
        );
    }

    // Leader path: count unique completed peers and broadcast stop once all are done.
    bool shouldBroadcast = false;
    {
        std::scoped_lock lock(gCompletedMutex);
        gCompletedPeers.insert(id);
        shouldBroadcast = gCompletedPeers.size() >= _totalPeers;
    }

    QUANTAS_LOG_INFO("coord") << "leader recorded done from peer " << id;
    if (shouldBroadcast) { broadcastStop(); }
}

PeerCompletionResult ProcessCoordinatorMQ::waitForAllDone(std::chrono::milliseconds timeout) {
    PeerCompletionResult result;
    if (!_isLeader) return result;

    std::unordered_set<interfaceId> seenPeers;

    QUANTAS_LOG_INFO("coord") << "leader waiting up to " << timeout.count() << " ms for "
                              << _totalPeers << " done messages";

    try {
        message_queue doneQueue(open_only, "mq_done");
        const auto deadline = boost::posix_time::microsec_clock::universal_time() +
                              boost::posix_time::milliseconds(timeout.count());

        while (result.completedPeers.size() < _totalPeers) {
            interfaceId doneId = NO_PEER_ID;
            unsigned int priority = 0;
            message_queue::size_type recvd_size = 0;

            if (!doneQueue.timed_receive(&doneId, sizeof(doneId), recvd_size, priority, deadline)) {
                result.timedOut = true;
                QUANTAS_LOG_WARN("coord")
                    << "leader completion wait timed out after receiving "
                    << result.completedPeers.size() << " of " << _totalPeers << " peers";
                break;
            }

            if (recvd_size != sizeof(doneId)) {
                throw std::runtime_error(
                    "Unexpected done message size at ::waitForAllDone for leader " +
                    std::to_string(_myId)
                );
            }

            if (doneId < 0 || static_cast<size_t>(doneId) >= _totalPeers) {
                QUANTAS_LOG_WARN("coord") << "leader ignored invalid done peer id " << doneId;
                continue;
            }

            /* avoid reporting duplicate peer IDs if something goes wrong and a peer sends more than
             * one done message. */
            if (seenPeers.insert(doneId).second) {
                result.completedPeers.push_back(doneId);
                notifyPeerStopped(doneId);
            } else {
                QUANTAS_LOG_WARN("coord") << "leader ignored duplicate done from peer " << doneId;
            }
        }
    } catch (const interprocess_exception &ex) {
        throw std::runtime_error(
            "Failed to ::waitForAllDone for leader " + std::to_string(_myId) + ": " + ex.what()
        );
    }

    return result;
}

ProcessCoordinatorMQ::~ProcessCoordinatorMQ() {}

} // namespace quantas
