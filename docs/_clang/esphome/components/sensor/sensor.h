#pragma once

namespace esphome {
namespace sensor {

class Sensor {
 public:
  void publish_state(float state) { (void)state; }
};

}  // namespace sensor
}  // namespace esphome
