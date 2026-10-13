#pragma once

#include <cstdint>
#include <vector>

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"

namespace esphome {
namespace modbus_sniffer {

enum class RegisterType : uint8_t { HOLDING, INPUT };

// The _R variants store the low word first (word-swapped).
enum class ValueType : uint8_t { U16, S16, U32, S32, U32_R, S32_R, FLOAT32, FLOAT32_R };

class RegisterSensor : public sensor::Sensor {
 public:
  void set_slave(uint8_t slave) { this->slave_ = slave; }
  void set_register_type(RegisterType type) { this->register_type_ = type; }
  void set_address(uint16_t address) { this->address_ = address; }
  void set_value_type(ValueType type) { this->value_type_ = type; }

  uint8_t get_slave() const { return this->slave_; }
  RegisterType get_register_type() const { return this->register_type_; }
  uint16_t get_address() const { return this->address_; }
  uint8_t word_count() const;

  // Publishes the value if all of its registers lie within [start, start + count).
  void handle_registers(uint16_t start, const uint16_t *words, uint16_t count);

 protected:
  uint8_t slave_{1};
  RegisterType register_type_{RegisterType::HOLDING};
  uint16_t address_{0};
  ValueType value_type_{ValueType::U16};
};

class ModbusSniffer : public PollingComponent, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_log_frames(bool log_frames) { this->log_frames_ = log_frames; }
  void set_idle_timeout(uint32_t idle_timeout_ms) { this->idle_timeout_ms_ = idle_timeout_ms; }
  void set_frames_sensor(sensor::Sensor *s) { this->frames_sensor_ = s; }
  void set_discarded_bytes_sensor(sensor::Sensor *s) { this->discarded_bytes_sensor_ = s; }
  void set_unmatched_responses_sensor(sensor::Sensor *s) { this->unmatched_responses_sensor_ = s; }
  void set_exceptions_sensor(sensor::Sensor *s) { this->exceptions_sensor_ = s; }
  void register_sensor(RegisterSensor *s) { this->sensors_.push_back(s); }

 protected:
  // The most recent request seen on the bus, used to pair it with the slave's response.
  struct LastRequest {
    bool valid{false};
    uint8_t slave{0};
    uint8_t function{0};
    uint16_t read_start{0};
    uint16_t read_count{0};
  };

  // Tries to extract one frame from the front of buffer_. Returns false when more bytes are needed.
  bool parse_one_(bool idle);
  void handle_frame_(const uint8_t *frame, size_t len, bool is_response);
  void handle_request_(const uint8_t *frame, size_t len);
  void handle_response_(const uint8_t *frame, size_t len);
  void dispatch_(uint8_t slave, RegisterType type, uint16_t start, const uint8_t *data, uint16_t count);
  void consume_(size_t n);
  void discard_(size_t n);

  static bool crc_ok_(const uint8_t *data, size_t len);

  std::vector<uint8_t> buffer_;
  std::vector<RegisterSensor *> sensors_;
  LastRequest last_request_;
  uint32_t last_byte_ms_{0};
  uint32_t idle_timeout_ms_{50};
  bool log_frames_{false};

  uint32_t frames_{0};
  uint32_t discarded_bytes_{0};
  uint32_t unmatched_responses_{0};
  uint32_t exceptions_{0};

  sensor::Sensor *frames_sensor_{nullptr};
  sensor::Sensor *discarded_bytes_sensor_{nullptr};
  sensor::Sensor *unmatched_responses_sensor_{nullptr};
  sensor::Sensor *exceptions_sensor_{nullptr};
};

}  // namespace modbus_sniffer
}  // namespace esphome
