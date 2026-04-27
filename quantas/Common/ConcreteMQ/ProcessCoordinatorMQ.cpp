#include "ProcessCoordinatorMQ.hpp"
#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/exceptions.hpp>
#include <boost/interprocess/interprocess_fwd.hpp>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string>

/* TODO:
    The TCP coordinator's stop mechanism is quite specificm peers send a "done" signal to the
    leader, the leader counts how many have finished, then broadcasts "stop" to all.

    How will it work in the MQ version?

    - for example, whether peers signal the logger directly, whether the logger decides when to stop
    based on a timer or peer count, or something else entirely.
    - Since the logger is the leader in our design the stop logic ties directly into the logger's
    responsibility.

    No stop/done signal. The TCP coordinator has `notifyPeerStopped()` / `waitForStop()` /
    `broadcastStop()` — peers signal when they finish so the simulation knows when to shut down. The
    MQ version has no equivalent yet */

using namespace boost::interprocess;

namespace quantas {

ProcessCoordinatorMQ &ProcessCoordinatorMQ::instance() {
    static ProcessCoordinatorMQ coordinator;
    return coordinator;
}

void ProcessCoordinatorMQ::configureProcess(bool isLeader, size_t totalPeers, interfaceId myId) {
    _isLeader = isLeader;
    _totalPeers = totalPeers;
    _myId = myId;
}

// 1. Leader creates quantas_barrier queue first
void ProcessCoordinatorMQ::createBarrier() {
    if (!_isLeader)
        return;

    message_queue::remove("mq_barrier");

    /* WARNING: capacity 10 is capped by the POSIX limit fs.mqueue.msg_max
    (default 10 on Linux). The barrier needs to hold one "ready" message per
    follower, so N > 10 peers will require `sudo sysctl
    fs.mqueue.msg_max=<higher>` before this queue can be created with a
    larger size */
    try {
        _myBarrier.emplace(create_only, "mq_barrier", 10, sizeof(unsigned int));
    } catch (const interprocess_exception &ex) {
        throw std::runtime_error(std::string("Failed to ::createBarrier queue: ") + ex.what());
    }
}

// 2. Every follower creates their own inbox
void ProcessCoordinatorMQ::createInbox() {
    if (_isLeader)
        return;

    std::string queueName = "peer_" + std::to_string(_myId);
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
    if (_isLeader)
        return;

    try {
        message_queue mq(open_only, "mq_barrier");
        unsigned int trigger = 1;
        mq.send(&trigger, sizeof(trigger), 0);
    } catch (const interprocess_exception &ex) {
        throw std::runtime_error(
            "Failed to ::sendReady queue for peer " + std::to_string(_myId) + ": " + ex.what()
        );
    }
}

// 4. Leader reads N "ready" messages from quantas_barrier
void ProcessCoordinatorMQ::waitForAllReady() {
    if (!_isLeader)
        return;

    try {
        for (size_t i = 0; i < _totalPeers; ++i) {
            unsigned int priority;
            unsigned int trigger;
            message_queue::size_type recvd_size;

            _myBarrier->receive(&trigger, sizeof(trigger), recvd_size, priority);
            if (recvd_size != sizeof(trigger) || trigger != 1)
                throw std::runtime_error(
                    "Unexpected ready message (trigger=" + std::to_string(trigger) +
                    ", size=" + std::to_string(recvd_size) + ") at ::waitForAllReady for peer " +
                    std::to_string(_myId)
                );
        }
    } catch (const interprocess_exception &ex) {
        throw std::runtime_error(
            "Failed to ::waitForAllReady for peer " + std::to_string(_myId) + ": " + ex.what()
        );
    }
}

// 5. Leader sends "start" into each peer's inbox
void ProcessCoordinatorMQ::broadcastStart() {
    if (!_isLeader)
        return;

    try {
        for (size_t i = 0; i < _totalPeers; ++i) {
            std::string queueName = "peer_" + std::to_string(i);
            message_queue mq(open_only, queueName.c_str());

            unsigned int trigger = 1;
            mq.send(&trigger, sizeof(trigger), 0);
        }
    } catch (const interprocess_exception &ex) {
        throw std::runtime_error(
            "Failed to ::broadCastStart for peer " + std::to_string(_myId) + ": " + ex.what()
        );
    }
}

// 6. Every follower reads "start" from their inbox → begin
void ProcessCoordinatorMQ::waitForStart() {
    if (_isLeader)
        return;

    try {
        unsigned int priority;
        char buffer[MAX_MSG_SIZE];
        message_queue::size_type recvd_size;

        _myInbox->receive(buffer, MAX_MSG_SIZE, recvd_size, priority);

        unsigned int trigger;
        std::memcpy(&trigger, buffer, sizeof(trigger));

        if (recvd_size != sizeof(trigger) || trigger != 1)
            throw std::runtime_error(
                "Unexpected start message at ::waitForStart for peer " + std::to_string(_myId)
            );
    } catch (const interprocess_exception &ex) {
        throw std::runtime_error(
            "Failed to ::waitForStart for peer " + std::to_string(_myId) + ": " + ex.what()
        );
    }
}

// Remove all the names queues from the OS so they dont presist after the simulation ends
void ProcessCoordinatorMQ::cleanUp() {
    if (_isLeader) { // only leader should remove mq_barrier and all peer queues
        message_queue::remove("mq_barrier");
        for (size_t i = 0; i < _totalPeers; ++i) {
            std::string queueName = "peer_" + std::to_string(i);
            message_queue::remove(queueName.c_str());
        }
    } else { // a follower should only remove its own
        std::string queueName = "peer_" + std::to_string(_myId);
        message_queue::remove(queueName.c_str());
    }
}

ProcessCoordinatorMQ::~ProcessCoordinatorMQ() {}

} // namespace quantas
