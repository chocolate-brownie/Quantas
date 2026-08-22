#include "BoostMqReportWriter.hpp"
#include "quantas/Common/LoggingSupport.hpp"
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace quantas {
/*
 * Utility: Read the JSON metrics written by one peer. Stop with a clear error
 * when the expected file cannot be opened.
 */
nlohmann::json BoostMqReportWriter::readPeerMetricFile(const std::string& path) {
    std::ifstream input(path);
    if (!input.is_open()) {
        throw std::runtime_error("error: cannot open peer metric file: " + path);
    }

    nlohmann::json metrics;
    input >> metrics;
    return metrics;
}
/*
 * Main report operation: Create the basic experiment report before its tests
 * run. The test results are added later by the leader.
 */
nlohmann::json
BoostMqReportWriter::makeBaseExperimentReport(size_t expIndex, const RuntimeExperimentConfig& exp,
                                              const BoostMqQueueConfig& queueConfig) {
    nlohmann::json report;
    report["backend"] = "mq";
    report["experimentIndex"] = expIndex;
    report["peerCount"] = exp.initialPeers;
    report["peerType"] = exp.initialPeerType;
    report["topologyType"] = exp.topology.value("type", "unknown");
    report["rounds"] = exp.rounds;
    report["testCount"] = exp.tests;
    report["doneTimeoutMs"] = exp.doneTimeoutMs;
    report["boostMq"] = {{"controlQueueCapacity", queueConfig.controlQueueCapacity},
                         {"dataQueueCapacity", queueConfig.dataQueueCapacity},
                         {"maxMessageSizeBytes", queueConfig.maxMessageSizeBytes},
                         {"readyTimeoutMs", queueConfig.readyTimeoutMs}};
    report["tests"] = nlohmann::json::array();
    return report;
}

/*
 * Main report operation: Convert the facts collected for one test into JSON.
 */
nlohmann::json BoostMqReportWriter::makeTestReport(const TestReportInfo& info) {
    nlohmann::json testReport;
    testReport["testIndex"] = info.testIndex;
    testReport["durationSeconds"] = info.duration.count();
    testReport["completedPeers"] = info.completedPeers;
    testReport["completedPeerCount"] = info.completedPeers.size();
    testReport["readyPeers"] = info.readyPeers;
    testReport["missingPeers"] = info.missingPeers;
    testReport["timedOut"] = info.readyTimedOut || info.completionTimedOut;
    testReport["success"] = info.success;
    testReport["peerOutputFiles"] = info.peerOutputFiles;
    testReport["peerMetrics"] = info.peerMetrics;
    testReport["transportReliability"] = info.transportReliability;
    return testReport;
}

/*
 * Utility: Build the complete list of peer IDs expected in an experiment.
 */
std::vector<interfaceId> BoostMqReportWriter::expectedPeers(int totalPeers) {
    std::vector<interfaceId> peers;
    peers.reserve(static_cast<size_t>(totalPeers));
    for (int id = 0; id < totalPeers; ++id) {
        peers.push_back(id);
    }
    return peers;
}

/*
 * Utility: Compare observed peer IDs with the expected range and return the
 * IDs that are missing.
 */
std::vector<interfaceId>
BoostMqReportWriter::findMissingPeers(int totalPeers,
                                      const std::vector<interfaceId>& completedPeers) {
    std::vector<bool> seen(static_cast<size_t>(totalPeers), false);
    for (interfaceId id : completedPeers) {
        if (id >= 0 && id < totalPeers) {
            seen[static_cast<size_t>(id)] = true;
        }
    }

    std::vector<interfaceId> missing;
    for (int id = 0; id < totalPeers; ++id) {
        if (!seen[static_cast<size_t>(id)]) {
            missing.push_back(id);
        }
    }
    return missing;
}

/*
 * Utility: Build the output path for one experiment's leader report.
 */
std::string BoostMqReportWriter::makeLeaderReportPath(const std::string& logFileBase,
                                                      size_t expIndex) {
    if (logFileBase == "cout" || logFileBase == "cerr") {
        return logFileBase;
    }

    std::filesystem::path reportBase(logFileBase);
    reportBase.replace_extension(".json");
    const std::string experimentFile =
        makeExperimentFileName(reportBase.string(), expIndex, std::nullopt, ".json");
    return addFileNameSuffix(experimentFile, "_leader_report");
}

/*
 * Utility: Map every peer ID to the metrics file expected for one test.
 */
nlohmann::json BoostMqReportWriter::makePeerOutputFiles(const std::string& logFileBase,
                                                        size_t expIndex, int testNumber,
                                                        int totalPeers) {
    nlohmann::json outputFiles = nlohmann::json::object();
    for (int peerId = 0; peerId < totalPeers; ++peerId) {
        const std::string experimentFile =
            makeExperimentFileName(logFileBase, expIndex, peerId, ".json");
        outputFiles[std::to_string(peerId)] =
            addFileNameSuffix(experimentFile, "_TEST" + std::to_string(testNumber));
    }
    return outputFiles;
}

/*
 * Main output operation: Write the final leader report to a file, stdout, or
 * stderr according to the configured path.
 */
void BoostMqReportWriter::writeLeaderReport(const std::string& path, const nlohmann::json& report) {
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
 * Main metrics operation: Read metrics only for peers that completed the test.
 */
nlohmann::json
BoostMqReportWriter::readCompletedPeerMetrics(const nlohmann::json& peerOutputFiles,
                                              const std::vector<interfaceId>& completedPeers) {
    nlohmann::json peerMetrics = nlohmann::json::object();
    for (interfaceId peerId : completedPeers) {
        const std::string key = std::to_string(peerId);
        const std::string path = peerOutputFiles.at(key).get<std::string>();
        if (path == "cout" || path == "cerr") {
            continue;
        }
        peerMetrics[key] = readPeerMetricFile(path);
    }
    return peerMetrics;
}

/*
 * Main metrics operation: Combine peer transport counters into one simple
 * reliability summary for the test.
 */
nlohmann::json
BoostMqReportWriter::summarizeTransportReliability(const nlohmann::json& peerMetrics) {
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
    return nlohmann::json{{"sent_total", sentTotal},
                          {"received_raw_total", receivedRawTotal},
                          {"delivered_to_instream_total", deliveredToInstreamTotal},
                          {"dropped_backpressure_total", droppedBackpressureTotal},
                          {"reliable", reliable}};
}

} // namespace quantas
