#include "modbus_sniffer.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome {
namespace modbus_sniffer {

static const char *const TAG = "modbus_sniffer";

// An RTU frame is at most 256 bytes: address + 253-byte PDU + 2-byte CRC.
static constexpr size_t MAX_FRAME = 256;
static constexpr size_t MIN_FRAME = 4;
static constexpr size_t MAX_REGISTERS = 125;
static constexpr size_t READ_CHUNK = 128;

static inline uint16_t be16(const uint8_t *p) { return (uint16_t(p[0]) << 8) | p[1]; }

// ---------------------------------------------------------------------------
// RegisterSensor

uint8_t RegisterSensor::word_count() const {
  switch (this->value_type_) {
    case ValueType::U16:
    case ValueType::S16:
      return 1;
    default:
      return 2;
  }
}

void RegisterSensor::handle_registers(uint16_t start, const uint16_t *words, uint16_t count) {
  const uint32_t first = this->address_;
  const uint32_t last = first + this->word_count();
  if (first < start || last > uint32_t(start) + count)
    return;
  const uint16_t *w = words + (first - start);

  float value;
  switch (this->value_type_) {
    case ValueType::U16:
      value = w[0];
      break;
    case ValueType::S16:
      value = int16_t(w[0]);
      break;
    case ValueType::U32:
      value = (uint32_t(w[0]) << 16) | w[1];
      break;
    case ValueType::S32:
      value = int32_t((uint32_t(w[0]) << 16) | w[1]);
      break;
    case ValueType::U32_R:
      value = (uint32_t(w[1]) << 16) | w[0];
      break;
    case ValueType::S32_R:
      value = int32_t((uint32_t(w[1]) << 16) | w[0]);
      break;
    case ValueType::FLOAT32:
    case ValueType::FLOAT32_R: {
      uint32_t raw = this->value_type_ == ValueType::FLOAT32 ? (uint32_t(w[0]) << 16) | w[1]
                                                              : (uint32_t(w[1]) << 16) | w[0];
      std::memcpy(&value, &raw, sizeof(value));
      break;
    }
    default:
      return;
  }
  this->publish_state(value);
}

// ---------------------------------------------------------------------------
// ModbusSniffer

void ModbusSniffer::setup() { this->buffer_.reserve(2 * MAX_FRAME); }

void ModbusSniffer::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Modbus RTU sniffer:\n"
                "  Log frames: %s\n"
                "  Idle timeout: %" PRIu32 " ms\n"
                "  Register sensors: %u",
                YESNO(this->log_frames_), this->idle_timeout_ms_, (unsigned) this->sensors_.size());
  LOG_UPDATE_INTERVAL(this);
  LOG_SENSOR("  ", "Frames", this->frames_sensor_);
  LOG_SENSOR("  ", "Discarded bytes", this->discarded_bytes_sensor_);
  LOG_SENSOR("  ", "Unmatched responses", this->unmatched_responses_sensor_);
  LOG_SENSOR("  ", "Exceptions", this->exceptions_sensor_);
  for (auto *s : this->sensors_) {
    LOG_SENSOR("  ", "Register", s);
    ESP_LOGCONFIG(TAG, "    Slave: %u, %s register 0x%04X", s->get_slave(),
                  s->get_register_type() == RegisterType::INPUT ? "input" : "holding", s->get_address());
  }
}

void ModbusSniffer::update() {
  if (this->frames_sensor_ != nullptr)
    this->frames_sensor_->publish_state(this->frames_);
  if (this->discarded_bytes_sensor_ != nullptr)
    this->discarded_bytes_sensor_->publish_state(this->discarded_bytes_);
  if (this->unmatched_responses_sensor_ != nullptr)
    this->unmatched_responses_sensor_->publish_state(this->unmatched_responses_);
  if (this->exceptions_sensor_ != nullptr)
    this->exceptions_sensor_->publish_state(this->exceptions_);
}

