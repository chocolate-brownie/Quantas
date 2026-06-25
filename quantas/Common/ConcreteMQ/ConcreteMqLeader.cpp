#include "../Logger.hpp"
#include "../LoggingSupport.hpp"
#include "MqTopology.hpp"
#include "ProcessCoordinatorMQ.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

struct ExperimentConfig {
    int initialPeers{0};
    std::string initialPeerType;
    nlohmann::json topology;
    int tests{1};
    int rounds{0};
    int doneTimeoutMs{30000};
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
 * Purpose: Read and validate one experiment's leader-side runtime settings.
 * Used by: `main()` before configuring the coordinator and starting that experiment.
 */
ExperimentConfig parseLeaderExp(const nlohmann::json &config, size_t expIndex) {
    const nlohmann::json experiment = config["experiments"].at(expIndex);
    if (!experiment.contains("topology"))
        throw std::runtime_error("error: experiment missing 'topology'");

    ExperimentConfig out;
    out.initialPeers = experiment["topology"].value("initialPeers", 0);
    out.initialPeerType = experiment["topology"].value("initialPeerType", "");
    out.topology = experiment["topology"];
    out.tests = experiment.value("tests", 1);
    out.rounds = experiment.value("rounds", 0);
    out.doneTimeoutMs = experiment.value("doneTimeoutMs", 30000);

    if (out.initialPeers <= 0) throw std::runtime_error("error: topology.initialPeers must be > 0");
    if (out.initialPeerType.empty())
        throw std::runtime_error("error: topology.initialPeerType is empty");
    if (out.tests <= 0) throw std::runtime_error("error: tests must be > 0");
    if (out.rounds <= 0) throw std::runtime_error("error: rounds must be > 0");
    if (out.doneTimeoutMs <= 0) throw std::runtime_error("error: doneTimeoutMs must be > 0");
    return out;
}

/*
 * Purpose: Load the input JSON and verify that it contains at least one experiment.
 * Used by: `main()` at startup so execution stops early when the input is unusable.
 */
std::optional<nlohmann::json> parseAndLoadConfig(int argc, char **argv) {
    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <input_json>\n";
        return std::nullopt;
    }

    try {
        std::ifstream inFile(argv[1]);
        if (!inFile.is_open())
            throw std::runtime_error(std::string("error: cannot open input file: ") + argv[1]);

        nlohmann::json config;
        inFile >> config;

        if (!config.contains("experiments") || !config["experiments"].is_array() ||
            config["experiments"].empty()) {
            throw std::runtime_error("error: configuration missing non-empty 'experiments' array");
        }

        return config;
    } catch (const std::exception &ex) {
        std::cerr << ex.what() << '\n';
        return std::nullopt;
    }
}

/*
 * Purpose: Create the experiment-level report metadata and an empty test list.
 * Used by: `main()` as the report object that receives every test result.
 */
nlohmann::json makeBaseExperimentReport(size_t expIndex, const ExperimentConfig &exp) {
    nlohmann::json report;
    report["backend"] = "mq";
    report["experimentIndex"] = expIndex;
    report["peerCount"] = exp.initialPeers;
    report["peerType"] = exp.initialPeerType;
    report["topologyType"] = exp.topology.value("type", "unknown");
    report["rounds"] = exp.rounds;
    report["doneTimeoutMs"] = exp.doneTimeoutMs;
    report["tests"] = nlohmann::json::array();

    return report;
}

/*
 * Purpose: Convert one completed test's timing, peer completion, and output paths into JSON.
 * Used by: `main()` before appending the test result to the experiment report.
 */
nlohmann::json makeTestReport(const TestReportInfo &info) {
    nlohmann::json testReport;
    testReport["testIndex"] = info.testIndex;
    testReport["durationSeconds"] = info.duration.count();
    testReport["completedPeers"] = info.completedPeers;
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
    for (int id = 0; id < totalPeers; ++id) { peers.push_back(id); }
    return peers;
}

/*
 * Purpose: Compare completed peer ids with the expected id range and return any missing peers.
 * Used by: `main()` to determine each test's completion status and failure evidence.
 */
std::vector<quantas::interfaceId>
findMissingPeers(int totalPeers, const std::vector<quantas::interfaceId> &completedPeers) {
    std::vector<bool> seen(static_cast<size_t>(totalPeers), false);
    for (quantas::interfaceId id : completedPeers) {
        if (id >= 0 && id < totalPeers) { seen[static_cast<size_t>(id)] = true; }
    }

    std::vector<quantas::interfaceId> missing;
    for (int id = 0; id < totalPeers; ++id) {
        if (!seen[static_cast<size_t>(id)]) { missing.push_back(id); }
    }
    return missing;
}

/*
 * Purpose: Derive the experiment-specific leader report destination from the configured log base.
 * Used by: `main()` before execution so the final report has a stable output path.
 */
