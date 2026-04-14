#include "Channel.hpp"


namespace quantas {

Channel::Channel(interfaceId targetId, interfaceId targetInternalId,
                        interfaceId sourceId, interfaceId sourceInternalId,
                        const nlohmann::json &channelParams)
: _targetId(targetId),
  _targetInternalId(targetInternalId),
  _sourceId(sourceId),
  _sourceInternalId(sourceInternalId)
{
    setParameters(channelParams);
}

Channel::~Channel() {
    while (!_packetQueue.empty()) {
        _packetQueue.pop_front();
    }
};

void Channel::setParameters(const nlohmann::json &params) {
    _properties = ChannelPropertiesFactory::instance().create(params);
    _throughputLeft = _properties->getMaxMsgsRec()*(RoundManager::lastRound()-RoundManager::currentRound());
}

int Channel::computeRandomDelay() const {
    int delay = 1;
    switch (_properties->getDelayStyle()) {
    case DelayStyle::DS_UNIFORM:
        delay = uniformInt(_properties->getMinDelay(), _properties->getMaxDelay());
        break;
    case DelayStyle::DS_POISSON:
        delay = poissonInt(_properties->getAvgDelay());
        delay = std::clamp(delay, _properties->getMinDelay(), _properties->getMaxDelay());
        break;
    case DelayStyle::DS_ONE:
        delay = 1;
        break;
    }
    return delay;
}

/*  In a real network, these three problems happen INDEPENDENTLY of each other.

    A packet can:
    - survive (not get lost) AND arrive late (delayed)
    - survive (not get lost) AND arrive twice (duplicated)
    - survive (not get lost) AND arrive late AND arrive twice

    They are separate network problems that can all happen at the same time. A packet not getting
    lost doesn't mean it won't be delayed or duplicated.

    QUANTAS simulates all three independently so researchers can test their algorithms
    against realistic network conditions — where multiple things can go wrong at the same time,
    pushPacket() is the tool that QUANTAS uses to simulates these problems */

void Channel::pushPacket(Packet pkt) {
    /* 1. DROP CHECK: It uses probability param from the config and check the percentage
     * of the packet survival when it travels from A to B */
    if (trueWithProbability(_properties->getDropProbability())) {
        return;
    }

    // If we get past the drop check...
    bool duplicate = false;
    do {
        duplicate = false;
        if (!canSend()) {
            return;
        }

        // 2. DELAY: stmap how many rounds to wait
        consumeThroughput();
        int d = computeRandomDelay();
        pkt.setDelay(d, d);
        _packetQueue.push_back(pkt); // packet enters queue

        // 3. DUPLICATE CHECK: happens last
        duplicate = trueWithProbability(_properties->getDuplicateProbability());
        if (duplicate) pkt.setMessage(pkt.getMessage());

    } while (duplicate); // if duplicate loop again -> second copy
}

void Channel::shuffleChannel() {
    // reorder
    if (_packetQueue.size() > 1 && trueWithProbability(_properties->getReorderProbability())) {
        std::shuffle(_packetQueue.begin(), _packetQueue.end(), threadLocalEngine());
    }
}

Packet Channel::popPacket() {
    Packet p = std::move(_packetQueue.front());
    _packetQueue.pop_front();
    return p;
}

} // end namespace quantas
