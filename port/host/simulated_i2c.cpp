#include "simulated_i2c.hpp"

#include "port/host/scenario.hpp"

namespace sensorfw {
namespace port {
namespace host {

SimulatedLm75Bus::SimulatedLm75Bus(hal::IClock& clock, uint8_t address)
    : clock_(clock),
      address_(address),
      pointer_(0u),
      present_(true),
      configured_(false),
      transferCount_(0u) {}

Status SimulatedLm75Bus::write(uint8_t deviceAddr, const uint8_t* data, uint8_t length) {
    if (!present_ || deviceAddr != address_) { return Status::BusError; }
    if (data == 0 || length == 0u) { return Status::InvalidArgument; }

    ++transferCount_;
    pointer_ = data[0];
    if (pointer_ == 0x01u && length >= 2u) { configured_ = true; }
    return Status::Ok;
}

Status SimulatedLm75Bus::writeRead(uint8_t deviceAddr,
                                   const uint8_t* txData, uint8_t txLength,
                                   uint8_t* rxData, uint8_t rxLength) {
    if (!present_ || deviceAddr != address_) { return Status::BusError; }
    if (txData == 0 || txLength == 0u || rxData == 0 || rxLength < 2u) {
        return Status::InvalidArgument;
    }
    if (txData[0] != 0x00u) { return Status::InvalidArgument; }

    ++transferCount_;

    const Real value = TemperatureScenario::trueValueAt(clock_.nowMs());

    /* Encode exactly like the silicon: 11-bit two-complement, 0.125 degC/LSB,
     * left aligned in a 16-bit big-endian word. */
    const int16_t ticks = static_cast<int16_t>(value / static_cast<Real>(0.125));
    const uint16_t word = static_cast<uint16_t>(static_cast<uint16_t>(ticks) << 5);

    rxData[0] = static_cast<uint8_t>(word >> 8);
    rxData[1] = static_cast<uint8_t>(word & 0xFFu);
    return Status::Ok;
}

} // namespace host
} // namespace port
} // namespace sensorfw
