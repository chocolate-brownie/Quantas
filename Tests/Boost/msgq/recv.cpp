
#include "../../../quantas/Common/Json.hpp"
#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/interprocess_fwd.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <iostream>

using namespace boost::interprocess;
using nlohmann::json;

// receiving -> [Boost MQ] → raw bytes → std::string → json::parse() → json
// object

int main() {
    try {
        std::cout << "Step 1: opening queue..." << std::endl;
        message_queue mq(open_only, "message_queue");

        std::cout << "Step 2: receiving json raw bytes" << std::endl;
        std::vector<char> buffer(1024);

        unsigned int prior;
        std::size_t recvd_size;

        mq.receive(buffer.data(), buffer.size(), recvd_size, prior);
        std::cout << "received: " << recvd_size << '\n';

        std::cout << "Parsing Json: " << '\n';
        try {
            json j = json::parse(buffer.data(), buffer.data() + recvd_size);
            std::cout << j.dump(2) << '\n';
        } catch (json::parse_error &e) {
            std::cout << e.what() << std::endl;
        }

        std::cout << "Done." << std::endl;
    } catch (interprocess_exception &ex) {
        std::cout << "Exception caught: " << ex.what() << std::endl;
        std::cout << "Error code: " << ex.get_error_code() << std::endl;
        std::cout << "Native error: " << ex.get_native_error() << std::endl;
        message_queue::remove("message_queue");
        return 1;
    }

    message_queue::remove("message_queue");
    return 0;
}
