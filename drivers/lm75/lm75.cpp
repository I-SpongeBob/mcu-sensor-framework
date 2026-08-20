#include "lm75.hpp"

namespace sensorfw {
namespace drivers {

namespace {
const uint8_t kRegTemperature   = 0x00u;
const uint8_t kRegConfiguration = 0x01u;
/** 0.125 degC per LSB after the 11-bit right shift (LM75B, TMP75). */
const Real    kLsbToCelsius     = static_cast<Real>(0.125);
}

Lm75Sensor::Lm75Sensor(hal::II2cBus& bus,
                       hal::IClock& clock,
                       uint8_t address,
                       const char* name)
    : bus_(bus), clock_(clock), address_(address), initialised_(false) {
    info_.name        = name;
    info_.quantity    = Quantity::Temperature;
    info_.minValue    = static_cast<Real>(-55);
    info_.maxValue    = static_cast<Real>(125);
    /* One conversion takes up to ~100 ms in 11-bit mode; asking faster only
     * returns the same value twice and wastes bus bandwidth and power. */
    info_.minPeriodMs = 100u;
}

Status Lm75Sensor::init() {
    /* Write the configuration register to normal (continuous) mode. It doubles
     * as a probe: an absent or mis-strapped device NACKs here, at boot, rather
     * than producing plausible-looking garbage later. */
    const uint8_t frame[2] = { kRegConfiguration, 0x00u };
    const Status status = bus_.write(address_, frame, sizeof(frame));
    initialised_ = isOk(status);
    return status;
}

Status Lm75Sensor::read(Sample& out) {
    if (!initialised_) { return Status::NotInitialised; }

    const uint8_t reg = kRegTemperature;
    uint8_t rx[2] = { 0u, 0u };

    const Status status = bus_.writeRead(address_, &reg, 1u, rx, 2u);
    if (!isOk(status)) { return status; }

    out.value     = convert(rx[0], rx[1]);
    out.timestamp = clock_.nowMs();
    return Status::Ok;
}

Real Lm75Sensor::convert(uint8_t msb, uint8_t lsb) {
    /* The temperature is left aligned in a 16-bit word: 11 significant bits,
     * two-complement, the lower 5 bits are undefined and must be discarded. */
    const uint16_t word = static_cast<uint16_t>((static_cast<uint16_t>(msb) << 8) | lsb);
    uint16_t magnitude = static_cast<uint16_t>(word >> 5);

    /* Sign extend by hand: right shifting a negative signed integer is only
     * implementation defined in C++, and this driver has to build on whatever
     * compiler the target vendor ships. */
    if ((magnitude & 0x0400u) != 0u) {
        magnitude = static_cast<uint16_t>(magnitude | 0xF800u);
    }
    const int16_t raw = static_cast<int16_t>(magnitude);

    return static_cast<Real>(raw) * kLsbToCelsius;
}

} // namespace drivers
} // namespace sensorfw
