#include "sensorfw/core/types.hpp"

namespace sensorfw {

const char* toString(Status s) {
    switch (s) {
        case Status::Ok:              return "Ok";
        case Status::NotInitialised:  return "NotInitialised";
        case Status::NotReady:        return "NotReady";
        case Status::BusError:        return "BusError";
        case Status::Timeout:         return "Timeout";
        case Status::OutOfRange:      return "OutOfRange";
        case Status::InvalidArgument: return "InvalidArgument";
        case Status::NoSpace:         return "NoSpace";
    }
    return "Unknown";
}

} // namespace sensorfw
