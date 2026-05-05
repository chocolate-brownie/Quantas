CXX      := g++
CXXFLAGS := -std=c++17 -pthread -O0 -g
LIBS     := -lboost_serialization -lrt

COMMON_SRCS := ../ProcessCoordinatorMQ.cpp ../NetworkInterfaceConcreteMQ.cpp
ALTBIT_SRCS := ../../../AltBitPeer/AltBitPeer.cpp
ABSTRACT_SRCS := ../../Abstract/Channel.cpp
TCP_CONCRETE_SRCS := ../../Concrete/NetworkInterfaceConcrete.cpp \
	../../Concrete/ProcessCoordinator.cpp \
	../../Concrete/ipUtil.cpp
LOGGING_SRCS := ../../Logger.cpp ../../LoggingSupport.cpp
MQ_INPUT ?= ../../../AltBitPeer/AltBitUtility.json
MQ_ROUNDS ?= 10

.PHONY: all run run_mq_peer_runner clean

all: leader peer0 peer1 mq_peer_runner

leader: leader.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -o $@

peer0: peer0.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -o $@

peer1: peer1.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -o $@

mq_peer_runner: mq_peer_runner.cpp $(COMMON_SRCS) $(ALTBIT_SRCS) $(ABSTRACT_SRCS) $(TCP_CONCRETE_SRCS) $(LOGGING_SRCS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -o $@

# Clean stale queues from prior runs, start peers in background, leader in foreground
run: all
	@rm -f /dev/shm/mq_barrier /dev/shm/peer_0 /dev/shm/peer_1
	@(./peer0 & ./peer1 & ./leader; wait)

run_mq_peer_runner: leader mq_peer_runner
	@rm -f /dev/shm/mq_barrier /dev/shm/peer_0 /dev/shm/peer_1
	@(./leader & sleep 0.2; ./mq_peer_runner $(MQ_INPUT) 0 $(MQ_ROUNDS) & ./mq_peer_runner $(MQ_INPUT) 1 $(MQ_ROUNDS) & wait)

clean:
	@rm -f leader peer0 peer1 mq_peer_runner
