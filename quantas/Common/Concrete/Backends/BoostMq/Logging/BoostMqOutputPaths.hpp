#ifndef QUANTAS_BOOST_MQ_OUTPUT_PATHS_HPP
#define QUANTAS_BOOST_MQ_OUTPUT_PATHS_HPP

#include <filesystem>
#include <stdexcept>
#include <string>

namespace quantas {

/* Build the shared results folder used by the leader and every peer in one experiment. */
inline std::filesystem::path makeBoostMqExperimentDirectory(const std::string& logFileBase,
                                                            size_t experimentIndex) {
    std::filesystem::path configuredPath(logFileBase);
    std::string experimentName = configuredPath.stem().string();
    if (experimentName.empty()) {
        experimentName = "experiment";
    }

    experimentName += "_EXP" + std::to_string(experimentIndex + 1);
    const std::filesystem::path directory = std::filesystem::path("results") / experimentName;

    std::error_code error;
    std::filesystem::create_directories(directory, error);
    if (error) {
        throw std::runtime_error("cannot create BoostMQ results directory " + directory.string() +
                                 ": " + error.message());
    }
    return directory;
}

/* Build the final JSON report path for one experiment. */
inline std::string makeBoostMqLeaderReportPath(const std::string& logFileBase,
                                               size_t experimentIndex) {
    if (logFileBase == "cout" || logFileBase == "cerr") {
        return logFileBase;
    }
    return (makeBoostMqExperimentDirectory(logFileBase, experimentIndex) / "leader_report.json")
        .string();
}

/* Build the raw output path written by one peer for one test. */
inline std::string makeBoostMqPeerOutputPath(const std::string& logFileBase, size_t experimentIndex,
                                             int peerId, int testNumber) {
    if (logFileBase == "cout" || logFileBase == "cerr") {
        return logFileBase;
    }

    std::string extension = std::filesystem::path(logFileBase).extension().string();
    if (extension.empty()) {
        extension = ".json";
    }

    const std::string fileName =
        "peer_" + std::to_string(peerId) + "_TEST" + std::to_string(testNumber) + extension;
    return (makeBoostMqExperimentDirectory(logFileBase, experimentIndex) / fileName).string();
}

} // namespace quantas

#endif
