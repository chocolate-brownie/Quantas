#include "quantas/Common/Concrete/Backends/BoostMq/Control/ProcessCoordinatorMQ.hpp"
#include "quantas/Common/Concrete/Backends/BoostMq/Control/QueueConfig.hpp"
#include "quantas/Common/Concrete/Runtime/Config/RuntimeConfig.hpp"
#include "quantas/Common/Concrete/Runtime/Topology/TopologyPlanner.hpp"
#include "quantas/Common/Logger.hpp"
#include "quantas/Common/LoggingSupport.hpp"
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
struct LeaderCliArgs {
    std::string inputPath;
    std::optional<size_t> experimentIndex;
    bool preflightOnly{false};
};

struct TestReportInfo {
    size_t testIndex{0};
    std::chrono::duration<double> duration{};
    std::vector<quantas::interfaceId> completedPeers;
    std::vector<quantas::interfaceId> missingPeerIds;
    nlohmann::json peerOutputFiles = nlohmann::json::object();
    bool timedOut{false};
};

/*
 * Purpose: Load the input JSON and verify that it contains at least one
 * experiment. Used by: `main()` at startup so execution stops early when the
 * input is unusable.
 */
std::optional<nlohmann::json> parseAndLoadConfig(const std::string& inputPath) {
    return quantas::loadRuntimeConfig(inputPath);
}

void printUsage(const char* programName) {
    std::cerr << "Usage: " << programName << " --experiment <experiment_index> <input_json>\n"
              << "       " << programName << " --preflight <input_json>\n";
}

std::optional<LeaderCliArgs> parseLeaderArgs(int argc, char* argv[]) {
    if (argc == 3 && std::string(argv[1]) == "--preflight") {
        return LeaderCliArgs{argv[2], std::nullopt, true};
    }
    if (argc != 4 || std::string(argv[1]) != "--experiment") {
        printUsage(argv[0]);
        return std::nullopt;
    }

    try {
        size_t parsedCharacters = 0;
        const long long experimentIndex = std::stoll(argv[2], &parsedCharacters);
        if (parsedCharacters != std::string(argv[2]).size() || experimentIndex < 0) {
            throw std::runtime_error("experiment index must be a non-negative integer");
        }
        return LeaderCliArgs{argv[3], static_cast<size_t>(experimentIndex), false};
    } catch (const std::exception& ex) {
        std::cerr << "error: invalid experiment index: " << ex.what() << '\n';
        return std::nullopt;
    }
}

/* Select every experiment for preflight, or one requested experiment for a
 * normal run. Return no value when the requested experiment does not exist. */
std::optional<std::vector<size_t>>
selectExperimentIndexes(const LeaderCliArgs& cli, size_t experimentCount) {
    std::vector<size_t> indexes;

    if (cli.preflightOnly) {
        indexes.reserve(experimentCount);
        for (size_t i = 0; i < experimentCount; ++i) indexes.push_back(i);
        return indexes;
    }

    if (*cli.experimentIndex >= experimentCount) {
        std::cerr << "error: experiment index " << *cli.experimentIndex << " is out of range\n";
        return std::nullopt;
    }

    indexes.push_back(*cli.experimentIndex);
    return indexes;
}

/*
 * Purpose: Create the experiment-level report metadata and an empty test list.
 * Used by: `main()` as the report object that receives every test result.
 */
nlohmann::json makeBaseExperimentReport(
    size_t expIndex,
    const quantas::RuntimeExperimentConfig& exp,
    const quantas::BoostMqQueueConfig& queueConfig
) {
    nlohmann::json report;
    report["backend"] = "mq";
    report["experimentIndex"] = expIndex;
    report["peerCount"] = exp.initialPeers;
    report["peerType"] = exp.initialPeerType;
    report["topologyType"] = exp.topology.value("type", "unknown");
    report["rounds"] = exp.rounds;
    report["testCount"] = exp.tests;
    report["doneTimeoutMs"] = exp.doneTimeoutMs;
    report["boostMq"] = {
        {"controlQueueCapacity", queueConfig.controlQueueCapacity},
        {"dataQueueCapacity", queueConfig.dataQueueCapacity},
        {"maxMessageSizeBytes", queueConfig.maxMessageSizeBytes}
    };
    report["tests"] = nlohmann::json::array();

    return report;
}

/*
 * Purpose: Convert one completed test's timing, peer completion, and output
 * paths into JSON. Used by: `main()` before appending the test result to the
 * experiment report.
 */
