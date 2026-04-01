#ifndef OutputWriter_hpp
#define OutputWriter_hpp

#include <string>
#include <fstream>
#include <iostream>
#include <filesystem>
#include <mutex>
#include "Json.hpp"

namespace quantas {

    using nlohmann::json;

    class OutputWriter {
    public:
        static OutputWriter* instance() {
            static OutputWriter s;
            return &s;
        }

        // Set log file path and open stream
        static void setLogFile(const std::string& path) {
            OutputWriter* inst = instance();
            std::lock_guard<std::mutex> lock(inst->_mutex);

            if (inst->_file_stream.is_open()) {
                inst->_file_stream.close();
            }

            if (path == "cout") {
                inst->_log_stream = &std::cout;
                return;
            }
            if (path == "cerr") {
                inst->_log_stream = &std::cerr;
                return;
            }
            std::filesystem::path destination(path);
            if (destination.has_parent_path()) {
                std::error_code ec;
                std::filesystem::create_directories(destination.parent_path(), ec);
            }
            inst->_file_stream.open(path);

            if (inst->_file_stream.is_open()) {
                inst->_log_stream = &inst->_file_stream;
            } else {
                std::cerr << "[OutputWriter] Failed to open log file: " << path << ". Falling back to std::cout.\n";
                inst->_log_stream = &std::cout;
            }
        }

        static void print() {
            OutputWriter* inst = instance();
            std::lock_guard<std::mutex> lock(inst->_mutex);
            if (inst->_log_stream != nullptr) {
                (*inst->_log_stream) << inst->data.dump(4) << std::endl;
                inst->_log_stream->flush();
            }
            inst->data.clear();
            if (inst->_file_stream.is_open()) {
                inst->_file_stream.close();
            }
            inst->_log_stream = nullptr;
        }

        static void setTest(int test) {
            OutputWriter* inst = instance();
            std::lock_guard<std::mutex> lock(inst->_mutex);
            inst->_test = test;
        }

        static int getTest() {
            OutputWriter* inst = instance();
            std::lock_guard<std::mutex> lock(inst->_mutex);
            return inst->_test;
        }

        template <typename T>
        static void pushValue(const std::string& key, const T& val) {
            OutputWriter* inst = instance();
            std::lock_guard<std::mutex> lock(inst->_mutex);
            inst->data["tests"][inst->_test][key].push_back(val);
        }

        template <typename T>
        static void setValue(const std::string& key, const T& val) {
            OutputWriter* inst = instance();
            std::lock_guard<std::mutex> lock(inst->_mutex);
            inst->data[key] = val;
        }

    private:
        std::ofstream _file_stream;
        std::ostream* _log_stream = nullptr;
        int _test = 0;
        json data;
        mutable std::mutex _mutex;

        // disallow copies
        OutputWriter() = default;
        OutputWriter(const OutputWriter&) = delete;
        OutputWriter& operator=(const OutputWriter&) = delete;
    };

} // namespace quantas

#endif // OutputWriter_hpp
