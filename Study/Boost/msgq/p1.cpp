#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/interprocess_fwd.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <iostream>

using namespace boost::interprocess;

int main() {
    try {
        std::cout << "Step 1: removing any stale queue..." << std::endl;
        message_queue::remove("message_queue");

        std::cout << "Step 2: creating queue..." << std::endl;
        message_queue mq(create_only, "message_queue", 10, sizeof(int));

        std::cout << "Step 3: sending 10 numbers..." << std::endl;
        for (int i = 0; i < 10; ++i) {
            mq.send(&i, sizeof(i), 0);
            std::cout << "  sent: " << i << std::endl;
        }
        std::cout << "Done." << std::endl;
    } catch (interprocess_exception &ex) {
        std::cout << "Exception caught: " << ex.what() << std::endl;
        std::cout << "Error code: " << ex.get_error_code() << std::endl;
        std::cout << "Native error: " << ex.get_native_error() << std::endl;
        return 1;
    }
    return 0;
}