nlohmann::json makeTestReport(const TestReportInfo& info) {
    nlohmann::json testReport;
    testReport["testIndex"] = info.testIndex;
    testReport["durationSeconds"] = info.duration.count();
    testReport["completedPeers"] = info.completedPeers;
    testReport["completedPeerCount"] = info.completedPeers.size();
    testReport["missingPeers"] = info.missingPeerIds;
    testReport["timedOut"] = info.timedOut;
    testReport["success"] = !info.timedOut && info.missingPeerIds.empty();
    testReport["peerOutputFiles"] = info.peerOutputFiles;

    return testReport;
}

/*
 * Purpose: Build the complete peer-id list expected for an experiment.
 * Used by: `main()` to record the expected participants in the leader report.
 */
std::vector<quantas::interfaceId> expectedPeers(int totalPeers) {
    std::vector<quantas::interfaceId> peers;
    peers.reserve(static_cast<size_t>(totalPeers));
    for (int id = 0; id < totalPeers; ++id) {
        peers.push_back(id);
    }
    return peers;
}

/*
 * Purpose: Compare completed peer ids with the expected id range and return any
 * missing peers. Used by: `main()` to determine each test's completion status
 * and failure evidence.
 */
std::vector<quantas::interfaceId>
findMissingPeers(int totalPeers, const std::vector<quantas::interfaceId>& completedPeers) {
    std::vector<bool> seen(static_cast<size_t>(totalPeers), false);
    for (quantas::interfaceId id : completedPeers) {
        if (id >= 0 && id < totalPeers) {
            seen[static_cast<size_t>(id)] = true;
        }
    }

    std::vector<quantas::interfaceId> missing;
    for (int id = 0; id < totalPeers; ++id) {
        if (!seen[static_cast<size_t>(id)]) {
            missing.push_back(id);
        }
    }
    return missing;
}

/*
 * Purpose: Derive the experiment-specific leader report destination from the
 * configured log base. Used by: `main()` before execution so the final report
 * has a stable output path.
 */
std::string makeLeaderReportPath(const std::string& logFileBase, size_t expIndex) {
    if (logFileBase == "cout" || logFileBase == "cerr") {
        return logFileBase;
    }

    std::filesystem::path reportBase(logFileBase);
    reportBase.replace_extension(".json");
    const std::string experimentFile = quantas::makeExperimentFileName(
        reportBase.string(),
        expIndex,
        std::nullopt,
        ".json"
    );
    return quantas::addFileNameSuffix(experimentFile, "_leader_report");
}

/*
 * Purpose: Map every expected peer id to the metrics file produced for one
 * test. Used by: `main()` to include peer output references in each test
 * report.
 */
nlohmann::json makePeerOutputFiles(
    const std::string& logFileBase, size_t expIndex, int testNumber, int totalPeers
) {
    nlohmann::json outputFiles = nlohmann::json::object();
    for (int peerId = 0; peerId < totalPeers; ++peerId) {
        const std::string experimentFile = quantas::makeExperimentFileName(
            logFileBase,
            expIndex,
            peerId,
            ".json"
        );
        outputFiles[std::to_string(peerId)] = quantas::addFileNameSuffix(
            experimentFile,
            "_TEST" + std::to_string(testNumber)
        );
    }
    return outputFiles;
}

/*
 * Purpose: Serialize the completed leader report to its configured file or
 * standard stream. Used by: `main()` after all available test results and
 * experiment status are finalized.
 */
void writeLeaderReport(const std::string& path, const nlohmann::json& report) {
    if (path == "cout") {
        std::cout << report.dump(4) << '\n';
        return;
    }
    if (path == "cerr") {
        std::cerr << report.dump(4) << '\n';
        return;
    }

    std::ofstream output(path);
    if (!output.is_open()) {
        throw std::runtime_error("error: cannot open leader report file: " + path);
    }
    output << report.dump(4) << '\n';
}

/*
 * Purpose: Read one completed peer's LogWriter JSON output.
 * Used by: `readCompletedPeerMetrics` after the peer has sent its done signal.
 */
nlohmann::json readPeerMetricFile(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("error: cannot open peer metric file: " + path);
    }

    nlohmann::json metrics;
    input >> metrics;
    return metrics;
}

/*
 * Purpose: Copy completed peer metric files into the leader report without
 * inventing merge rules. Used by: `main()` before appending each test report.
 */
