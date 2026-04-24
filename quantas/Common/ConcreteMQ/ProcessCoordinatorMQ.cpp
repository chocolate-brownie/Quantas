#include "ProcessCoordinatorMQ.hpp"
#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/exceptions.hpp>
#include <boost/interprocess/interprocess_fwd.hpp>
#include <stdexcept>

using namespace boost::interprocess;

namespace quantas {
namespace {} // namespace

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
    if (!_isLeader) {
        message_queue::remove("mq_barrier");
        return;
    }

    message_queue::remove("mq_barrier");

    /* WARNING: capacity 10 is capped by the POSIX limit fs.mqueue.msg_max
    (default 10 on Linux). The barrier needs to hold one "ready" message per
    follower, so N > 10 peers will require `sudo sysctl
    fs.mqueue.msg_max=<higher>` before this queue can be created with a
    larger size */
    try {
        message_queue mq_barrier(create_only, "mq_barrier", 10, 1024);
    } catch (const interprocess_exception &ex) {
        throw std::runtime_error(std::string("Failed to create barrier queue: ") + ex.what());
    }
}

// 2. Every follower creates their own inbox
void ProcessCoordinatorMQ::createInbox() {
    if (_isLeader)
        return;

    std::string queueName = "peer_" + std::to_string(_myId);
    message_queue::remove(queueName.c_str());

    try {
        _myInbox.emplace(create_only, queueName.c_str(), 10, 1024);
    } catch (const interprocess_exception &ex) {
        throw std::runtime_error(
            "Failed to create inbox queue for peer " + std::to_string(_myId) + ": " + ex.what()
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
            "Failed to sendReady queue for peer " + std::to_string(_myId) + ": " + ex.what()
        );
    }
}

// 4. Leader reads N "ready" messages from quantas_barrier
void ProcessCoordinatorMQ::waitForAllReady() {
    if (!_isLeader)
        return;

    try {
    } catch (const interprocess_exception &ex) {
    }
}

// void waitForAllReady();
// void broadcastStart();
// void waitForStart();
// void cleanUp();

} // namespace quantas
