#pragma once

#include <cstddef>
#include <cstdint>

namespace esphome {
namespace uart {

class UARTDevice {
 public:
  bool read_array(uint8_t *data, size_t len);
  void write_array(const uint8_t *data, size_t len);
  int available();
  void flush();
};

}  // namespace uart
}  // namespace esphome
