/*
Copyright 2022

This file is part of QUANTAS.
QUANTAS is free software: you can redistribute it and/or modify it under the
terms of the GNU General Public License as published by the Free Software
Foundation, either version 3 of the License, or (at your option) any later
version. QUANTAS is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE. See the GNU General Public License for more details.
You should have received a copy of the GNU General Public License along with
QUANTAS. If not, see <https://www.gnu.org/licenses/>.
*/
//
// This class handles reading in a configuration file, setting up log files for
// the simulation, initializing the network class, and repeating a simulation
// according to the configuration file (i.e., running multiple experiments with
// the same configuration).  It is templated with a user defined message and
// peer class, used for the underlaying network instance.

#ifndef Simulation_hpp
#define Simulation_hpp

#include <chrono>
#include <fstream>
#include <thread>

#include "../BS_thread_pool.hpp"
#include "../LogWriter.hpp"
#include "../memoryUtil.hpp"
#include "Network.hpp"

using std::ofstream;
using std::thread;

namespace quantas {

class Simulation {
  private:
    Network system;

    static size_t _peakMemoryKB;

  public:
    inline void run(json config);
};

size_t Simulation::_peakMemoryKB = 0;

inline void Simulation::run(json config) {
    std::string logFile = config.value("logFile", "cout");
    LogWriter::setLogFile(logFile); // Set the log file to the console

    std::chrono::time_point<std::chrono::high_resolution_clock> startTime,
        endTime; // chrono time points

    std::chrono::duration<double> duration; // chrono time interval
    startTime = std::chrono::high_resolution_clock::now();

    int _threadCount = config.value(
        "threadCount", thread::hardware_concurrency()
    ); // By default, use as many hardware cores as possible
    if (_threadCount <= 0) {
        _threadCount = 1;
    }
    if (_threadCount > config["topology"]["initialPeers"]) {
        _threadCount = config["topology"]["initialPeers"];
    }
    int networkSize = static_cast<int>(config["topology"]["initialPeers"]);

    BS::thread_pool pool(_threadCount);
    for (int i = 0; i < config["tests"]; i++) {
        // Tells LogWriter which test we're on
        LogWriter::instance()->setTest(i);

        // Reset round counter
        RoundManager::instance()->setCurrentRound(0);

        // Set how many rounds to run
        RoundManager::instance()->setLastRound(config["rounds"]);

        /* stores the channel config (delay type, drop probability, duplicate
         * probability, etc.) from the JSON.  All three fields are inside that
         * one object. When Channel needs them later, it reads individual fields
         * from it — like _distribution["maxDelay"] or
         * _distribution["dropProbability"]. But the storage is a single JSON
         * blob, not three separate variables. */
        system.setDistribution(config["distribution"]);

        /* creates all the peers and wires them together based on the topology
           type. So if the JSON says "type": "complete" with 20 peers, it
           creates 20 Peer objects and connects every peer to every other peer
           with channels. This is where the network gets built. */
        system.initNetwork(config["topology"]);

        if (config.contains("parameters")) {
            system.initParameters(config["parameters"]);
        } else {
            json empty;
            system.initParameters(empty);
        }

        // std::cout << "Test " << i + 1 << std::endl;
        for (int j = 0; j < config["rounds"]; j++) {
            // std::cout << "ROUND " << j + 1 << std::endl;
            RoundManager::incrementRound();

            // do the receive phase of the round
            BS::multi_future<void> receive_loop =
                pool.parallelize_loop(networkSize, [this](int a, int b) {
                    system.receive(a, b);
                });
            receive_loop.wait();

            BS::multi_future<void> compute_loop =
                pool.parallelize_loop(networkSize, [this](int a, int b) {
                    system.tryPerformComputation(a, b);
                });
            compute_loop.wait();

            system.endOfRound(); // do any end of round computations
        }
    }

    endTime = std::chrono::high_resolution_clock::now();
    duration = endTime - startTime;
    LogWriter::setValue("RunTime", double(duration.count()));

    size_t peakMemoryKB = getPeakMemoryKB();
    if (_peakMemoryKB < peakMemoryKB) {
        _peakMemoryKB = peakMemoryKB;
        LogWriter::setValue("Peak Memory KB", peakMemoryKB);
    } else {
        LogWriter::setValue("Previous Peak Memory KB", peakMemoryKB);
    }

    LogWriter::print();
}

} // namespace quantas

#endif /* Simulation_hpp */
