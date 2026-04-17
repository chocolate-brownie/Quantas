
#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/interprocess_fwd.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <iostream>

using namespace boost::interprocess;

int main() {
    try {
        std::cout << "Step 1: opening queue..." << std::endl;
        message_queue mq(open_only, "message_queue");

        unsigned int priority;
        message_queue::size_type recvd_size;

        std::cout << "Step 2: receiving 10 numbers..." << std::endl;
        for (int i = 0; i < 10; ++i) {
            int numbers;
            mq.receive(&numbers, sizeof(numbers), recvd_size, priority);
            std::cout << "  received: " << numbers << std::endl;
            if (numbers != i || recvd_size != sizeof(numbers)) {
                std::cout << "  mismatch! expected " << i << ", got " << numbers << std::endl;
                return 1;
            }
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
