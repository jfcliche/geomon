#pragma once

namespace esphome {

class Component {
 public:
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0; }
};

class PollingComponent : public Component {
 public:
  virtual void update() {}
};

namespace setup_priority {
inline constexpr float DATA = 0;
}

}  // namespace esphome