std::string makeLeaderReportPath(const std::string &logFileBase, size_t expIndex) {
    if (logFileBase == "cout" || logFileBase == "cerr") { return logFileBase; }

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
 * Purpose: Map every expected peer id to the metrics file produced for one test.
 * Used by: `main()` to include peer output references in each test report.
 */
nlohmann::json makePeerOutputFiles(
    const std::string &logFileBase, size_t expIndex, int testNumber, int totalPeers
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
 * Purpose: Serialize the completed leader report to its configured file or standard stream.
 * Used by: `main()` after all available test results and experiment status are finalized.
 */
void writeLeaderReport(const std::string &path, const nlohmann::json &report) {
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
nlohmann::json readPeerMetricFile(const std::string &path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("error: cannot open peer metric file: " + path);
    }

    nlohmann::json metrics;
    input >> metrics;
    return metrics;
}

/*
 * Purpose: Copy completed peer metric files into the leader report without inventing merge rules.
 * Used by: `main()` before appending each test report.
 */
nlohmann::json readCompletedPeerMetrics(
    const nlohmann::json &peerOutputFiles, const std::vector<quantas::interfaceId> &completedPeers
) {
    nlohmann::json peerMetrics = nlohmann::json::object();

    for (quantas::interfaceId peerId : completedPeers) {
        const std::string key = std::to_string(peerId);
        const std::string path = peerOutputFiles.at(key).get<std::string>();
        if (path == "cout" || path == "cerr") { continue; }
        peerMetrics[key] = readPeerMetricFile(path);
    }

    return peerMetrics;
}

/* --------------------------- Leader runtime --------------------------- */
int main(int argc, char *argv[]) {
    /* Parse cli args and load the json config file for the leader process */
    auto config = parseAndLoadConfig(argc, argv);
    if (!config) return 1;

    /* ProcessCoordinatorMQ` is the component that owns the rendezvous protocol API
     * (`createBarrier`, `waitForAllReady`, `broadcastStart`, `configureExperiment`). */
    auto &coordinator = quantas::ProcessCoordinatorMQ::instance();

    /* TODO:  Leader records the start/end time of the whole simulation. How long it took to do the
     * whole thing? */

    for (size_t expIndex = 0; expIndex < (*config)["experiments"].size(); ++expIndex) {
        try {
            std::chrono::time_point<std::chrono::high_resolution_clock> expStartTime, expEndTime;
            std::chrono::duration<double> expDuration;
            expStartTime = std::chrono::high_resolution_clock::now();

            const nlohmann::json &experiment = (*config)["experiments"].at(expIndex);
            ExperimentConfig exp = parseLeaderExp(*config, expIndex);

            const std::string logFileBase = quantas::chooseLogFileBase(*config, experiment);
            const std::string reportPath = makeLeaderReportPath(logFileBase, expIndex);

            coordinator.configureExperiment(
                expIndex,
                exp.initialPeerType,
                true,
                exp.initialPeers,
                quantas::NO_PEER_ID,
                logFileBase,
                quantas::StopMode::FixedRounds
            );

            QUANTAS_LOG_INFO("coord")
                << "leader starting rendezvous for experiment " << expIndex
                << " with totalPeers=" << exp.initialPeers << " tests=" << exp.tests;

            nlohmann::json expReport = makeBaseExperimentReport(expIndex, exp);
            expReport["inputFile"] = argv[1];
            expReport["expectedPeers"] = expectedPeers(exp.initialPeers);
            bool allTestsSucceeded = true;
            bool experimentTimedOut = false;

            for (int testIndex = 0; testIndex < exp.tests; ++testIndex) {
                const int testNumber = testIndex + 1;
                TestReportInfo testInfo;
                testInfo.testIndex = static_cast<size_t>(testIndex);

                QUANTAS_LOG_INFO("coord")
                    << "leader starting experiment " << expIndex << " test " << testNumber;

                std::chrono::time_point<std::chrono::high_resolution_clock> testStartTime,
                    testEndTime;
                testStartTime = std::chrono::high_resolution_clock::now();

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

                expReport["tests"].push_back(testReport);

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

            expEndTime = std::chrono::high_resolution_clock::now();
            expDuration = expEndTime - expStartTime;
            expReport["durationSeconds"] = expDuration.count();
            expReport["success"] = allTestsSucceeded;
            writeLeaderReport(reportPath, expReport);
            QUANTAS_LOG_INFO("coord") << "leader report written to " << reportPath;

            if (experimentTimedOut) return 1;

        } catch (const std::exception &ex) {
            coordinator.cleanUp();
            std::cerr << "error: leader failed at experiment " << expIndex << ": " << ex.what()
                      << '\n';

            return 1;
        }
    }

    return 0;
}
