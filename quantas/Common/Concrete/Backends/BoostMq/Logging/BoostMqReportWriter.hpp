#ifndef QUANTAS_BOOST_MQ_REPORT_WRITER_HPP
#define QUANTAS_BOOST_MQ_REPORT_WRITER_HPP

#include "quantas/Common/Concrete/Backends/BoostMq/Control/QueueConfig.hpp"
#include "quantas/Common/Concrete/Runtime/Config/RuntimeConfig.hpp"
#include "quantas/Common/Json.hpp"
#include <chrono>
#include <string>
#include <vector>

namespace quantas {

/* Build and write the BoostMQ leader report. All operations are static because the writer does not
 * keep any hidden state between experiments. */
class BoostMqReportWriter {
  public:
    /* Report data: Store all facts collected for one test before they are written to JSON. */
    struct TestReportInfo {
        size_t testIndex{0};
        std::chrono::duration<double> duration{};
        std::vector<interfaceId> completedPeers;
        std::vector<interfaceId> missingPeers;
        std::vector<interfaceId> readyPeers;
        nlohmann::json peerOutputFiles = nlohmann::json::object();
        nlohmann::json peerMetrics = nlohmann::json::object();
        nlohmann::json transportReliability = nlohmann::json::object();
        bool completionTimedOut{false};
        bool readyTimedOut{false};
        bool success{false};
    };

    /* Main report operations: Build the experiment and test JSON sections. */
    static nlohmann::json makeBaseExperimentReport(size_t expIndex,
                                                   const RuntimeExperimentConfig& exp,
                                                   const BoostMqQueueConfig& queueConfig);
    static nlohmann::json makeTestReport(const TestReportInfo& info);

    /* Utility operations: Find expected or missing peers and build paths. */
    static std::vector<interfaceId> expectedPeers(int totalPeers);
    static std::vector<interfaceId>
    findMissingPeers(int totalPeers, const std::vector<interfaceId>& completedPeers);
    static std::string makeLeaderReportPath(const std::string& logFileBase, size_t expIndex);
    static nlohmann::json makePeerOutputFiles(const std::string& logFileBase, size_t expIndex,
                                              int testNumber, int totalPeers);

    /* Main output operation: Write the completed leader report. */
    static void writeLeaderReport(const std::string& path, const nlohmann::json& report);

    /* Metrics operations: Read peer metrics and build the transport summary. */
    static bool readPeerMetrics(const nlohmann::json& peerOutputFiles,
                                const std::vector<interfaceId>& peerIds,
                                nlohmann::json& peerMetrics);
    static nlohmann::json summarizeTransportReliability(const nlohmann::json& peerMetrics);

  private:
    /* Utility: Read the JSON metrics written by one peer. */
    static bool readPeerMetricFile(const std::string& path, nlohmann::json& peerMetrics);
};

} // namespace quantas

#endif
