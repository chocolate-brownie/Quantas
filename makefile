# Copyright 2022

# This file is part of QUANTAS.  QUANTAS is free software: you can
# redistribute it and/or modify it under the terms of the GNU General
# Public License as published by the Free Software Foundation, either
# version 3 of the License, or (at your option) any later version.
# QUANTAS is distributed in the hope that it will be useful, but
# WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
# General Public License for more details.  You should have received a
# copy of the GNU General Public License along with QUANTAS. If not,
# see <https://www.gnu.org/licenses/>.

############################### Input ###############################

# Configurable usage to override the hardcoded input:
# [make run INPUTFILE=quantas/ExamplePeer/ExampleInput.json]

# Hard coded usage [make run]
# Configure this for the specific input file.
# Make sure to include the path to the input file 

INPUTFILE := quantas/ExamplePeer/ExampleInput.json

# INPUTFILE := quantas/AltBitPeer/AltBitUtility.json

# INPUTFILE := quantas/PBFTPeer/PBFTInput.json

# INPUTFILE := quantas/BitcoinPeer/BitcoinInput.json

# INPUTFILE := quantas/EthereumPeer/EthereumPeerInput.json

# INPUTFILE := quantas/LinearChordPeer/LinearChordInput.json

# INPUTFILE := quantas/KademliaPeer/KademliaPeerInput.json

# INPUTFILE := quantas/RaftPeer/RaftInput.json

# INPUTFILE := quantas/StableDataLinkPeer/StableDataLinkInput.json

############################### Variables and Flags ###############################

EXE := quantas.exe
MQ_EXE := quantas_mq_peer.exe
MQ_LEADER_EXE := quantas_mq_leader.exe
MQ_PEER_ID ?= 0
MQ_ROUNDS ?=
MQ_TOTAL_PEERS ?= 2
MQ_DEBUG_PEER_ID ?= 0

