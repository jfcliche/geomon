#pragma once

#include <cstdint>
#include <vector>

#include "esphome/components/sensor/sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"

// Passive Modbus RTU decoder with a short active-read window.
//
// Frames are recovered from idle-delimited bursts because the ESPHome loop
// cannot see the 3.5-character gap. Sensors without an active flag publish
// only what the other master already reads. Active sensors are read after
// that master has been silent, and the driver-enable pin is raised only
// while those bytes are leaving.

namespace esphome {
namespace modbus_sniffer {

/** Modbus register space a sensor listens to. */
enum class RegisterType : uint8_t { HOLDING, INPUT };

/** Word layout of one sensor. The `_R` variants store the low word first. */
enum class ValueType : uint8_t { U16, S16, U32, S32, U32_R, S32_R, FLOAT32, FLOAT32_R };

/** One Home Assistant sensor bound to a Modbus register. */
class RegisterSensor : public sensor::Sensor {
 public:
  void set_slave(uint8_t slave) { this->slave_ = slave; }
  void set_register_type(RegisterType type) { this->register_type_ = type; }
  void set_address(uint16_t address) { this->address_ = address; }
  void set_value_type(ValueType type) { this->value_type_ = type; }
  void set_active(bool active) { this->active_ = active; }

  uint8_t get_slave() const { return this->slave_; }
  RegisterType get_register_type() const { return this->register_type_; }
  uint16_t get_address() const { return this->address_; }
  bool is_active() const { return this->active_; }
  /** Number of 16-bit registers this value occupies. */
  uint8_t word_count() const;

  /** Publish when every register of this value lies in `[start, start + count)`. */
  void handle_registers(uint16_t start, const uint16_t *words, uint16_t count);

 protected:
  uint8_t slave_{1};
  RegisterType register_type_{RegisterType::HOLDING};
  uint16_t address_{0};
  ValueType value_type_{ValueType::U16};
  bool active_{false};
};

/** Sniffs a Modbus RTU UART and, for sensors marked active, reads them in the quiet gap. */
class ModbusSniffer : public PollingComponent, public uart::UARTDevice {
 public:
  void setup() override;
  void loop() override;
  void update() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::DATA; }

  void set_log_frames(bool log_frames) { this->log_frames_ = log_frames; }
  void set_idle_timeout(uint32_t idle_timeout_ms) { this->idle_timeout_ms_ = idle_timeout_ms; }
  /** Silence, in ms, required after the other master before an active read starts. */
  void set_active_delay(uint32_t delay_ms) { this->active_delay_ms_ = delay_ms; }
  /** How long, in ms, active reads may hold the bus once they start. */
  void set_active_window(uint32_t window_ms) { this->active_window_ms_ = window_ms; }
  /** True when a driver-enable pin is configured. Active reads need it. */
  void set_driver_enable(bool enabled) { this->driver_enable_ = enabled; }
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

  // One active read. Adjacent active sensors on the same slave are merged.
  struct Probe {
    uint8_t slave;
    uint8_t function;
    uint16_t start;
    uint16_t count;
  };

  enum class ProbeState : uint8_t { IDLE, WAIT };

  // Tries to extract one frame from the front of buffer_. Returns false when more bytes are needed.
  bool parse_one_(bool idle);
  void handle_frame_(const uint8_t *frame, size_t len, bool is_response);
  void handle_request_(const uint8_t *frame, size_t len);
  void handle_response_(const uint8_t *frame, size_t len);
  void dispatch_(uint8_t slave, RegisterType type, uint16_t start, const uint8_t *data, uint16_t count);
  void consume_(size_t n);
  void discard_(size_t n);
  void log_sequence_(const uint8_t *data, size_t len);
  void build_probes_();
  void poll_active_(uint32_t now);
  void send_probe_();
  void finish_window_();
  void abort_window_(const char *reason);

  static bool crc_ok_(const uint8_t *data, size_t len);

  std::vector<uint8_t> buffer_;
  std::vector<RegisterSensor *> sensors_;
  std::vector<Probe> probes_;
  LastRequest last_request_;
  uint32_t last_byte_ms_{0};
  uint32_t last_foreign_ms_{0};
  uint32_t idle_timeout_ms_{50};
  uint32_t active_delay_ms_{5000};
  uint32_t active_window_ms_{30000};
  uint32_t window_start_ms_{0};
  uint32_t probe_sent_ms_{0};
  uint32_t discard_at_send_{0};
  size_t probe_index_{0};
  ProbeState probe_state_{ProbeState::IDLE};
  bool log_frames_{false};
  bool driver_enable_{false};
  bool armed_{true};
  bool window_open_{false};
  bool collision_{false};
  // A foreign request replaced last_request_ and its reply still has to be paired.
  bool keep_foreign_request_{false};

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