void ModbusSniffer::loop() {
  const uint32_t now = millis();
  size_t avail = this->available();
  if (avail > 0) {
    uint8_t chunk[READ_CHUNK];
    while (avail > 0) {
      size_t n = std::min(avail, READ_CHUNK);
      if (!this->read_array(chunk, n))
        break;
      this->buffer_.insert(this->buffer_.end(), chunk, chunk + n);
      avail -= n;
    }
    this->last_byte_ms_ = now;
  }

  // The ESPHome main loop is far too coarse to see the 3.5-character inter-frame gap, so frames are
  // delimited structurally (function-code length rules plus CRC). The idle timeout only flushes
  // bytes that can never form a valid frame.
  const bool idle = (now - this->last_byte_ms_) > this->idle_timeout_ms_;
  while (!this->buffer_.empty() && this->parse_one_(idle)) {
  }
}

bool ModbusSniffer::crc_ok_(const uint8_t *data, size_t len) {
  uint16_t crc = crc16(data, len - 2);
  return data[len - 2] == (crc & 0xFF) && data[len - 1] == (crc >> 8);
}

void ModbusSniffer::consume_(size_t n) {
  n = std::min(n, this->buffer_.size());
  this->buffer_.erase(this->buffer_.begin(), this->buffer_.begin() + n);
}

void ModbusSniffer::discard_(size_t n) {
  n = std::min(n, this->buffer_.size());
  this->discarded_bytes_ += n;
  this->consume_(n);
}

bool ModbusSniffer::parse_one_(bool idle) {
  const uint8_t *b = this->buffer_.data();
  const size_t n = this->buffer_.size();

  if (n < MIN_FRAME) {
    if (idle)
      this->discard_(n);
    return false;
  }

  const uint8_t slave = b[0];
  const uint8_t fc = b[1];
  if (slave > 247) {
    this->discard_(1);
    return true;
  }

  const bool expect_response = this->last_request_.valid && this->last_request_.slave == slave &&
                               this->last_request_.function == (fc & 0x7F);

  // Candidate frame lengths, most likely first. A length of 0 means the header bytes needed to
  // compute it have not arrived yet.
  struct Candidate {
    size_t len;
    bool is_response;
  };
  Candidate cands[2];
  size_t nc = 0;
  auto add_pair = [&](size_t req_len, size_t resp_len) {
    if (expect_response) {
      cands[nc++] = {resp_len, true};
      cands[nc++] = {req_len, false};
    } else {
      cands[nc++] = {req_len, false};
      cands[nc++] = {resp_len, true};
    }
  };

  if (fc & 0x80) {
    cands[nc++] = {5, true};
  } else {
    switch (fc) {
      case 0x01:
      case 0x02:
      case 0x03:
      case 0x04:
        add_pair(8, 5 + size_t(b[2]));
        break;
      case 0x05:
      case 0x06:
        // The response is an echo of the request.
        cands[nc++] = {8, expect_response};
        break;
      case 0x0F:
      case 0x10:
        add_pair(n > 6 ? 9 + size_t(b[6]) : 0, 8);
        break;
      case 0x17:
        add_pair(n > 10 ? 13 + size_t(b[10]) : 0, 5 + size_t(b[2]));
        break;
      default: {
        // Unknown function code: accept the shortest prefix with a valid CRC.
        const size_t limit = std::min(n, MAX_FRAME);
        for (size_t len = MIN_FRAME; len <= limit; len++) {
          if (crc_ok_(b, len)) {
            this->handle_frame_(b, len, expect_response);
            this->consume_(len);
            return true;
          }
        }
        if (n < MAX_FRAME && !idle)
          return false;
        this->discard_(1);
        return true;
      }
    }
  }

  bool need_more = false;
  for (size_t i = 0; i < nc; i++) {
    const size_t len = cands[i].len;
    if (len == 0) {
      need_more = true;
      continue;
    }
    if (len > MAX_FRAME)
      continue;
    if (len > n) {
      need_more = true;
      continue;
    }
    if (crc_ok_(b, len)) {
      this->handle_frame_(b, len, cands[i].is_response);
      this->consume_(len);
      return true;
    }
  }

  if (need_more && !idle)
    return false;
  this->discard_(1);
  return true;
}

