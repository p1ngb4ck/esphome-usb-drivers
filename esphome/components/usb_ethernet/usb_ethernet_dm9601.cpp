#include "usb_ethernet_vendor.h"

#ifdef USE_USB_ETHERNET

#include "esphome/core/log.h"

#include <cstring>

namespace esphome::usb_ethernet {

static constexpr uint8_t REQ_READ_REGS = 0x00;
static constexpr uint8_t REQ_WRITE_REGS = 0x01;
static constexpr uint8_t REQ_WRITE_REG = 0x03;

static constexpr uint16_t DM_NET_CTRL = 0x00;
static constexpr uint16_t DM_RX_CTRL = 0x05;
static constexpr uint16_t DM_SHARED_CTRL = 0x0B;
static constexpr uint16_t DM_SHARED_ADDR = 0x0C;
static constexpr uint16_t DM_SHARED_DATA = 0x0D;
static constexpr uint16_t DM_PHY_ADDR = 0x10;
static constexpr uint16_t DM_MCAST_ADDR = 0x16;
static constexpr uint16_t DM_GPR_CTRL = 0x1E;
static constexpr uint16_t DM_GPR_DATA = 0x1F;
static constexpr uint16_t DM_CHIP_ID = 0x2C;
static constexpr uint16_t DM_MODE_CTRL = 0x91;
static constexpr uint32_t DM_CHIP_DM9620 = 1;
static constexpr uint16_t DM_RX_ALL_MULTI = 0x31 | 0x08;
static constexpr uint16_t SHARED_PHY_WRITE = 0x1A;
static constexpr uint16_t SHARED_PHY_ADDRESS = 0x40;
static constexpr uint16_t MII_BMCR = 0x00;
static constexpr uint16_t MII_ADVERTISE = 0x04;
static constexpr uint16_t BMCR_RESET = 0x8000;
static constexpr uint16_t BMCR_AN_RESTART = 0x1000 | 0x0200;
static constexpr uint16_t ADVERTISE_ALL_PAUSE = 0x01E0 | 0x0001 | 0x0400;
static constexpr uint8_t DM_MCAST_HASH[8] = {0, 0, 0, 0, 0, 0, 0, 0x80};

static constexpr uint16_t SR_NCR = 0x00;
static constexpr uint16_t SR_RCR = 0x05;
static constexpr uint16_t SR_PAR = 0x10;
static constexpr uint16_t SR_PRR = 0x1F;
static constexpr uint16_t SR_NCR_RST = 0x01;
static constexpr uint16_t SR_PRR_RST = 0x01;
static constexpr uint16_t SR_RCR_RUN = 0x01 | 0x10 | 0x20 | 0x08;
static constexpr uint8_t SR_RSR_MF = 0x40;

static constexpr size_t DM_RX_OVERHEAD = 7;
static constexpr size_t RX_HEADER_SIZE = 3;
static constexpr size_t TX_HEADER_SIZE = 2;
static constexpr size_t FCS_SIZE = 4;
static constexpr size_t DM_FRAME_LEN = 1514;

enum DmHook : uint8_t { H_MAC, H_CHIP, H_MODE, H_DONE };

#define DM_PHY_WRITE(reg, data) \
  vnd_out16(REQ_WRITE_REGS, 0, DM_SHARED_DATA, data), \
      vnd_out(REQ_WRITE_REG, (reg) | SHARED_PHY_ADDRESS, DM_SHARED_ADDR), \
      vnd_out(REQ_WRITE_REG, SHARED_PHY_WRITE, DM_SHARED_CTRL), vnd_in(REQ_READ_REGS, 0, DM_SHARED_CTRL, 1), \
      vnd_until_clear(0x01, 1, 1000, 0), vnd_out(REQ_WRITE_REG, 0, DM_SHARED_CTRL)

static constexpr VndOp DM_INIT_OPS[] = {
    vnd_out(REQ_WRITE_REG, 1, DM_NET_CTRL),
    vnd_in_mac(REQ_READ_REGS, 0, DM_PHY_ADDR),
    vnd_hook(H_MAC),
    vnd_out_mac(REQ_WRITE_REGS, 0, DM_PHY_ADDR),
    vnd_in(REQ_READ_REGS, 0, DM_CHIP_ID, 1),
    vnd_hook(H_CHIP),
    vnd_in(REQ_READ_REGS, 0, DM_MODE_CTRL, 1),
    vnd_hook(H_MODE),
    vnd_out(REQ_WRITE_REG, VND_VALUE, DM_MODE_CTRL),
    vnd_out(REQ_WRITE_REG, 1, DM_GPR_CTRL),
    vnd_out(REQ_WRITE_REG, 0, DM_GPR_DATA),
    vnd_out_bytes(REQ_WRITE_REGS, 0, DM_MCAST_ADDR, DM_MCAST_HASH, 8),
    vnd_out(REQ_WRITE_REG, DM_RX_ALL_MULTI, DM_RX_CTRL),
    DM_PHY_WRITE(MII_BMCR, BMCR_RESET),
    DM_PHY_WRITE(MII_ADVERTISE, ADVERTISE_ALL_PAUSE),
    DM_PHY_WRITE(MII_BMCR, BMCR_AN_RESTART),
    vnd_hook(H_DONE),
};

#undef DM_PHY_WRITE

static constexpr VndOp SR_INIT_OPS[] = {
    vnd_out(REQ_WRITE_REG, SR_NCR_RST, SR_NCR),
    vnd_in_mac(REQ_READ_REGS, 0, SR_PAR),
    vnd_hook(H_MAC),
    vnd_out_mac(REQ_WRITE_REGS, 0, SR_PAR),
    vnd_out(REQ_WRITE_REG, SR_PRR_RST, SR_PRR),
    vnd_delay(20),
    vnd_out(REQ_WRITE_REG, 0, SR_PRR),
    vnd_delay(2),
    vnd_out(REQ_WRITE_REG, SR_RCR_RUN, SR_RCR),
    vnd_hook(H_DONE),
};

const USBEthernet::VidPid USBEthernet::DM9601_IDS[] = {
    {0x07AA, 0x9601}, {0x0A46, 0x9601}, {0x0A46, 0x6688}, {0x0A46, 0x0268}, {0x0A46, 0x8515},
    {0x0A47, 0x9601}, {0x0FE6, 0x8101}, {0x0A46, 0x9000}, {0x0A46, 0x9620}, {0x0A46, 0x9621},
    {0x0A46, 0x9622}, {0x0A46, 0x0269}, {0x0A46, 0x1269}, {0x0586, 0x3427},
};
const size_t USBEthernet::DM9601_ID_COUNT = sizeof(USBEthernet::DM9601_IDS) / sizeof(USBEthernet::DM9601_IDS[0]);

const USBEthernet::VidPid USBEthernet::SR9700_IDS[] = {
    {0x0FE6, 0x9700},
    {0x0FE6, 0x9702},
};
const size_t USBEthernet::SR9700_ID_COUNT = sizeof(USBEthernet::SR9700_IDS) / sizeof(USBEthernet::SR9700_IDS[0]);

void USBEthernet::dm9601_begin_(uint32_t generation) {
  this->vnd_script_(generation, DM_INIT_OPS, vnd_count(DM_INIT_OPS));
}

void USBEthernet::sr9700_begin_(uint32_t generation) {
  this->vnd_script_(generation, SR_INIT_OPS, vnd_count(SR_INIT_OPS));
}

uint8_t USBEthernet::dm9601_hook_(uint32_t generation, uint8_t id) {
  switch (id) {
    case H_MAC:
      this->vnd_check_mac_();
      return 0;
    case H_CHIP:
      return this->vnd_data_ == DM_CHIP_DM9620 ? 0 : 3;
    case H_MODE:
      this->vnd_data_ &= 0x7F;
      return 0;
    case H_DONE:
      this->vnd_start_data_(generation);
      return 0;
    default:
      return 0;
  }
}

uint8_t USBEthernet::sr9700_hook_(uint32_t generation, uint8_t id) {
  if (id == H_MAC) {
    this->vnd_check_mac_();
  } else if (id == H_DONE) {
    this->vnd_start_data_(generation);
  }
  return 0;
}

void USBEthernet::handle_dm9601_rx_(const uint8_t *data, size_t len) {
  if (len < DM_RX_OVERHEAD)
    return;
  const uint8_t status = data[0];
  const int frame = static_cast<int>(vnd_le16(data + 1)) - static_cast<int>(FCS_SIZE);
  if ((status & 0xBF) != 0 || frame <= 0 || static_cast<size_t>(frame) + RX_HEADER_SIZE > len)
    return;
  this->deliver_(data + RX_HEADER_SIZE, static_cast<size_t>(frame));
}

size_t USBEthernet::dm9601_fill_tx_(uint8_t *out, const uint8_t *frame, size_t len) {
  size_t total = len + TX_HEADER_SIZE;
  while ((total & 1) != 0 || total % this->out_mps_ == 0)
    total++;
  if (total > this->tx_size_)
    return 0;
  const size_t payload = total - TX_HEADER_SIZE;
  vnd_put16(out, static_cast<uint16_t>(payload));
  memcpy(out + TX_HEADER_SIZE, frame, len);
  memset(out + TX_HEADER_SIZE + len, 0, payload - len);
  return total;
}

void USBEthernet::handle_sr9700_rx_(const uint8_t *data, size_t len) {
  while (len > DM_RX_OVERHEAD) {
    if (data[0] != SR_RSR_MF)
      return;
    const int frame = static_cast<int>(vnd_le16(data + 1)) - static_cast<int>(FCS_SIZE);
    if (frame < 0 || static_cast<size_t>(frame) > DM_FRAME_LEN || static_cast<size_t>(frame) > len - DM_RX_OVERHEAD)
      return;
    this->deliver_(data + RX_HEADER_SIZE, static_cast<size_t>(frame));
    data += static_cast<size_t>(frame) + DM_RX_OVERHEAD;
    len -= static_cast<size_t>(frame) + DM_RX_OVERHEAD;
  }
}

size_t USBEthernet::sr9700_fill_tx_(uint8_t *out, const uint8_t *frame, size_t len) {
  size_t total = len + TX_HEADER_SIZE;
  const bool pad = total % this->out_mps_ == 0;
  if (total + (pad ? 1 : 0) > this->tx_size_)
    return 0;
  vnd_put16(out, static_cast<uint16_t>(pad ? len + 1 : len));
  memcpy(out + TX_HEADER_SIZE, frame, len);
  if (pad) {
    out[total] = 0;
    total++;
  }
  return total;
}

}

#endif
