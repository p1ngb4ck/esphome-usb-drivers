#pragma once

#include "usb_ethernet.h"

#ifdef USE_USB_ETHERNET

#include <cstddef>
#include <cstdint>

namespace esphome::usb_ethernet {

enum class VndKind : uint8_t {
  OUT,
  OUT8,
  OUT16,
  OUT_DATA,
  OUT_BYTES,
  OUT_MAC,
  IN,
  IN_OPTIONAL,
  IN_MAC,
  DELAY,
  HOOK,
  UNTIL_SET,
  UNTIL_CLEAR,
};

struct VndOp {
  VndKind kind;
  uint8_t request;
  uint16_t value;
  uint16_t index;
  uint32_t arg;
  const uint8_t *bytes;
};

static constexpr uint16_t VND_VALUE = 0xFFFF;
static constexpr uint16_t VND_PHY = 0xFFFE;

static constexpr VndOp vnd_out(uint8_t request, uint16_t value, uint16_t index) {
  return {VndKind::OUT, request, value, index, 0, nullptr};
}
static constexpr VndOp vnd_out8(uint8_t request, uint16_t value, uint16_t index, uint8_t data) {
  return {VndKind::OUT8, request, value, index, data, nullptr};
}
static constexpr VndOp vnd_out16(uint8_t request, uint16_t value, uint16_t index, uint16_t data) {
  return {VndKind::OUT16, request, value, index, data, nullptr};
}
static constexpr VndOp vnd_out_data(uint8_t request, uint16_t value, uint16_t index, uint8_t width) {
  return {VndKind::OUT_DATA, request, value, index, width, nullptr};
}
static constexpr VndOp vnd_out_bytes(uint8_t request, uint16_t value, uint16_t index, const uint8_t *bytes,
                                     uint8_t count) {
  return {VndKind::OUT_BYTES, request, value, index, count, bytes};
}
static constexpr VndOp vnd_out_mac(uint8_t request, uint16_t value, uint16_t index) {
  return {VndKind::OUT_MAC, request, value, index, 0, nullptr};
}
static constexpr VndOp vnd_in(uint8_t request, uint16_t value, uint16_t index, uint8_t length) {
  return {VndKind::IN, request, value, index, length, nullptr};
}
static constexpr VndOp vnd_in_optional(uint8_t request, uint16_t value, uint16_t index, uint8_t length) {
  return {VndKind::IN_OPTIONAL, request, value, index, length, nullptr};
}
static constexpr VndOp vnd_in_mac(uint8_t request, uint16_t value, uint16_t index) {
  return {VndKind::IN_MAC, request, value, index, 6, nullptr};
}
static constexpr VndOp vnd_delay(uint16_t ms) { return {VndKind::DELAY, 0, 0, 0, ms, nullptr}; }
static constexpr VndOp vnd_hook(uint8_t id) { return {VndKind::HOOK, id, 0, 0, 0, nullptr}; }
static constexpr VndOp vnd_until_set(uint32_t mask, uint8_t back, uint16_t tries, uint16_t delay_ms) {
  return {VndKind::UNTIL_SET, back, tries, delay_ms, mask, nullptr};
}
static constexpr VndOp vnd_until_clear(uint32_t mask, uint8_t back, uint16_t tries, uint16_t delay_ms) {
  return {VndKind::UNTIL_CLEAR, back, tries, delay_ms, mask, nullptr};
}

static inline size_t vnd_round_up(size_t n, size_t unit) { return unit == 0 ? n : (n + unit - 1) / unit * unit; }
static inline uint16_t vnd_le16(const uint8_t *p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
static inline uint32_t vnd_le32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
static inline void vnd_put16(uint8_t *p, uint16_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
}
static inline void vnd_put32(uint8_t *p, uint32_t v) {
  vnd_put16(p, static_cast<uint16_t>(v));
  vnd_put16(p + 2, static_cast<uint16_t>(v >> 16));
}

template<size_t N> static constexpr size_t vnd_count(const VndOp (&)[N]) { return N; }

}

#endif