nlohmann::json readCompletedPeerMetrics(
    const nlohmann::json& peerOutputFiles, const std::vector<quantas::interfaceId>& completedPeers
) {
    nlohmann::json peerMetrics = nlohmann::json::object();

    for (quantas::interfaceId peerId : completedPeers) {
        const std::string key = std::to_string(peerId);
        const std::string path = peerOutputFiles.at(key).get<std::string>();
        if (path == "cout" || path == "cerr") {
            continue;
        }
        peerMetrics[key] = readPeerMetricFile(path);
    }

    return peerMetrics;
}

nlohmann::json summarizeTransportReliability(const nlohmann::json& peerMetrics) {
    uint64_t sentTotal = 0;
    uint64_t receivedRawTotal = 0;
    uint64_t deliveredToInstreamTotal = 0;
    uint64_t droppedBackpressureTotal = 0;

    for (const auto& [peerId, metrics] : peerMetrics.items()) {
        if (!metrics.contains("transportMetrics")) {
            continue;
        }

        const auto& transportMetrics = metrics["transportMetrics"];
        sentTotal += transportMetrics.value("sent", 0);
        receivedRawTotal += transportMetrics.value("received_raw", 0);
        deliveredToInstreamTotal += transportMetrics.value("delivered_to_instream", 0);
        droppedBackpressureTotal += transportMetrics.value("dropped_backpressure", 0);
    }

    const bool reliable = sentTotal == receivedRawTotal &&
                          receivedRawTotal == deliveredToInstreamTotal &&
                          droppedBackpressureTotal == 0;

    return nlohmann::json{
        {"sent_total", sentTotal},
        {"received_raw_total", receivedRawTotal},
        {"delivered_to_instream_total", deliveredToInstreamTotal},
        {"dropped_backpressure_total", droppedBackpressureTotal},
        {"reliable", reliable}
    };
}

} // namespace

