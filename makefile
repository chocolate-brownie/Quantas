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
MQ_RESOURCE_DIR := /dev/shm
MQ_PROCESS_PATTERN := (^|/)[q]uantas_mq_(leader|peer)\.exe([[:space:]]|$$)
MQ_RESOURCE_REGEX := .*/(mq_barrier|mq_done|peer_[0-9]+_(control|data))

# Sources shared by each executable.
COMMON_SRCS := $(wildcard quantas/Common/*.cpp)
ABSTRACT_SRCS := $(COMMON_SRCS) \
	quantas/Common/Abstract/abstractSimulation.cpp \
	quantas/Common/Abstract/Channel.cpp \
	quantas/Common/Abstract/Network.cpp \
	quantas/Common/Concrete/Backends/TCP/NetworkInterfaceConcrete.cpp \
	quantas/Common/Concrete/Backends/TCP/ProcessCoordinator.cpp \
	quantas/Common/Concrete/Backends/TCP/ipUtil.cpp
MQ_SRCS := $(COMMON_SRCS) \
	quantas/Common/Concrete/Backends/BoostMq/Entrypoints/ConcreteMqPeer.cpp \
	quantas/Common/Concrete/Runtime/Config/RuntimeConfig.cpp \
	quantas/Common/Concrete/Backends/BoostMq/Control/QueueConfig.cpp \
	quantas/Common/Concrete/Backends/BoostMq/Control/ProcessCoordinatorMQ.cpp \
	quantas/Common/Concrete/Runtime/Topology/TopologyPlanner.cpp \
	quantas/Common/Concrete/Backends/BoostMq/Transport/NetworkInterfaceConcreteMQ.cpp \
	quantas/Common/Abstract/Channel.cpp \
	quantas/Common/Concrete/Backends/TCP/NetworkInterfaceConcrete.cpp \
	quantas/Common/Concrete/Backends/TCP/ProcessCoordinator.cpp \
	quantas/Common/Concrete/Backends/TCP/ipUtil.cpp
MQ_LEADER_SRCS := $(COMMON_SRCS) \
	quantas/Common/Concrete/Backends/BoostMq/Entrypoints/ConcreteMqLeader.cpp \
	quantas/Common/Concrete/Runtime/Config/RuntimeConfig.cpp \
	quantas/Common/Concrete/Backends/BoostMq/Control/QueueConfig.cpp \
	quantas/Common/Concrete/Runtime/Topology/TopologyPlanner.cpp \
	quantas/Common/Concrete/Backends/BoostMq/Control/ProcessCoordinatorMQ.cpp

# compiles all cpps specified as necessary in the INPUTFILE
ALGS := $(shell python3 -c 'import json, sys; print(" ".join("quantas/" + path for path in json.load(open(sys.argv[1]))["algorithms"]))' "$(INPUTFILE)")

# necessary flags
CXX := g++
CXXFLAGS := -pthread -std=c++17 -I.
LDFLAGS :=
MQ_LDLIBS := -lboost_serialization -lrt
GCC_VERSION := $(shell $(CXX) $(CXXFLAGS) -dumpversion)
GCC_MIN_VERSION := 8
MQ_DEP_CHECK := /tmp/quantas_mq_dep_check

RELEASE_ABSTRACT_OBJS := $(addprefix build/release/,$(ABSTRACT_SRCS:.cpp=.o) $(ALGS:.cpp=.o))
DEBUG_ABSTRACT_OBJS := $(addprefix build/debug/,$(ABSTRACT_SRCS:.cpp=.o) $(ALGS:.cpp=.o))
CLANG_ABSTRACT_OBJS := $(addprefix build/clang/,$(ABSTRACT_SRCS:.cpp=.o) $(ALGS:.cpp=.o))
RELEASE_MQ_OBJS := $(addprefix build/release/,$(MQ_SRCS:.cpp=.o) $(ALGS:.cpp=.o))
DEBUG_MQ_OBJS := $(addprefix build/debug/,$(MQ_SRCS:.cpp=.o) $(ALGS:.cpp=.o))
RELEASE_MQ_LEADER_OBJS := $(addprefix build/release/,$(MQ_LEADER_SRCS:.cpp=.o))
DEBUG_MQ_LEADER_OBJS := $(addprefix build/debug/,$(MQ_LEADER_SRCS:.cpp=.o))

ALL_DEPFILES := $(RELEASE_ABSTRACT_OBJS:.o=.d) $(DEBUG_ABSTRACT_OBJS:.o=.d) \
	$(CLANG_ABSTRACT_OBJS:.o=.d) $(RELEASE_MQ_OBJS:.o=.d) $(DEBUG_MQ_OBJS:.o=.d) \
	$(RELEASE_MQ_LEADER_OBJS:.o=.d) $(DEBUG_MQ_LEADER_OBJS:.o=.d)

############################### Build Types ###############################

# release for faster runtime, debug for debugging
release: check-version build/release/$(EXE)
	@ln -sfn build/release/$(EXE) $(EXE)
debug: check-version build/debug/$(EXE)
	@ln -sfn build/debug/$(EXE) $(EXE)
mq_peer_release: check-version build/release/$(MQ_EXE)
	@ln -sfn build/release/$(MQ_EXE) $(MQ_EXE)
mq_peer_debug: check-version build/debug/$(MQ_EXE)
	@ln -sfn build/debug/$(MQ_EXE) $(MQ_EXE)
mq_leader_release: check-version build/release/$(MQ_LEADER_EXE)
	@ln -sfn build/release/$(MQ_LEADER_EXE) $(MQ_LEADER_EXE)
mq_leader_debug: check-version build/debug/$(MQ_LEADER_EXE)
	@ln -sfn build/debug/$(MQ_LEADER_EXE) $(MQ_LEADER_EXE)

# Build-only aliases for developers working on the two BoostMQ binaries.
mq_release: mq_peer_release mq_leader_release

############################### Running Commands ###############################

help:
	@printf "🧭 QUANTAS make targets\n"
	@echo ""
	@printf "🧪Abstract runtime\n"
	@printf "  %s\n" "make run INPUTFILE=quantas/ExamplePeer/ExampleInput.json"
	@printf "  %s\n" "make debug INPUTFILE=..."
	@echo ""
	@printf "🚀BoostMQ runtime\n"
	@printf "  %-42s # %s\n" "make mq INPUTFILE=..." "run the JSON experiments with release binaries"
	@printf "  %-42s # %s\n" "make mq_debug INPUTFILE=..." "run the same JSON experiments with debug binaries"
	@echo ""
	@printf "🔍Tests / diagnostics\n"
	@printf "  %-42s # %s\n" "make check_mq_deps" "verify BoostMQ compile/link dependencies"
	@printf "  %-42s # %s\n" "make mq_status" "list active BoostMQ processes and queue resources"
	@printf "  %-42s # %s\n" "make mq_cleanup" "remove abandoned BoostMQ queues; refuses while active"
	@printf "  %-42s # %s\n" "make test" "run focused tests and memory checks for all sample inputs"
	@printf "  %-42s # %s\n" "make run_simple_memory INPUTFILE=..." "run a concise Valgrind memory check"
	@printf "  %-42s # %s\n" "make clean_outputs" "remove root generated .txt/.json outputs"
	@printf "  %-42s # %s\n" "make clean_txt" "remove root generated .txt outputs only"
	@printf "  %-42s # %s\n" "make clean" "remove build artifacts, binaries, and generated outputs"

# Build and run abstract mode with the platform's normal Clang toolchain.
clang: check-clang build/clang/$(EXE)
	@ln -sfn build/clang/$(EXE) $(EXE)
	@echo running with input: $(INPUTFILE)
	@./$(EXE) $(INPUTFILE)

# When running on Linux use make run
run: release
	@echo running with input: $(INPUTFILE)
	@./$(EXE) $(INPUTFILE); exit_code=$$?; \
	if [ $$exit_code -ne 0 ]; then $(call check_failure); exit $$exit_code; fi

mq: mq_peer_release mq_leader_release
	$(call reject_removed_mq_variables)
	@$(MAKE) --no-print-directory -s mq_cleanup >/dev/null
	$(call run_mq_experiments)
	@$(MAKE) --no-print-directory -s mq_cleanup >/dev/null

mq_debug: mq_peer_debug mq_leader_debug
	$(call reject_removed_mq_variables)
	@$(MAKE) --no-print-directory -s mq_cleanup >/dev/null
	$(call run_mq_experiments)
	@$(MAKE) --no-print-directory -s mq_cleanup >/dev/null

############################### Debugging ###############################

# runs the program with full Valgrind to trace memory leaks
run_memory: debug
	@echo running: $(INPUTFILE) with valgrind
	@valgrind --error-exitcode=99 --leak-check=full \
         --show-leak-kinds=all \
         --track-origins=yes \
		 ./$(EXE) $(INPUTFILE)

# runs the program with Valgrind to see if there are any memory leaks
run_simple_memory: debug
	@echo ""
	@echo running: $(INPUTFILE) with valgrind
	@mkdir -p build/tests
	@valgrind --error-exitcode=99 --leak-check=full ./$(EXE) $(INPUTFILE) \
		>build/tests/valgrind.log 2>&1; status=$$?; \
	grep -E "HEAP SUMMARY|in use|LEAK SUMMARY|definitely lost: |indirectly lost: |possibly lost: |still reachable: |ERROR SUMMARY" build/tests/valgrind.log; \
	exit $$status
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
rand_test: build/tests/rand_test.exe
	@echo "Testing thread based random number generation..."
	@./build/tests/rand_test.exe
	@echo ""

mq_timeout_test: build/tests/mq_timeout_test.exe
	@echo "Testing MQ completion timeout..."
	@./build/tests/mq_timeout_test.exe
	@echo ""

mq_queue_config_test: build/tests/mq_queue_config_test.exe
	@echo "Testing BoostMQ queue configuration..."
	@./build/tests/mq_queue_config_test.exe
	@echo ""

mq_transport_metrics_test: build/tests/mq_transport_metrics_test.exe
	@echo "Testing BoostMQ transport metrics..."
	@./build/tests/mq_transport_metrics_test.exe
	@echo ""

mq_runtime_config_test: build/tests/mq_runtime_config_test.exe
	@echo "Testing required runtime counts..."
	@./build/tests/mq_runtime_config_test.exe
	@echo ""

mq_run_counts_test: mq_peer_debug mq_leader_debug mq_runtime_config_test
	@echo "Testing JSON-only BoostMQ run counts..."
	@bash quantas/Tests/mqRunCountsTest.sh
	@echo ""

mq_cleanup_test:
	@echo "Testing safe BoostMQ cleanup..."
	@bash quantas/Tests/mqCleanupTest.sh
	@echo ""

# in the future this could be generalized to go through every file in a Tests
# folder such that the input files need not be listed here
TEST_INPUTS := quantas/ExamplePeer/ExampleInput.json quantas/AltBitPeer/AltBitUtility.json quantas/PBFTPeer/PBFTInput.json quantas/BitcoinPeer/BitcoinPeerInput.json quantas/EthereumPeer/EthereumPeerInput.json quantas/LinearChordPeer/LinearChordInput.json quantas/KademliaPeer/KademliaPeerInput.json quantas/RaftPeer/RaftInput.json quantas/StableDataLinkPeer/StableDataLinkInput.json

test: check-version rand_test mq_timeout_test mq_queue_config_test mq_transport_metrics_test mq_runtime_config_test mq_run_counts_test mq_cleanup_test
	@echo "Running memory tests on all test inputs..."
	@echo ""
	@for file in $(TEST_INPUTS); do \
		$(MAKE) --no-print-directory run_simple_memory INPUTFILE="$$file" || exit $$?; \
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

define reject_removed_mq_variables
$(if $(filter undefined,$(origin MQ_TOTAL_PEERS)),,$(error MQ_TOTAL_PEERS was removed; edit the JSON input file))
$(if $(filter undefined,$(origin MQ_ROUNDS)),,$(error MQ_ROUNDS was removed; edit the JSON input file))
$(if $(filter undefined,$(origin MQ_PEER_ID)),,$(error MQ_PEER_ID was removed; peer IDs come from the JSON experiment))
$(if $(filter undefined,$(origin MQ_DEBUG_PEER_ID)),,$(error MQ_DEBUG_PEER_ID was removed; mq_debug runs the complete JSON experiment))
endef

define run_mq_experiments
	@./$(MQ_LEADER_EXE) --preflight "$(INPUTFILE)"
	@plan=$$(python3 -c 'import json, sys; config=json.load(open(sys.argv[1])); [print(index, experiment["topology"]["initialPeers"]) for index, experiment in enumerate(config["experiments"])]' "$(INPUTFILE)"); \
	test -n "$$plan" || { echo "error: no experiments found in $(INPUTFILE)"; exit 1; }; \
	printf '%s\n' "$$plan" | while read -r experiment_index total_peers; do \
		echo "[mq] experiment $$experiment_index: peers=$$total_peers input=$(INPUTFILE)"; \
		leader_pid=""; peer_pids=""; \
		remove_resources() { \
			resources=$$(find "$(MQ_RESOURCE_DIR)" -maxdepth 1 -type f \
				-regextype posix-extended -regex '$(MQ_RESOURCE_REGEX)' -print); \
			for resource in $$resources; do $(RM) -- "$$resource"; done; \
		}; \
		terminate_children() { \
			if test -n "$$leader_pid"; then kill $$leader_pid 2>/dev/null || true; fi; \
			for entry in $$peer_pids; do kill $${entry##*:} 2>/dev/null || true; done; \
			if test -n "$$leader_pid"; then wait $$leader_pid 2>/dev/null || true; fi; \
			for entry in $$peer_pids; do wait $${entry##*:} 2>/dev/null || true; done; \
			remove_resources; \
		}; \
		trap 'terminate_children; exit 130' INT TERM HUP; \
		./$(MQ_LEADER_EXE) --experiment $$experiment_index "$(INPUTFILE)" & leader_pid=$$!; \
		echo "[mq] started leader pid=$$leader_pid"; \
		sleep 0.2; \
		for i in $$(seq 0 $$((total_peers-1))); do \
			./$(MQ_EXE) --experiment $$experiment_index "$(INPUTFILE)" $$i & \
			pid=$$!; \
			echo "[mq] started peer $$i pid=$$pid"; \
			peer_pids="$$peer_pids $$i:$$pid"; \
		done; \
		wait $$leader_pid; leader_ec=$$?; \
		echo "[mq] leader (pid $$leader_pid) exit code=$$leader_ec"; \
		overall=$$leader_ec; \
		if test $$leader_ec -ne 0; then \
			terminate_children; \
			trap - INT TERM HUP; \
			$(call check_failure); \
			exit $$overall; \
		fi; \
		for entry in $$peer_pids; do \
			peer_id=$${entry%%:*}; pid=$${entry##*:}; \
			wait $$pid; peer_ec=$$?; \
			echo "[mq] peer $$peer_id (pid $$pid) exit code=$$peer_ec"; \
			if test $$peer_ec -ne 0 && test $$overall -eq 0; then overall=$$peer_ec; fi; \
		done; \
		trap - INT TERM HUP; \
		leader_pid=""; peer_pids=""; \
		if test $$overall -ne 0; then remove_resources; $(call check_failure); exit $$overall; fi; \
	done
endef

build/release/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo compiling $<
	@$(CXX) $(CXXFLAGS) -O3 -MMD -MP -c $< -o $@

build/debug/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo compiling $<
	@$(CXX) $(CXXFLAGS) -O0 -g -MMD -MP -c $< -o $@

build/clang/%.o: %.cpp
	@mkdir -p $(dir $@)
	@echo compiling $<
	@clang++ $(CXXFLAGS) -O3 -MMD -MP -c $< -o $@

# check the version of the GCC compiler being used is above a threshold
check-version:
	@if [ "$(GCC_VERSION)" -lt "$(GCC_MIN_VERSION)" ]; then \
		echo "$(CXX) version must be at least $(GCC_MIN_VERSION) (found $(GCC_VERSION))."; \
		exit 1; \
	fi

check-clang:
	@command -v clang++ >/dev/null || { echo "clang++ is not installed or not in PATH."; exit 1; }

check_mq_deps:
	@printf '%s\n' \
		'#include <boost/interprocess/ipc/message_queue.hpp>' \
		'#include <boost/archive/binary_oarchive.hpp>' \
		'#include <boost/serialization/set.hpp>' \
		'int main() { return 0; }' \
		| $(CXX) $(CXXFLAGS) -x c++ - -o $(MQ_DEP_CHECK) $(LDFLAGS) $(MQ_LDLIBS) \
			>/tmp/quantas_mq_dep_check.out 2>/tmp/quantas_mq_dep_check.err || { \
				echo "Missing BoostMQ build dependencies."; \
				echo "On Ubuntu/Linux Mint install: sudo apt install libboost-serialization-dev"; \
				cat /tmp/quantas_mq_dep_check.err; \
				exit 1; \
			}
	@$(RM) $(MQ_DEP_CHECK) /tmp/quantas_mq_dep_check.out /tmp/quantas_mq_dep_check.err

build/release/$(EXE): $(RELEASE_ABSTRACT_OBJS)
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) -s

build/debug/$(EXE): $(DEBUG_ABSTRACT_OBJS)
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS)

build/clang/$(EXE): $(CLANG_ABSTRACT_OBJS)
	@clang++ $(CXXFLAGS) $^ -o $@ $(LDFLAGS) -s

build/release/$(MQ_EXE): $(RELEASE_MQ_OBJS) | check_mq_deps
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) -s $(MQ_LDLIBS)

build/debug/$(MQ_EXE): $(DEBUG_MQ_OBJS) | check_mq_deps
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) $(MQ_LDLIBS)

build/release/$(MQ_LEADER_EXE): $(RELEASE_MQ_LEADER_OBJS) | check_mq_deps
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) -s $(MQ_LDLIBS)

build/debug/$(MQ_LEADER_EXE): $(DEBUG_MQ_LEADER_OBJS) | check_mq_deps
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(LDFLAGS) $(MQ_LDLIBS)

build/tests/rand_test.exe: quantas/Tests/randtest.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $^ -o $@

build/tests/mq_timeout_test.exe: quantas/Tests/processCoordinatorMQTimeoutTest.cpp \
		quantas/Common/Concrete/Backends/BoostMq/Control/ProcessCoordinatorMQ.cpp quantas/Common/Logger.cpp | check_mq_deps
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(MQ_LDLIBS)

build/tests/mq_queue_config_test.exe: quantas/Tests/boostMqQueueConfigTest.cpp \
		quantas/Common/Concrete/Backends/BoostMq/Control/QueueConfig.cpp quantas/Common/Logger.cpp | check_mq_deps
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(MQ_LDLIBS)

build/tests/mq_transport_metrics_test.exe: quantas/Tests/boostMqTransportMetricsTest.cpp \
		quantas/Common/Concrete/Backends/BoostMq/Transport/NetworkInterfaceConcreteMQ.cpp \
		quantas/Common/Logger.cpp | check_mq_deps
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $^ -o $@ $(MQ_LDLIBS)

build/tests/mq_runtime_config_test.exe: quantas/Tests/runtimeConfigCountTest.cpp \
		quantas/Common/Concrete/Runtime/Config/RuntimeConfig.cpp
	@mkdir -p $(dir $@)
	@$(CXX) $(CXXFLAGS) $^ -o $@

-include $(ALL_DEPFILES)

############################### Cleanup ###############################

# enables recursive glob patterns for bash to clean out unecessary files
clean: SHELL := /bin/bash -O globstar
clean: clean_outputs
	@$(RM) -r build
	@$(RM) **/*.out
	@$(RM) **/*.o
	@$(RM) **/*.d
	@$(RM) **/*.dSYM
	@$(RM) **/*.gch
	@$(RM) **/*.tmp
	@$(RM) **/*.exe
	@$(RM) ./vgcore.*

mq_status:
	@command -v pgrep >/dev/null 2>&1 || { echo "error: pgrep is required."; exit 1; }
	@test -d "$(MQ_RESOURCE_DIR)" || { \
		echo "error: BoostMQ resource directory $(MQ_RESOURCE_DIR) is unavailable."; exit 1; \
	}
	@echo "Active QUANTAS BoostMQ processes:"
	@processes=$$(pgrep -af '$(MQ_PROCESS_PATTERN)' || true); \
	if test -n "$$processes"; then printf '%s\n' "$$processes"; else echo "  none"; fi
	@echo "QUANTAS BoostMQ resources in $(MQ_RESOURCE_DIR):"
	@resources=$$(find "$(MQ_RESOURCE_DIR)" -maxdepth 1 -type f \
		-regextype posix-extended -regex '$(MQ_RESOURCE_REGEX)' -print | LC_ALL=C sort); \
	if test -n "$$resources"; then printf '%s\n' "$$resources"; else echo "  none"; fi

mq_cleanup:
	@command -v pgrep >/dev/null 2>&1 || { echo "error: pgrep is required; refusing cleanup."; exit 1; }
	@test -d "$(MQ_RESOURCE_DIR)" || { \
		echo "error: BoostMQ resource directory $(MQ_RESOURCE_DIR) is unavailable."; exit 1; \
	}
	@resources=$$(find "$(MQ_RESOURCE_DIR)" -maxdepth 1 -type f \
		-regextype posix-extended -regex '$(MQ_RESOURCE_REGEX)' -print | LC_ALL=C sort); \
	processes=$$(pgrep -af '$(MQ_PROCESS_PATTERN)' || true); \
	if test -n "$$processes"; then \
		echo "error: active QUANTAS BoostMQ processes detected; refusing cleanup:"; \
		printf '%s\n' "$$processes"; \
		if test -n "$$resources"; then \
			for resource in $$resources; do echo "Skipped $$resource: active process detected"; done; \
		else \
			echo "No QUANTAS BoostMQ resources found to remove."; \
		fi; \
		exit 1; \
	fi; \
	if test -z "$$resources"; then \
		echo "No abandoned QUANTAS BoostMQ resources found."; \
		exit 0; \
	fi; \
	for resource in $$resources; do \
		if $(RM) -- "$$resource"; then \
			echo "Removed $$resource"; \
		else \
			echo "error: failed to remove $$resource"; \
			exit 1; \
		fi; \
	done

clean_outputs:
	@$(RM) ./*.txt ./*.json

clean_txt:
	@$(RM) ./*.txt

############################### PHONY ###############################

# All make commands found in this file
.PHONY: help clean mq_status mq_cleanup run mq mq_debug release debug mq_peer_release mq_peer_debug mq_release mq_leader_release mq_leader_debug clang run_memory run_simple_memory run_debug check-version check-clang check_mq_deps rand_test mq_timeout_test mq_queue_config_test mq_transport_metrics_test mq_runtime_config_test mq_run_counts_test mq_cleanup_test test clean_outputs clean_txt
