#include "usb_ethernet_vendor.h"

#ifdef USE_USB_ETHERNET

#include "esphome/core/log.h"

#include <cstring>

namespace esphome::usb_ethernet {

static const char *const TAG = "usb_ethernet";

static constexpr uint8_t ACCESS_MAC = 0x01;
static constexpr uint8_t ACCESS_PHY = 0x02;
static constexpr uint8_t ACCESS_EEPROM = 0x04;
static constexpr uint8_t ACCESS_FIFO = 0x81;
static constexpr uint16_t PHY_ID = 0x03;

static constexpr uint16_t REG_RX_CTL = 0x0B;
static constexpr uint16_t REG_NODE_ID = 0x10;
static constexpr uint16_t REG_MEDIUM = 0x22;
static constexpr uint16_t REG_RSTCTL = 0x26;
static constexpr uint16_t REG_BULKIN = 0x2E;
static constexpr uint16_t REG_CLK = 0x33;
static constexpr uint16_t REG_RXCOE = 0x34;
static constexpr uint16_t REG_TXCOE = 0x35;
static constexpr uint16_t REG_PAUSE_LOW = 0x54;
static constexpr uint16_t REG_PAUSE_HIGH = 0x55;

static constexpr uint16_t RST_IPRL = 0x0020;
static constexpr uint16_t RST_AT = 0x1000;
static constexpr uint8_t CLK_ACS_BCS = 0x02 | 0x01;
static constexpr uint8_t CLK_ULR = 0x08;
static constexpr uint16_t RX_CTL_INIT = 0x0100 | 0x0200 | 0x0080 | 0x0020 | 0x0002 | 0x0008;
static constexpr uint16_t RX_CTL_RUN = 0x0080 | 0x0008 | 0x0200 | 0x0002;
static constexpr uint8_t COE_ALL = 0x01 | 0x02 | 0x04 | 0x20 | 0x40;
static constexpr uint16_t MEDIUM_RECEIVE = 0x0100;
static constexpr uint16_t MEDIUM_TX_FLOW = 0x0020;
static constexpr uint16_t MEDIUM_RX_FLOW = 0x0010;
static constexpr uint16_t MEDIUM_FULL = 0x0002;
static constexpr uint16_t MEDIUM_GIGA = 0x0001;
static constexpr uint16_t MEDIUM_125MHZ = 0x0008;
static constexpr uint16_t MEDIUM_PS = 0x0200;
static constexpr uint16_t MEDIUM_DEFAULT = MEDIUM_RECEIVE | MEDIUM_TX_FLOW | MEDIUM_RX_FLOW | MEDIUM_FULL | MEDIUM_GIGA;

static constexpr uint16_t MII_BMCR = 0x00;
static constexpr uint16_t MII_PHYADDR = 0x19;
static constexpr uint16_t MII_MMD_CTRL = 0x0D;
static constexpr uint16_t MII_MMD_DATA = 0x0E;
static constexpr uint16_t MII_MMD_NOINCR = 0x4000;
static constexpr uint16_t MMD_AN = 7;
static constexpr uint16_t AN_EEE_ADV = 60;
static constexpr uint16_t PHY_PAGE_SELECT = 0x1F;
static constexpr uint16_t PHY_PHYSR = 0x11;
static constexpr uint16_t PAGE_3 = 3;
static constexpr uint16_t PAGE_0 = 0;
static constexpr uint16_t EEE_OFF = 0x3246;
static constexpr uint16_t BMCR_AN_ENABLE = 0x1000;
static constexpr uint16_t BMCR_AN_RESTART = 0x0200;
static constexpr uint16_t PHYSR_SPEED_MASK = 0xC000;
static constexpr uint16_t PHYSR_GIGA = 0x8000;
static constexpr uint16_t PHYSR_100 = 0x4000;
static constexpr uint16_t PHYSR_FULL = 0x2000;
static constexpr uint16_t PHYSR_LINK = 0x0400;

static constexpr uint32_t RXHDR_CRC_ERR = 0x20000000;
static constexpr uint32_t RXHDR_DROP_ERR = 0x80000000;
static constexpr uint32_t TX_PADDING_ENABLE = 0x80008000;
static constexpr size_t TX_HEADER_SIZE = 8;
static constexpr size_t ETH_HEADER = 14;

static constexpr uint8_t BULKIN_SETTING[5] = {7, 0x4F, 0, 0x12, 0xFF};

enum Ax179Hook : uint8_t {
  H_DETACH,
  H_CLK,
  H_RST,
  H_MAC,
  H_AN,
  H_DONE,
  H_MEDIUM,
  H_LINK_DONE,
};

static constexpr VndOp INIT_OPS[] = {
    vnd_out16(ACCESS_MAC, REG_RSTCTL, 2, 0),
    vnd_out16(ACCESS_MAC, REG_RSTCTL, 2, RST_IPRL),
    vnd_delay(500),
    vnd_out8(ACCESS_MAC, REG_CLK, 1, CLK_ACS_BCS),
    vnd_delay(200),
    vnd_in_optional(ACCESS_EEPROM, 0x43, 1, 2),
    vnd_hook(H_DETACH),
    vnd_in(ACCESS_MAC, REG_CLK, 1, 1),
    vnd_hook(H_CLK),
    vnd_out_data(ACCESS_MAC, REG_CLK, 1, 1),
    vnd_in(ACCESS_MAC, REG_RSTCTL, 2, 2),
    vnd_hook(H_RST),
    vnd_out_data(ACCESS_MAC, REG_RSTCTL, 2, 2),
    vnd_in_mac(ACCESS_MAC, REG_NODE_ID, 6),
    vnd_hook(H_MAC),
    vnd_out_mac(ACCESS_MAC, REG_NODE_ID, 6),
    vnd_out_bytes(ACCESS_MAC, REG_BULKIN, 5, BULKIN_SETTING, 5),
    vnd_out8(ACCESS_MAC, REG_PAUSE_HIGH, 1, 0x34),
    vnd_out8(ACCESS_MAC, REG_PAUSE_LOW, 1, 0x52),
    vnd_out8(ACCESS_MAC, REG_RXCOE, 1, COE_ALL),
    vnd_out8(ACCESS_MAC, REG_TXCOE, 1, COE_ALL),
    vnd_out16(ACCESS_MAC, REG_RX_CTL, 2, RX_CTL_INIT),
    vnd_out16(ACCESS_MAC, REG_MEDIUM, 2, MEDIUM_DEFAULT),
    vnd_out16(ACCESS_PHY, PHY_ID, PHY_PAGE_SELECT, PAGE_3),
    vnd_out16(ACCESS_PHY, PHY_ID, MII_PHYADDR, EEE_OFF),
    vnd_out16(ACCESS_PHY, PHY_ID, PHY_PAGE_SELECT, PAGE_0),
    vnd_out16(ACCESS_PHY, PHY_ID, MII_MMD_CTRL, MMD_AN),
    vnd_out16(ACCESS_PHY, PHY_ID, MII_MMD_DATA, AN_EEE_ADV),
    vnd_out16(ACCESS_PHY, PHY_ID, MII_MMD_CTRL, MMD_AN | MII_MMD_NOINCR),
    vnd_out16(ACCESS_PHY, PHY_ID, MII_MMD_DATA, 0),
    vnd_in(ACCESS_PHY, PHY_ID, MII_BMCR, 2),
    vnd_hook(H_AN),
    vnd_out_data(ACCESS_PHY, PHY_ID, MII_BMCR, 2),
    vnd_hook(H_DONE),
};

static constexpr VndOp LINK_OPS[] = {
    vnd_out16(ACCESS_MAC, REG_RX_CTL, 2, 0),
    vnd_out16(ACCESS_MAC, REG_RX_CTL, 2, RX_CTL_RUN),
    vnd_in(ACCESS_FIFO, 0x8C, 0, 4),
    vnd_until_clear(0x40000000, 3, 30, 0),
    vnd_in(ACCESS_PHY, PHY_ID, PHY_PHYSR, 2),
    vnd_hook(H_MEDIUM),
    vnd_out_data(ACCESS_MAC, REG_MEDIUM, 2, 2),
    vnd_hook(H_LINK_DONE),
};

const USBEthernet::VidPid USBEthernet::AX88179_IDS[] = {
    {0x0B95, 0x1790}, {0x0B95, 0x178A}, {0x04B4, 0x3610}, {0x2001, 0x4A00}, {0x0DF6, 0x0072},
    {0x04E8, 0xA100}, {0x17EF, 0x304B}, {0x050D, 0x0128}, {0x0930, 0x0A13}, {0x0711, 0x0179},
    {0x07C9, 0x000E}, {0x07C9, 0x000F}, {0x07C9, 0x0010},
};
const size_t USBEthernet::AX88179_ID_COUNT = sizeof(USBEthernet::AX88179_IDS) / sizeof(USBEthernet::AX88179_IDS[0]);

void USBEthernet::ax88179_begin_(uint32_t generation) {
  this->vnd_script_(generation, INIT_OPS, vnd_count(INIT_OPS));
}

uint8_t USBEthernet::ax88179_hook_(uint32_t generation, uint8_t id) {
  switch (id) {
    case H_DETACH:
      return ((this->vnd_data_ & 0xFFFF) == 0xFFFF || (this->vnd_data_ & 0x0100) == 0) ? 6 : 0;
    case H_CLK:
      this->vnd_data_ |= CLK_ULR;
      return 0;
    case H_RST:
      this->vnd_data_ |= RST_AT;
      return 0;
    case H_MAC:
      this->vnd_check_mac_();
      return 0;
    case H_AN:
      if ((this->vnd_data_ & BMCR_AN_ENABLE) == 0)
        return 1;
      this->vnd_data_ |= BMCR_AN_RESTART;
      return 0;
    case H_DONE:
      this->vnd_start_data_(generation);
      return 0;
    case H_MEDIUM: {
      const uint16_t status = static_cast<uint16_t>(this->vnd_data_);
      if ((status & PHYSR_LINK) == 0) {
        this->link_target_.store(false, std::memory_order_release);
        this->vnd_link_done_(generation);
        return 0;
      }
      uint16_t mode = MEDIUM_RECEIVE | MEDIUM_TX_FLOW | MEDIUM_RX_FLOW;
      uint32_t speed = 10000000;
      if ((status & PHYSR_SPEED_MASK) == PHYSR_GIGA) {
        mode |= MEDIUM_GIGA | MEDIUM_125MHZ;
        speed = 1000000000;
      } else if ((status & PHYSR_SPEED_MASK) == PHYSR_100) {
        mode |= MEDIUM_PS;
        speed = 100000000;
      }
      if ((status & PHYSR_FULL) != 0)
        mode |= MEDIUM_FULL;
      this->vnd_data_ = mode;
      this->link_speed_.store(speed, std::memory_order_release);
      return 0;
    }
    case H_LINK_DONE:
      this->vnd_link_done_(generation);
      return 0;
    default:
      return 0;
  }
}

bool USBEthernet::ax88179_link_script_(uint32_t generation) {
  this->vnd_script_(generation, LINK_OPS, vnd_count(LINK_OPS));
  return true;
}

void USBEthernet::handle_ax88179_rx_(const uint8_t *data, size_t len) {
  if (len < 4)
    return;
  len -= 4;
  const uint32_t rx_header = vnd_le32(data + len);
  uint16_t count = static_cast<uint16_t>(rx_header);
  const uint16_t metadata = static_cast<uint16_t>(rx_header >> 16);
  if (count == 0 || count * 4u + metadata > len)
    return;
  size_t position = 0;
  size_t remaining = metadata;
  for (const uint8_t *header = data + metadata; count > 0; count--, header += 4) {
    const uint32_t value = vnd_le32(header);
    const uint16_t packet = (value >> 16) & 0x1FFF;
    const size_t padded = (static_cast<size_t>(packet) + 7) & 0xFFF8;
    if (packet == 0)
      continue;
    if (padded > remaining)
      return;
    if ((value & (RXHDR_CRC_ERR | RXHDR_DROP_ERR)) == 0 && packet >= 2 + ETH_HEADER)
      this->deliver_(data + position + 2, packet - 2);
    position += padded;
    remaining -= padded;
  }
}

size_t USBEthernet::ax88179_fill_tx_(uint8_t *out, const uint8_t *frame, size_t len) {
  if (len + TX_HEADER_SIZE + 1 > this->tx_size_)
    return 0;
  size_t total = TX_HEADER_SIZE + len;
  const bool pad = total % this->out_mps_ == 0;
  vnd_put32(out, static_cast<uint32_t>(len));
  vnd_put32(out + 4, pad ? TX_PADDING_ENABLE : 0);
  memcpy(out + TX_HEADER_SIZE, frame, len);
  if (pad) {
    out[total] = 0;
    total++;
  }
  return total;
}

}

#endif
