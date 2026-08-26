#include "BoostMqReportWriter.hpp"
#include "BoostMqOutputPaths.hpp"
#include "quantas/Common/Logger.hpp"
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace quantas {

/* Utility: Read the JSON metrics written by one peer. Stop with a clear error when the expected
 * file cannot be opened. */
bool BoostMqReportWriter::readPeerMetricFile(const std::string& path, nlohmann::json& peerMetrics) {
    if (path.empty()) {
        QUANTAS_LOG_ERROR("report") << "peer metrics path is empty";
        return false;
    }

    std::ifstream input(path);

    if (!input) {
        QUANTAS_LOG_ERROR("report") << "cannot open peer metrics file: " << path;
        return false;
    }

    try {
        input >> peerMetrics;
    } catch (const nlohmann::json::parse_error& ex) {
        QUANTAS_LOG_ERROR("report")
            << "invalid JSON in peer metrics file " << path << ": " << ex.what();
        return false;
    } catch (const nlohmann::json::exception& ex) {
        QUANTAS_LOG_ERROR("report")
            << "cannot read peer metrics file " << path << ": " << ex.what();
        return false;
    }

    if (input.bad()) {
        QUANTAS_LOG_ERROR("report") << "I/O error while reading peer metrics file: " << path;
        return false;
    }

    if (!peerMetrics.is_object()) {
        QUANTAS_LOG_ERROR("report") << "peer metrics file must contain a JSON object: " << path;
        return false;
    }

    return true;
}
/* Main report operation: Create the basic experiment report before its tests run. The test results
 * are added later by the leader. */
nlohmann::json
BoostMqReportWriter::makeBaseExperimentReport(size_t expIndex, const RuntimeExperimentConfig& exp,
                                              const BoostMqQueueConfig& queueConfig) {
    nlohmann::json report;
    report["backend"] = "mq";
    report["experimentIndex"] = expIndex;
    report["peerType"] = exp.initialPeerType;
    report["peerCount"] = exp.initialPeers;
    report["topologyType"] = exp.topology.value("type", "unknown");
    report["rounds"] = exp.rounds;
    report["testCount"] = exp.tests;

    report["doneTimeoutMs"] = exp.doneTimeoutMs;

    report["boostMq"] = {{"controlQueueCapacity", queueConfig.controlQueueCapacity},
                         {"dataQueueCapacity", queueConfig.dataQueueCapacity},
                         {"maxMessageSizeBytes", queueConfig.maxMessageSizeBytes},
                         {"readyTimeoutMs", queueConfig.readyTimeoutMs},
                         {"dataSendTimeoutMs", queueConfig.dataSendTimeoutMs}};

    report["tests"] = nlohmann::json::array();
    return report;
}

/* Main report operation: Convert the facts collected for one test into JSON. */
nlohmann::json BoostMqReportWriter::makeTestReport(const TestReportInfo& info) {
    nlohmann::json testReport;
    testReport["testIndex"] = info.testIndex;
    testReport["success"] = info.success;
    testReport["timedOut"] = info.readyTimedOut || info.completionTimedOut;
    testReport["durationSeconds"] = info.duration.count();

    testReport["readyPeers"] = info.readyPeers;
    testReport["completedPeers"] = info.completedPeers;
    testReport["missingPeers"] = info.missingPeers;

    testReport["peerOutputFiles"] = info.peerOutputFiles;
    testReport["transportReliability"] = info.transportReliability;

    testReport["completedPeerCount"] = info.completedPeers.size();
    testReport["peerMetrics"] = info.peerMetrics;
    return testReport;
}

/* Utility: Build the complete list of peer IDs expected in an experiment. */
std::vector<interfaceId> BoostMqReportWriter::expectedPeers(int totalPeers) {
    std::vector<interfaceId> peers;
    peers.reserve(static_cast<size_t>(totalPeers));
    for (int id = 0; id < totalPeers; ++id) {
        peers.push_back(id);
    }
    return peers;
}

/* Utility: Compare observed peer IDs with the expected range and return the IDs that are
 * missing. */
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

/* Utility: Build the output path for one experiment's leader report. */
std::string BoostMqReportWriter::makeLeaderReportPath(const std::string& logFileBase,
                                                      size_t expIndex) {
    return makeBoostMqLeaderReportPath(logFileBase, expIndex);
}

/* Utility: Map every peer ID to the metrics file expected for one test. */
nlohmann::json BoostMqReportWriter::makePeerOutputFiles(const std::string& logFileBase,
                                                        size_t expIndex, int testNumber,
                                                        int totalPeers) {
    nlohmann::json outputFiles = nlohmann::json::object();

    for (int peerId = 0; peerId < totalPeers; ++peerId) {
        outputFiles[std::to_string(peerId)] =
            makeBoostMqPeerOutputPath(logFileBase, expIndex, peerId, testNumber);
    }
    return outputFiles;
}

/* Main output operation: Write the final leader report to a file, stdout, or stderr according to
 * the configured path. */
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

/* Main metrics operation: Read metrics for the supplied peer IDs. This can include a failed peer
 * that wrote its final transport counters before exit. */
bool BoostMqReportWriter::readPeerMetrics(const nlohmann::json& peerOutputFiles,
                                          const std::vector<interfaceId>& peerIds,
                                          nlohmann::json& peerMetrics) {
    bool allOutputsValid = true;
    peerMetrics = nlohmann::json::object();

    for (interfaceId peerId : peerIds) {
        const std::string key = std::to_string(peerId);

        if (!peerOutputFiles.contains(key) || !peerOutputFiles.at(key).is_string()) {
            QUANTAS_LOG_ERROR("report") << "missing output path for peer " << peerId;
            allOutputsValid = false;
            continue;
        }

        const std::string path = peerOutputFiles.at(key).get<std::string>();
        if (path == "cout" || path == "cerr")
            continue;

        nlohmann::json metrics;
        if (!readPeerMetricFile(path, metrics)) {
            allOutputsValid = false;
            continue;
        }

        peerMetrics[key] = std::move(metrics);
    }

    return allOutputsValid;
}

/* Main metrics operation: Combine peer transport counters into one simple reliability summary for
 * the test. */
nlohmann::json
BoostMqReportWriter::summarizeTransportReliability(const nlohmann::json& peerMetrics) {
    uint64_t sentTotal = 0;
    uint64_t receivedRawTotal = 0;
    uint64_t deliveredToInstreamTotal = 0;
    uint64_t droppedBackpressureTotal = 0;

    for (const auto& [peerId, metrics] : peerMetrics.items()) {
        if (!metrics.contains("transportMetrics"))
            continue;

        const auto& transportMetrics = metrics["transportMetrics"];
        sentTotal += transportMetrics.value("sent", 0);
        receivedRawTotal += transportMetrics.value("received_raw", 0);
        deliveredToInstreamTotal += transportMetrics.value("delivered_to_instream", 0);
        droppedBackpressureTotal += transportMetrics.value("dropped_backpressure", 0);
    }

    const uint64_t pendingAtShutdownTotal =
        sentTotal > receivedRawTotal ? sentTotal - receivedRawTotal : 0;
    const bool reliable =
        droppedBackpressureTotal == 0 && receivedRawTotal == deliveredToInstreamTotal;

    return nlohmann::json{{"sent_total", sentTotal},
                          {"received_raw_total", receivedRawTotal},
                          {"delivered_to_instream_total", deliveredToInstreamTotal},
                          {"dropped_backpressure_total", droppedBackpressureTotal},
                          {"pending_at_shutdown_total", pendingAtShutdownTotal},
                          {"reliable", reliable}};
}

} // namespace quantas
