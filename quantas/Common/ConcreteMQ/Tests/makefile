CXX      := g++
CXXFLAGS := -std=c++17 -pthread -O0 -g
LIBS     := -lboost_serialization -lrt

COMMON_SRCS := ../ProcessCoordinatorMQ.cpp ../NetworkInterfaceConcreteMQ.cpp

.PHONY: all run clean

all: leader peer0 peer1

leader: leader.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -o $@

peer0: peer0.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -o $@

peer1: peer1.cpp $(COMMON_SRCS)
	$(CXX) $(CXXFLAGS) $^ $(LIBS) -o $@

# Clean stale queues from prior runs, start peers in background, leader in foreground
run: all
	@rm -f /dev/shm/mq_barrier /dev/shm/peer_0 /dev/shm/peer_1
	@(./peer0 & ./peer1 & ./leader; wait)

clean:
	@rm -f leader peer0 peer1
