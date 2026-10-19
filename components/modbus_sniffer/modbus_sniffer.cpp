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
// A slave that is going to answer does so in well under this. Longer means the bus was taken.
static constexpr uint32_t PROBE_TIMEOUT_MS = 400;

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

void ModbusSniffer::setup() {
  this->buffer_.reserve(2 * MAX_FRAME);
  this->last_foreign_ms_ = millis();
  this->build_probes_();
}

void ModbusSniffer::dump_config() {
  ESP_LOGCONFIG(TAG,
                "Modbus RTU sniffer:\n"
                "  Log frames: %s\n"
                "  Idle timeout: %" PRIu32 " ms\n"
                "  Register sensors: %u\n"
                "  Active reads: %u, %" PRIu32 " ms after the master stops, for %" PRIu32 " ms\n"
                "  Driver enable: %s",
                YESNO(this->log_frames_), this->idle_timeout_ms_, (unsigned) this->sensors_.size(),
                (unsigned) this->probes_.size(), this->active_delay_ms_, this->active_window_ms_,
                YESNO(this->driver_enable_));
  if (!this->probes_.empty() && !this->driver_enable_) {
    ESP_LOGW(TAG, "Active reads need rs485_txen_pin set to the module DE pin");
  }
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

void ModbusSniffer::log_sequence_(const uint8_t *data, size_t len) {
  // 32 bytes per line keeps each log record short enough for the API log link.
  static constexpr size_t CHUNK = 32;
  char hex[format_hex_pretty_size(CHUNK)];
  ESP_LOGI(TAG, "RX %u bytes", (unsigned) len);
  for (size_t off = 0; off < len; off += CHUNK) {
    const size_t n = std::min(CHUNK, len - off);
    format_hex_pretty_to(hex, data + off, n, ' ');
    ESP_LOGI(TAG, "  %s", hex);
  }
}

void ModbusSniffer::loop() {
  const uint32_t now = millis();
  bool got_bytes = false;
  size_t avail = this->available();
  if (avail > 0) {
    uint8_t chunk[READ_CHUNK];
    while (avail > 0) {
      size_t n = std::min(avail, READ_CHUNK);
      if (!this->read_array(chunk, n))
        break;
      this->buffer_.insert(this->buffer_.end(), chunk, chunk + n);
      avail -= n;
      got_bytes = true;
    }
    this->last_byte_ms_ = now;
  }

  // Bytes that arrive while we are not waiting for our own reply belong to the other master.
  // A gap inside its sweep is about 200 ms, so 5 s of silence means the sweep is over.
  if (got_bytes && this->probe_state_ != ProbeState::WAIT) {
    this->last_foreign_ms_ = now;
    this->armed_ = true;
    if (this->window_open_) {
      this->abort_window_("master resumed");
    }
  }

  if (!this->buffer_.empty()) {
    // Hold the burst until the line goes idle, then log it whole. Parsing earlier discards bytes that
    // fail the CRC one at a time, which is what hid the traffic: the counter rose and nothing was logged.
    const bool idle = (now - this->last_byte_ms_) > this->idle_timeout_ms_;
    const bool overflow = this->buffer_.size() >= 2 * MAX_FRAME;
    if (!idle && !overflow)
      return;

    if (this->log_frames_)
      this->log_sequence_(this->buffer_.data(), this->buffer_.size());

    while (!this->buffer_.empty() && this->parse_one_(true)) {
    }
  }

  this->poll_active_(millis());
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

  // An echoed copy of our own request matches. Anything else while we are waiting is the other master.
  if (this->probe_state_ == ProbeState::WAIT && this->last_request_.valid && len >= 6) {
    const uint16_t start = be16(frame + 2);
    const uint16_t count = be16(frame + 4);
    if (slave != this->last_request_.slave || fc != this->last_request_.function ||
        start != this->last_request_.read_start || count != this->last_request_.read_count) {
      this->collision_ = true;
      this->keep_foreign_request_ = true;
    }
  }

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
    if (this->probe_state_ == ProbeState::WAIT)
      this->collision_ = true;
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

void ModbusSniffer::build_probes_() {
  std::vector<RegisterSensor *> active;
  for (auto *s : this->sensors_) {
    if (s->is_active())
      active.push_back(s);
  }
  std::sort(active.begin(), active.end(), [](const RegisterSensor *a, const RegisterSensor *b) {
    if (a->get_slave() != b->get_slave())
      return a->get_slave() < b->get_slave();
    if (a->get_register_type() != b->get_register_type())
      return a->get_register_type() < b->get_register_type();
    return a->get_address() < b->get_address();
  });

  for (auto *s : active) {
    const uint8_t fc = s->get_register_type() == RegisterType::INPUT ? 0x04 : 0x03;
    const uint16_t end = s->get_address() + s->word_count();
    if (!this->probes_.empty()) {
      Probe &p = this->probes_.back();
      const uint16_t span_end = p.start + p.count;
      // One unused register between two sensors (a reserved hole) still fits in one read.
      if (p.slave == s->get_slave() && p.function == fc && s->get_address() <= span_end + 1) {
        const uint16_t new_end = std::max(span_end, end);
        if (new_end - p.start <= MAX_REGISTERS) {
          p.count = new_end - p.start;
          continue;
        }
      }
    }
    this->probes_.push_back(Probe{s->get_slave(), fc, s->get_address(), s->word_count()});
  }
}

void ModbusSniffer::poll_active_(uint32_t now) {
  if (this->probes_.empty())
    return;

  if (this->probe_state_ == ProbeState::WAIT) {
    // A matched reply means this burst was ours. Releasing the driver often leaves one
    // 0xFF on RX before the slave answers; that discarded byte is not the other master.
    if (!this->last_request_.valid && !this->collision_) {
      this->probe_index_++;
      this->probe_state_ = ProbeState::IDLE;
    } else if (this->collision_ || this->discarded_bytes_ != this->discard_at_send_) {
      this->abort_window_("collision");
      return;
    } else if (now - this->probe_sent_ms_ >= PROBE_TIMEOUT_MS) {
      const Probe &p = this->probes_[this->probe_index_];
      ESP_LOGW(TAG, "No reply from slave %u fc 0x%02X address %u (%u registers)", p.slave, p.function, p.start,
               p.count);
      this->last_request_.valid = false;
      this->probe_index_++;
      this->probe_state_ = ProbeState::IDLE;
    } else {
      return;
    }
  }

  if (this->window_open_) {
    const bool ran_out = now - this->window_start_ms_ + PROBE_TIMEOUT_MS >= this->active_window_ms_;
    if (this->probe_index_ >= this->probes_.size() || ran_out) {
      this->finish_window_();
      return;
    }
    this->send_probe_();
    return;
  }

  if (!this->armed_)
    return;
  if (now - this->last_foreign_ms_ < this->active_delay_ms_)
    return;

  this->window_open_ = true;
  this->window_start_ms_ = now;
  this->probe_index_ = 0;
  this->collision_ = false;
  ESP_LOGI(TAG, "Master quiet for %" PRIu32 " ms, reading %u ranges", this->active_delay_ms_,
           (unsigned) this->probes_.size());
  this->send_probe_();
}

void ModbusSniffer::send_probe_() {
  const Probe &p = this->probes_[this->probe_index_];
  uint8_t frame[8];
  frame[0] = p.slave;
  frame[1] = p.function;
  frame[2] = p.start >> 8;
  frame[3] = p.start & 0xFF;
  frame[4] = p.count >> 8;
  frame[5] = p.count & 0xFF;
  const uint16_t crc = crc16(frame, 6);
  frame[6] = crc & 0xFF;
  frame[7] = crc >> 8;

  this->last_request_.valid = true;
  this->last_request_.slave = p.slave;
  this->last_request_.function = p.function;
  this->last_request_.read_start = p.start;
  this->last_request_.read_count = p.count;
  this->probe_state_ = ProbeState::WAIT;
  this->probe_sent_ms_ = millis();
  this->discard_at_send_ = this->discarded_bytes_;
  this->collision_ = false;

  ESP_LOGD(TAG, "Probe slave %u fc 0x%02X address %u count %u", p.slave, p.function, p.start, p.count);
  this->write_array(frame, sizeof(frame));
  this->flush();
}

void ModbusSniffer::finish_window_() {
  ESP_LOGI(TAG, "Active reads finished, %u of %u ranges", (unsigned) this->probe_index_,
           (unsigned) this->probes_.size());
  this->window_open_ = false;
  this->armed_ = false;
  this->probe_state_ = ProbeState::IDLE;
  this->last_request_.valid = false;
}

void ModbusSniffer::abort_window_(const char *reason) {
  ESP_LOGW(TAG, "Active reads stopped: %s", reason);
  this->window_open_ = false;
  this->armed_ = true;
  this->probe_state_ = ProbeState::IDLE;
  if (!this->keep_foreign_request_)
    this->last_request_.valid = false;
  this->collision_ = false;
  this->keep_foreign_request_ = false;
  this->last_foreign_ms_ = millis();
}

}  // namespace modbus_sniffer
}  // namespace esphome
