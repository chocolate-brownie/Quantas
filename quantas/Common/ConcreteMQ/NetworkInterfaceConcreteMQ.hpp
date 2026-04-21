#ifndef NETWORK_INTERFACE_CONCRETE_MQ_HPP
#define NETWORK_INTERFACE_CONCRETE_MQ_HPP

#include "../../../quantas/Common/Json.hpp"
#include <boost/interprocess/creation_tags.hpp>
#include <boost/interprocess/interprocess_fwd.hpp>
#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/shared_memory_object.hpp>

using namespace boost::interprocess;
using nlohmann::json;

class NetworkInterfaceConcreteMQ {
  private:
};

#endif