/* --------------------------- Leader runtime --------------------------- */
int main(int argc, char* argv[]) {
    /* Validate the command line and select normal execution or validation-only
     * preflight mode. Both modes operate on one JSON input file. */
    const auto cli = parseLeaderArgs(argc, argv);
    if (!cli) return 1;

    /* Load and validate the top-level configuration before acquiring any MQ
     * resources or starting an experiment. */
    auto config = parseAndLoadConfig(cli->inputPath);
    if (!config) return 1;

    /* Select every experiment for preflight, or one requested experiment for a
     * normal run. Return no value when the requested experiment does not exist. */
    const auto experimentIndexes = selectExperimentIndexes(*cli, (*config)["experiments"].size());
    if (!experimentIndexes) return 1;

    /* Use the shared coordinator to own the leader's rendezvous protocol and
     * the lifecycle of its BoostMQ resources. */
    auto& coordinator = quantas::ProcessCoordinatorMQ::instance();

    /* Process each configured experiment independently so failures can be
     * reported with the corresponding experiment index. */
    for (const size_t expIndex : *experimentIndexes) {
        try {
            const nlohmann::json& experiment = (*config)["experiments"].at(expIndex);

            /* Parse backend-independent runtime settings, then validate the
             * BoostMQ queue sizes and topology payloads before creating queues. */
            quantas::RuntimeExperimentConfig exp = quantas::parseRuntimeExperiment(
                *config,
                expIndex
            );

            auto queueConfig = quantas::parseBoostMqQueueConfig(experiment, exp.initialPeers);
            quantas::preflightBoostMqQueues(queueConfig, expIndex);

            const quantas::TopologyResult validationTopology = quantas::buildTopology(exp.topology);
            quantas::validateBoostMqAssignmentPayloads(
                validationTopology.assignments,
                queueConfig,
                expIndex
            );

            if (cli->preflightOnly) continue;

            /* Initialize timing, output paths, and coordinator state for this
             * experiment before any peers enter the rendezvous. */
            std::chrono::time_point<std::chrono::high_resolution_clock> expStartTime, expEndTime;
            std::chrono::duration<double> expDuration;
            expStartTime = std::chrono::high_resolution_clock::now();

            const std::string logFileBase = quantas::chooseLogFileBase(*config, experiment);
            const std::string reportPath = makeLeaderReportPath(logFileBase, expIndex);

            coordinator.configureExperiment(
                expIndex,
                exp.initialPeerType,
                true,
                exp.initialPeers,
                quantas::NO_PEER_ID,
                logFileBase,
                quantas::StopMode::FixedRounds,
                queueConfig
            );

            QUANTAS_LOG_INFO("coord")
                << "leader starting rendezvous for experiment " << expIndex
                << " with totalPeers=" << exp.initialPeers << " tests=" << exp.tests;

            nlohmann::json expReport = makeBaseExperimentReport(expIndex, exp, queueConfig);
            expReport["inputFile"] = cli->inputPath;
            expReport["expectedPeers"] = expectedPeers(exp.initialPeers);
            bool allTestsSucceeded = true;
            bool experimentTimedOut = false;

            /* Run every test in the experiment through a fresh rendezvous and
             * collect an independent result for the final experiment report. */
            for (int testIndex = 0; testIndex < exp.tests; ++testIndex) {
                const int testNumber = testIndex + 1;
                TestReportInfo testInfo;
                testInfo.testIndex = static_cast<size_t>(testIndex);

                QUANTAS_LOG_INFO("coord")
                    << "leader starting experiment " << expIndex << " test " << testNumber;

                std::chrono::time_point<std::chrono::high_resolution_clock> testStartTime;
                std::chrono::time_point<std::chrono::high_resolution_clock> testEndTime;

                testStartTime = std::chrono::high_resolution_clock::now();

                /* Wait for all peers, distribute their topology assignments,
                 * start the run together, and wait for completion or timeout. */
                coordinator.createBarrier();
                coordinator.waitForAllReady();
                quantas::TopologyResult topology = quantas::buildTopology(exp.topology);
                coordinator.sendAssignments(topology.assignments);
                coordinator.broadcastStart();

                const quantas::PeerCompletionResult completion = coordinator.waitForAllDone(
                    std::chrono::milliseconds(exp.doneTimeoutMs)
                );

                testInfo.completedPeers = completion.completedPeers;
                testInfo.timedOut = completion.timedOut;

                testEndTime = std::chrono::high_resolution_clock::now();
                testInfo.duration = testEndTime - testStartTime;

                /* Record completion state and peer output locations, then
                 * aggregate peer transport metrics into the test report. */
                testInfo.missingPeerIds = findMissingPeers(
                    exp.initialPeers,
                    testInfo.completedPeers
                );

                testInfo.peerOutputFiles = makePeerOutputFiles(
                    logFileBase,
                    expIndex,
                    testNumber,
                    exp.initialPeers
                );

                allTestsSucceeded = allTestsSucceeded && !testInfo.timedOut &&
                                    testInfo.missingPeerIds.empty();

                nlohmann::json testReport = makeTestReport(testInfo);

                testReport["peerMetrics"] = readCompletedPeerMetrics(
                    testInfo.peerOutputFiles,
                    testInfo.completedPeers
                );

                testReport["transportReliability"] = summarizeTransportReliability(
                    testReport["peerMetrics"]
                );

                expReport["tests"].push_back(testReport);

                /* Stop peers best-effort after a timeout and always release
                 * this test's coordinator resources before continuing. */
                if (testInfo.timedOut) {
                    experimentTimedOut = true;
                    coordinator.broadcastStopBestEffort();
                }
                coordinator.cleanUp();

                if (experimentTimedOut) {
                    QUANTAS_LOG_ERROR("coord")
                        << "leader timed out in experiment " << expIndex << " test " << testNumber;
                    break;
                }

                QUANTAS_LOG_INFO("coord")
                    << "leader completed experiment " << expIndex << " test " << testNumber;
            }

            /* Finalize and persist the experiment-level duration and success
             * result after all tests finish or the first timeout occurs. */
            expEndTime = std::chrono::high_resolution_clock::now();
            expDuration = expEndTime - expStartTime;
            expReport["durationSeconds"] = expDuration.count();
            expReport["success"] = allTestsSucceeded;
            writeLeaderReport(reportPath, expReport);
            QUANTAS_LOG_INFO("coord") << "leader report written to " << reportPath;

            if (experimentTimedOut) return 1;

        } catch (const std::exception& ex) {
            /* Release any partially initialized MQ state and fail with the
             * experiment index when configuration or runtime work throws. */
            coordinator.cleanUp();
            std::cerr << "error: leader failed at experiment " << expIndex << ": " << ex.what()
                      << '\n';

            return 1;
        }
    }

    /* Confirm that validation-only mode checked every configured experiment
     * without starting a rendezvous. */
    if (cli->preflightOnly) {
        QUANTAS_LOG_INFO("preflight") << "all BoostMQ queue preflight checks passed";
    }

    return 0;
}
