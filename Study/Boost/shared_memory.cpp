
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <iostream>

using namespace boost::interprocess;

int main() {
    shared_memory_object shdmem{open_or_create, "Boost", read_write};
    shdmem.truncate(1024);
    std::cout << "shdmem obj: " << shdmem.get_name() << std::endl;

    offset_t size;
    if (shdmem.get_size(size))
        std::cout << std::dec << "shdmem obj [size]: "<< size << std::endl;

    mapped_region region1 {shdmem, read_write};
    std::cout << std::hex << "region1: [addr]: " << region1.get_address() << std::endl;
    std::cout << std::dec << "region1 [size]: " << region1.get_size() << std::endl;

    int *var1 = static_cast<int*>(region1.get_address());
    *var1 = 99;

    mapped_region region2 {shdmem, read_only};
    std::cout << std::hex << "region2: [addr]: " << region2.get_address() << std::endl;
    std::cout << std::dec << "region2 [size]: " << region2.get_size() << "\n\n";

    int *var2 = static_cast<int*>(region2.get_address());
    std::cout << *var2 << "\n";

    bool removed = shared_memory_object::remove("Boost");
    std::cout << std::boolalpha << removed << '\n';
}
