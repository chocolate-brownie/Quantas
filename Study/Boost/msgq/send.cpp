#include "../../../quantas/Common/Json.hpp"
#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/interprocess_fwd.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <iostream>

using namespace boost::interprocess;
using nlohmann::json;

// sending -> json object → .dump() → std::string → .data()/.size() → raw bytes
int main() {
    try {
        std::cout << "Step 1: removing any stale queue..." << std::endl;
        message_queue::remove("message_queue");

        std::cout << "Step 2: Create Json" << std::endl;
        json j = {
            {"from_id", 1},
            {"body",
            {{"action", "data"}, {"messageNum", 5}, {"roundSubmitted", 12}}}
        };

        std::cout << "Step 3: creating queue..." << std::endl;
        message_queue mq(create_only, "message_queue", 10, 1024);

        std::string jsn_str = j.dump();
        mq.send(jsn_str.data(), jsn_str.size(), 0);

        std::cout << "Send " << jsn_str.size() << " bytes." << '\n';

        std::cout << "Done." << std::endl;
    } catch (interprocess_exception &ex) {
        std::cout << "Exception caught: " << ex.what() << std::endl;
        std::cout << "Error code: " << ex.get_error_code() << std::endl;
        std::cout << "Native error: " << ex.get_native_error() << std::endl;
        return 1;
    }
    return 0;
}