void ModbusSniffer::handle_frame_(const uint8_t *frame, size_t len, bool is_response) {
  this->frames_++;

  if (this->log_frames_) {
    char hex[format_hex_pretty_size(MAX_FRAME)];
    format_hex_pretty_to(hex, frame, len, ' ');
    ESP_LOGD(TAG, "%s slave=%u fc=0x%02X len=%u: %s", is_response ? "RSP" : "REQ", frame[0], frame[1],
             (unsigned) len, hex);
  }

  if (frame[1] & 0x80) {
    this->exceptions_++;
    ESP_LOGW(TAG, "Exception from slave %u: function 0x%02X, code 0x%02X", frame[0], frame[1] & 0x7F, frame[2]);
    this->last_request_.valid = false;
    return;
  }

  if (is_response) {
    this->handle_response_(frame, len);
  } else {
    this->handle_request_(frame, len);
  }
}

void ModbusSniffer::handle_request_(const uint8_t *frame, size_t len) {
  const uint8_t slave = frame[0];
  const uint8_t fc = frame[1];

  LastRequest &req = this->last_request_;
  req.valid = slave != 0;  // broadcasts get no response
  req.slave = slave;
  req.function = fc;
  req.read_start = 0;
  req.read_count = 0;

  switch (fc) {
    case 0x01:
    case 0x02:
    case 0x03:
    case 0x04:
      req.read_start = be16(frame + 2);
      req.read_count = be16(frame + 4);
      break;
    case 0x06:
      this->dispatch_(slave, RegisterType::HOLDING, be16(frame + 2), frame + 4, 1);
      break;
    case 0x10: {
      const uint16_t start = be16(frame + 2);
      const uint16_t count = be16(frame + 4);
      if (frame[6] == 2 * count && len == 9 + size_t(frame[6]))
        this->dispatch_(slave, RegisterType::HOLDING, start, frame + 7, count);
      break;
    }
    case 0x17: {
      req.read_start = be16(frame + 2);
      req.read_count = be16(frame + 4);
      const uint16_t start = be16(frame + 6);
      const uint16_t count = be16(frame + 8);
      if (frame[10] == 2 * count && len == 13 + size_t(frame[10]))
        this->dispatch_(slave, RegisterType::HOLDING, start, frame + 11, count);
      break;
    }
    default:
      break;
  }
}

void ModbusSniffer::handle_response_(const uint8_t *frame, size_t len) {
  const uint8_t slave = frame[0];
  const uint8_t fc = frame[1];
  LastRequest &req = this->last_request_;

  if (!req.valid || req.slave != slave || req.function != fc) {
    this->unmatched_responses_++;
    return;
  }
  req.valid = false;

  if (fc == 0x03 || fc == 0x04 || fc == 0x17) {
    if (frame[2] != 2 * req.read_count || len != 5 + size_t(frame[2])) {
      this->unmatched_responses_++;
      return;
    }
    const RegisterType type = fc == 0x04 ? RegisterType::INPUT : RegisterType::HOLDING;
    this->dispatch_(slave, type, req.read_start, frame + 3, req.read_count);
  }
}

void ModbusSniffer::dispatch_(uint8_t slave, RegisterType type, uint16_t start, const uint8_t *data,
                              uint16_t count) {
  if (count == 0 || count > MAX_REGISTERS || this->sensors_.empty())
    return;
  uint16_t words[MAX_REGISTERS];
  for (uint16_t i = 0; i < count; i++)
    words[i] = be16(data + 2 * i);

  for (auto *s : this->sensors_) {
    if (s->get_slave() == slave && s->get_register_type() == type)
      s->handle_registers(start, words, count);
  }
}

}  // namespace modbus_sniffer
}  // namespace esphome