# compiles all the cpps in Common and main.cpp
COMMON_SRCS := $(wildcard quantas/Common/*.cpp)
COMMON_OBJS := $(COMMON_SRCS:.cpp=.o)

ABSTRACT_OBJS := $(COMMON_OBJS) quantas/Common/Abstract/abstractSimulation.o quantas/Common/Abstract/Channel.o quantas/Common/Abstract/Network.o
CONCRETE_OBJS := $(COMMON_OBJS) quantas/Common/Concrete/concreteSimulation.o quantas/Common/Concrete/ipUtil.o
MQ_OBJS := $(COMMON_OBJS) \
	quantas/Common/ConcreteMQ/ConcreteMqPeer.o \
	quantas/Common/ConcreteMQ/ProcessCoordinatorMQ.o \
	quantas/Common/ConcreteMQ/NetworkInterfaceConcreteMQ.o \
	quantas/Common/Abstract/Channel.o \
	quantas/Common/Concrete/NetworkInterfaceConcrete.o \
	quantas/Common/Concrete/ProcessCoordinator.o \
	quantas/Common/Concrete/ipUtil.o
MQ_LEADER_OBJS := $(COMMON_OBJS) \
	quantas/Common/ConcreteMQ/ConcreteMqLeader.o \
	quantas/Common/ConcreteMQ/ProcessCoordinatorMQ.o

# compiles all cpps specified as necessary in the INPUTFILE
ALGS := $(shell sed -n '/"algorithms"/,/]/p' $(INPUTFILE) \
         | sed -n 's/.*"\([^"]*\.cpp\)".*/quantas\/\1/p')
ALG_OBJS += $(ALGS:.cpp=.o)

# necessary flags
CXX := g++
CXXFLAGS := -pthread -std=c++17
MQ_LDLIBS := -lboost_serialization -lrt
GCC_VERSION := $(shell $(CXX) $(CXXFLAGS) -dumpversion)
GCC_MIN_VERSION := 8

############################### Build Types ###############################

# release for faster runtime, debug for debugging
release: CXXFLAGS += -O3 -s
release: check-version $(EXE)
debug: CXXFLAGS += -O0 -g -D_GLIBCXX_DEBUG 
# -fsanitize=address,undefined -fno-omit-frame-pointer # flag helps with double delete errors
debug: check-version $(EXE)
mq_peer_release: CXXFLAGS += -O3 -s
mq_peer_release: check-version $(MQ_EXE)
mq_peer_debug: CXXFLAGS += -O0 -g -D_GLIBCXX_DEBUG
mq_peer_debug: check-version $(MQ_EXE)
mq_leader_release: CXXFLAGS += -O3 -s
mq_leader_release: check-version $(MQ_LEADER_EXE)
mq_leader_debug: CXXFLAGS += -O0 -g -D_GLIBCXX_DEBUG
mq_leader_debug: check-version $(MQ_LEADER_EXE)

# Backward-compatible aliases (prefer mq_peer_release / mq_peer_debug).
mq_release: mq_peer_release
mq_debug: mq_peer_debug

############################### Running Commands ###############################

help:
	@echo "QUANTAS make targets"
	@echo ""
	@echo "Abstract runtime:"
	@echo "  make run INPUTFILE=quantas/ExamplePeer/ExampleInput.json"
	@echo "  make debug INPUTFILE=..."
	@echo ""
	@echo "MQ peer runtime:"
	@echo "  make mq_peer_debug INPUTFILE=quantas/AltBitPeer/AltBitUtility.json"
	@echo "  make mq_peer_run INPUTFILE=... MQ_PEER_ID=0 [MQ_ROUNDS=10]"
	@echo ""
	@echo "MQ leader runtime:"
	@echo "  make mq_leader_debug INPUTFILE=..."
	@echo "  make mq_leader_run INPUTFILE=..."
	@echo ""
	@echo "MQ leader + peers orchestration:"
	@echo "  make mq_run_all INPUTFILE=... MQ_TOTAL_PEERS=2 [MQ_ROUNDS=10]"
	@echo "  make mq_run_all_debug_peer INPUTFILE=... MQ_TOTAL_PEERS=11 MQ_DEBUG_PEER_ID=0 [MQ_ROUNDS=10]"
	@echo ""
	@echo "Tests / diagnostics:"
	@echo "  make test"
	@echo "  make run_simple_memory INPUTFILE=..."
	@echo "  make clean"

# When running on windows use make clang
clang: CXX := clang++
clang: release
	@echo running with input: $(INPUTFILE)
	@./$(EXE) $(INPUTFILE)

# When running on Linux use make run
run: release
	@echo running with input: $(INPUTFILE)
	@./$(EXE) $(INPUTFILE); exit_code=$$?; \
	if [ $$exit_code -ne 0 ]; then $(call check_failure); exit $$exit_code; fi

mq_peer_run: mq_peer_release
	@echo running MQ peer with input: $(INPUTFILE), peer_id: $(MQ_PEER_ID), rounds: $(MQ_ROUNDS)
	@if [ -n "$(MQ_ROUNDS)" ]; then \
		./$(MQ_EXE) $(INPUTFILE) $(MQ_PEER_ID) $(MQ_ROUNDS); \
	else \
		./$(MQ_EXE) $(INPUTFILE) $(MQ_PEER_ID); \
	fi; \
	exit_code=$$?; \
	if [ $$exit_code -ne 0 ]; then $(call check_failure); exit $$exit_code; fi

mq_leader_run: mq_leader_release
	@echo running MQ leader with input: $(INPUTFILE)
	@./$(MQ_LEADER_EXE) $(INPUTFILE); exit_code=$$?; \
	if [ $$exit_code -ne 0 ]; then $(call check_failure); exit $$exit_code; fi

mq_run_all: mq_peer_release mq_leader_release
	@echo running MQ leader + peers with input: $(INPUTFILE), peers: $(MQ_TOTAL_PEERS), rounds: $(MQ_ROUNDS)
	@rm -f /dev/shm/mq_barrier
	@for i in $$(seq 0 $$(($(MQ_TOTAL_PEERS)-1))); do rm -f /dev/shm/peer_$$i; done
	@./$(MQ_LEADER_EXE) $(INPUTFILE) & leader_pid=$$!; \
	echo "[mq_run_all] started leader pid=$$leader_pid"; \
	sleep 0.2; \
	peer_pids=""; \
	for i in $$(seq 0 $$(($(MQ_TOTAL_PEERS)-1))); do \
		if [ -n "$(MQ_ROUNDS)" ]; then \
			./$(MQ_EXE) $(INPUTFILE) $$i $(MQ_ROUNDS) & \
		else \
			./$(MQ_EXE) $(INPUTFILE) $$i & \
		fi; \
		pid=$$!; \
		echo "[mq_run_all] started peer $$i pid=$$pid"; \
		peer_pids="$$peer_pids $$i:$$pid"; \
	done; \
	overall=0; \
	for entry in $$peer_pids; do \
		peer_id=$${entry%%:*}; pid=$${entry##*:}; \
		wait $$pid; peer_ec=$$?; \
		echo "[mq_run_all] peer $$peer_id (pid $$pid) exit code=$$peer_ec"; \
		if [ $$peer_ec -ne 0 ] && [ $$overall -eq 0 ]; then overall=$$peer_ec; fi; \
	done; \
	wait $$leader_pid; leader_ec=$$?; \
	echo "[mq_run_all] leader (pid $$leader_pid) exit code=$$leader_ec"; \
	if [ $$leader_ec -ne 0 ] && [ $$overall -eq 0 ]; then overall=$$leader_ec; fi; \
	if [ $$overall -ne 0 ]; then $(call check_failure); exit $$overall; fi

mq_run_all_debug_peer: mq_peer_debug mq_leader_debug
	@echo running MQ leader + peers with gdb on peer $(MQ_DEBUG_PEER_ID), input: $(INPUTFILE), peers: $(MQ_TOTAL_PEERS), rounds: $(MQ_ROUNDS)
	@rm -f /dev/shm/mq_barrier
	@for i in $$(seq 0 $$(($(MQ_TOTAL_PEERS)-1))); do rm -f /dev/shm/peer_$$i; done
	@./$(MQ_LEADER_EXE) $(INPUTFILE) & leader_pid=$$!; \
	echo "[mq_run_all_debug_peer] started leader pid=$$leader_pid"; \
	sleep 0.2; \
	peer_pids=""; \
	for i in $$(seq 0 $$(($(MQ_TOTAL_PEERS)-1))); do \
		if [ $$i -eq $(MQ_DEBUG_PEER_ID) ]; then \
			continue; \
		fi; \
		if [ -n "$(MQ_ROUNDS)" ]; then \
			./$(MQ_EXE) $(INPUTFILE) $$i $(MQ_ROUNDS) & \
		else \
			./$(MQ_EXE) $(INPUTFILE) $$i & \
		fi; \
		pid=$$!; \
		echo "[mq_run_all_debug_peer] started peer $$i pid=$$pid"; \
		peer_pids="$$peer_pids $$i:$$pid"; \
	done; \
	echo "[mq_run_all_debug_peer] launching gdb for peer $(MQ_DEBUG_PEER_ID)"; \
	if [ -n "$(MQ_ROUNDS)" ]; then \
		gdb --args ./$(MQ_EXE) $(INPUTFILE) $(MQ_DEBUG_PEER_ID) $(MQ_ROUNDS); \
	else \
		gdb --args ./$(MQ_EXE) $(INPUTFILE) $(MQ_DEBUG_PEER_ID); \
	fi; \
	gdb_ec=$$?; \
	echo "[mq_run_all_debug_peer] gdb peer $(MQ_DEBUG_PEER_ID) exit code=$$gdb_ec"; \
	overall=$$gdb_ec; \
	for entry in $$peer_pids; do \
		peer_id=$${entry%%:*}; pid=$${entry##*:}; \
		wait $$pid; peer_ec=$$?; \
		echo "[mq_run_all_debug_peer] peer $$peer_id (pid $$pid) exit code=$$peer_ec"; \
		if [ $$peer_ec -ne 0 ] && [ $$overall -eq 0 ]; then overall=$$peer_ec; fi; \
	done; \
	wait $$leader_pid; leader_ec=$$?; \
	echo "[mq_run_all_debug_peer] leader (pid $$leader_pid) exit code=$$leader_ec"; \
	if [ $$leader_ec -ne 0 ] && [ $$overall -eq 0 ]; then overall=$$leader_ec; fi; \
	if [ $$overall -ne 0 ]; then $(call check_failure); exit $$overall; fi

############################### Debugging ###############################

# runs the program with full Valgrind to trace memory leaks
run_memory: debug
	@echo running: $(INPUTFILE) with valgrind
	@valgrind --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
		 ./$(EXE) $(INPUTFILE)

# runs the program with Valgrind to see if there are any memory leaks
run_simple_memory: debug
	@echo ""
	@echo running: $(INPUTFILE) with valgrind
	@valgrind --leak-check=full ./$(EXE) $(INPUTFILE) 2>&1 \
		| grep -E "HEAP SUMMARY|in use|LEAK SUMMARY|definitely lost: |indirectly lost: |possibly lost: |still reachable: |ERROR SUMMARY"
	@echo ""

# runs the program with GDB for more advanced error viewing
run_debug: debug
	@gdb -q -nx \
		 -iex "set pagination off" \
		 --ex "set pagination off" \
		 --ex "set height 0" \
	     --ex "set debuginfod enabled off" \
	     --ex "set print thread-events off" \
	     --ex run \
	     --ex backtrace \
	     --args ./$(EXE) $(INPUTFILE); \
	exit_code=$$?; \
	if [ $$exit_code -ne 0 ]; then $(call check_failure); exit $$exit_code; fi

############################### Tests ###############################

# Test thread based random number generation
rand_test: quantas/Tests/randtest.cpp
	@echo "Testing thread based random number generation..."
	@make --no-print-directory clean
	@$(CXX) $(CXXFLAGS) $^ -o $@.exe
	@./$@.exe
	@echo ""
	
# in the future this could be generalized to go through every file in a Tests
# folder such that the input files need not be listed here
TEST_INPUTS := quantas/ExamplePeer/ExampleInput.json quantas/AltBitPeer/AltBitUtility.json quantas/PBFTPeer/PBFTInput.json quantas/BitcoinPeer/BitcoinInput.json quantas/EthereumPeer/EthereumPeerInput.json quantas/LinearChordPeer/LinearChordInput.json quantas/KademliaPeer/KademliaPeerInput.json quantas/RaftPeer/RaftInput.json quantas/StableDataLinkPeer/StableDataLinkInput.json

test: check-version rand_test
	@make --no-print-directory clean
	@echo "Running memory tests on all test inputs..."
	@echo ""
	@for file in $(TEST_INPUTS); do \
		$(MAKE) --no-print-directory run_simple_memory INPUTFILE="$$file"; \
	done

############################### Helpers ###############################

# Define a helper function to check dmesg for errors
define check_failure
    echo "Make target '$@' failed! Checking kernel logs..."; \
    { \
      if command -v journalctl >/dev/null 2>&1; then \
        journalctl -k -n 200 --no-pager 2>/dev/null; \
      else \
        dmesg 2>/dev/null | tail -200; \
      fi; \
    } | grep -iE 'oom|killed|segfault|error' || echo "No relevant logs found."
endef

%.o: %.cpp
	@echo compiling $<
	@$(CXX) $(CXXFLAGS) -c $< -o $@

# check the version of the GCC compiler being used is above a threshold
check-version:
	@if [ "$(GCC_VERSION)" -lt "$(GCC_MIN_VERSION)" ]; then echo "Default version of g++ must be higher than 8."; fi
	@if [ "$(GCC_VERSION)" -lt "$(GCC_MIN_VERSION)" ]; then echo "To change the default version visit: https://linuxconfig.org/how-to-switch-between-multiple-gcc-and-g-compiler-versions-on-ubuntu-20-04-lts-focal-fossa"; fi
	@if [ "$(GCC_VERSION)" -lt "$(GCC_MIN_VERSION)" ]; then exit 1; fi

$(EXE): $(ALG_OBJS) $(ABSTRACT_OBJS)
	@$(CXX) $(CXXFLAGS) $^ -o $(EXE)

$(MQ_EXE): $(ALG_OBJS) $(MQ_OBJS)
	@$(CXX) $(CXXFLAGS) $^ -o $(MQ_EXE) $(MQ_LDLIBS)

$(MQ_LEADER_EXE): $(MQ_LEADER_OBJS)
	@$(CXX) $(CXXFLAGS) $^ -o $(MQ_LEADER_EXE) $(MQ_LDLIBS)

############################### Cleanup ###############################

# enables recursive glob patterns for bash to clean out unecessary files
clean: SHELL := /bin/bash -O globstar
clean:
	@$(RM) **/*.out
	@$(RM) **/*.o
	@$(RM) **/*.d
	@$(RM) **/*.dSYM
	@$(RM) **/*.gch
	@$(RM) **/*.tmp
	@$(RM) **/*.exe

clean_txt: SHELL := /bin/bash -O globstar
clean_txt:
	@$(RM) **/*.txt

# -include $(OBJS:.o=.d)

############################### PHONY ###############################

# All make commands found in this file
.PHONY: help clean run mq_peer_run mq_leader_run mq_run_all mq_run_all_debug_peer release debug mq_peer_release mq_peer_debug mq_release mq_debug mq_leader_release mq_leader_debug $(EXE) $(MQ_EXE) $(MQ_LEADER_EXE) %.o clang run_memory run_simple_memory run_debug check-version rand_test test clean_txt
