#include "usb_ethernet_vendor.h"

#ifdef USE_USB_ETHERNET

#include "esphome/core/log.h"

#include <algorithm>
#include <cstring>

namespace esphome::usb_ethernet {

static const char *const TAG = "usb_ethernet";

static constexpr uint8_t CMD_SET_SW_MII = 0x06;
static constexpr uint8_t CMD_READ_MII = 0x07;
static constexpr uint8_t CMD_WRITE_MII = 0x08;
static constexpr uint8_t CMD_STATMNGSTS = 0x09;
static constexpr uint8_t CMD_SET_HW_MII = 0x0A;
static constexpr uint8_t CMD_READ_EEPROM = 0x0B;
static constexpr uint8_t CMD_WRITE_RX_CTL = 0x10;
static constexpr uint8_t CMD_WRITE_IPG0 = 0x12;
static constexpr uint8_t CMD_READ_NODE_ID = 0x13;
static constexpr uint8_t CMD_WRITE_NODE_ID = 0x14;
static constexpr uint8_t CMD_READ_PHY_ID = 0x19;
static constexpr uint8_t CMD_WRITE_MEDIUM = 0x1B;
static constexpr uint8_t CMD_WRITE_GPIOS = 0x1F;
static constexpr uint8_t CMD_SW_RESET = 0x20;
static constexpr uint8_t CMD_SW_PHY_SELECT = 0x22;
static constexpr uint8_t CMD_QCTCTRL = 0x2A;

static constexpr uint32_t HOST_EN = 0x01;
static constexpr uint8_t CHIPCODE_MASK = 0x70;
static constexpr uint8_t CHIP_AX88772 = 0x00;
static constexpr uint8_t CHIP_AX88772A = 0x10;
static constexpr uint8_t CHIP_AX88772B = 0x20;
static constexpr uint8_t EMBEDDED_PHY = 0x10;
static constexpr uint8_t PHYSEL_SSEN = 0x10;
static constexpr uint16_t ASIX_GPIO_RSE = 0x80;
static constexpr uint16_t ASIX_GPIO_GPO_2 = 0x20;
static constexpr uint16_t ASIX_GPIO_GPO2EN = 0x10;
static constexpr uint16_t RESET_CLEAR = 0x00;
static constexpr uint16_t RESET_PRL = 0x08;
static constexpr uint16_t RESET_IPRL = 0x20;
static constexpr uint16_t RESET_IPPD = 0x40;
static constexpr uint16_t IPG = 0x15 | 0x0C;
static constexpr uint16_t IPG2 = 0x12;
static constexpr uint16_t RX_CTL_ALL_MULTI = 0x0080 | 0x0008 | 0x0002;
static constexpr uint16_t MEDIUM_FD = 0x0002;
static constexpr uint16_t MEDIUM_AC = 0x0004;
static constexpr uint16_t MEDIUM_RE = 0x0100;
static constexpr uint16_t MEDIUM_PS = 0x0200;
static constexpr uint16_t MEDIUM_DEFAULT = MEDIUM_FD | MEDIUM_PS | MEDIUM_AC | MEDIUM_RE;
static constexpr uint8_t MII_BMCR = 0x00;
static constexpr uint8_t MII_ADVERTISE = 0x04;
static constexpr uint8_t MII_LPA = 0x05;
static constexpr uint16_t BMCR_AN_RESTART = 0x1000 | 0x0200;
static constexpr uint16_t ADVERTISE_ALL_CSMA = 0x01E0 | 0x0001;
static constexpr uint16_t LPA_10FULL = 0x0040;
static constexpr uint16_t LPA_100HALF = 0x0080;
static constexpr uint16_t LPA_100FULL = 0x0100;
static constexpr uint8_t PHY14H = 0x14;
static constexpr uint8_t PHY15H = 0x15;
static constexpr uint8_t PHY16H = 0x16;
static constexpr uint16_t PHY14H_DEFAULT = 0x442C;
static constexpr uint16_t PHY15H_DEFAULT = 0x03C8;
static constexpr uint16_t PHY16H_DEFAULT = 0x4044;

static constexpr size_t TX_HEADER_SIZE = 4;
static constexpr size_t TX_PADDING_SIZE = 4;
static constexpr uint16_t RX_SIZE_MASK = 0x07FF;
static constexpr size_t FRAME_MAX = 1518;
static constexpr uint32_t PAD_BYTES = 0xFFFF0000;

enum AsixHook : uint8_t {
  H_EE0,
  H_EE1,
  H_EE2,
  H_MAC,
  H_PHY,
  H_CHIP,
  H_SELECT_772,
  H_SELECT_A,
  H_IF_EMBEDDED,
  H_IF_NOT_EMBEDDED,
  H_IF_B,
  H_IF_A,
  H_RESET_DONE,
  H_UP_DONE,
  H_ADVERTISE,
  H_LPA,
  H_LINK_DONE,
};

#define ASIX_ENTER_MII \
  vnd_out(CMD_SET_SW_MII, 0, 0), vnd_delay(1), vnd_in(CMD_STATMNGSTS, 0, 0, 1), vnd_until_set(HOST_EN, 3, 30, 0)

static constexpr VndOp MAC_NODE_OPS[] = {
    vnd_in_mac(CMD_READ_NODE_ID, 0, 0),
    vnd_hook(H_MAC),
};

static constexpr VndOp MAC_EEPROM_OPS[] = {
    vnd_in(CMD_READ_EEPROM, 0x04, 0, 2), vnd_hook(H_EE0), vnd_in(CMD_READ_EEPROM, 0x05, 0, 2), vnd_hook(H_EE1),
    vnd_in(CMD_READ_EEPROM, 0x06, 0, 2), vnd_hook(H_EE2), vnd_hook(H_MAC),
};

static constexpr VndOp PROBE_OPS[] = {
    vnd_in(CMD_READ_PHY_ID, 0, 0, 2),
    vnd_hook(H_PHY),
    vnd_in(CMD_STATMNGSTS, 0, 0, 1),
    vnd_hook(H_CHIP),
};

static constexpr VndOp RESET_772_OPS[] = {
    vnd_out(CMD_WRITE_GPIOS, ASIX_GPIO_RSE | ASIX_GPIO_GPO_2 | ASIX_GPIO_GPO2EN, 0),
    vnd_delay(5),
    vnd_hook(H_SELECT_772),
    vnd_out(CMD_SW_PHY_SELECT, VND_VALUE, 0),
    vnd_hook(H_IF_EMBEDDED),
    vnd_out(CMD_SW_RESET, RESET_IPPD, 0),
    vnd_delay(10),
    vnd_out(CMD_SW_RESET, RESET_CLEAR, 0),
    vnd_delay(60),
    vnd_out(CMD_SW_RESET, RESET_IPRL | RESET_PRL, 0),
    vnd_hook(H_IF_NOT_EMBEDDED),
    vnd_out(CMD_SW_RESET, RESET_IPPD | RESET_PRL, 0),
    vnd_delay(150),
    vnd_out(CMD_WRITE_RX_CTL, RX_CTL_ALL_MULTI, 0),
    vnd_out(CMD_WRITE_MEDIUM, MEDIUM_DEFAULT, 0),
    vnd_out(CMD_WRITE_IPG0, IPG, IPG2),
    vnd_hook(H_RESET_DONE),
};

static constexpr VndOp RESET_A_OPS[] = {
    vnd_out(CMD_WRITE_GPIOS, ASIX_GPIO_RSE, 0),
    vnd_delay(5),
    vnd_hook(H_SELECT_A),
    vnd_out(CMD_SW_PHY_SELECT, VND_VALUE, 0),
    vnd_delay(10),
    vnd_out(CMD_SW_RESET, RESET_IPPD | RESET_IPRL, 0),
    vnd_delay(10),
    vnd_out(CMD_SW_RESET, RESET_IPRL, 0),
    vnd_delay(160),
    vnd_out(CMD_SW_RESET, RESET_CLEAR, 0),
    vnd_out(CMD_SW_RESET, RESET_IPRL, 0),
    vnd_delay(200),
    vnd_hook(H_IF_B),
    vnd_out(CMD_QCTCTRL, 0x8000, 0x8001),
    vnd_hook(H_IF_A),
    ASIX_ENTER_MII,
    vnd_out16(CMD_WRITE_MII, VND_PHY, PHY14H, PHY14H_DEFAULT),
    vnd_out16(CMD_WRITE_MII, VND_PHY, PHY15H, PHY15H_DEFAULT),
    vnd_out16(CMD_WRITE_MII, VND_PHY, PHY16H, PHY16H_DEFAULT),
    vnd_out(CMD_SET_HW_MII, 0, 0),
    vnd_out(CMD_WRITE_IPG0, IPG, IPG2),
    vnd_hook(H_RESET_DONE),
};

static constexpr VndOp UP_OPS[] = {
    vnd_out_mac(CMD_WRITE_NODE_ID, 0, 0),
    vnd_out(CMD_WRITE_RX_CTL, RX_CTL_ALL_MULTI, 0),
    vnd_out(CMD_WRITE_MEDIUM, MEDIUM_DEFAULT, 0),
    ASIX_ENTER_MII,
    vnd_out16(CMD_WRITE_MII, VND_PHY, MII_ADVERTISE, ADVERTISE_ALL_CSMA),
    vnd_out16(CMD_WRITE_MII, VND_PHY, MII_BMCR, BMCR_AN_RESTART),
    vnd_out(CMD_SET_HW_MII, 0, 0),
    vnd_hook(H_UP_DONE),
};

static constexpr VndOp LINK_OPS[] = {
    ASIX_ENTER_MII,
    vnd_in(CMD_READ_MII, VND_PHY, MII_ADVERTISE, 2),
    vnd_hook(H_ADVERTISE),
    vnd_in(CMD_READ_MII, VND_PHY, MII_LPA, 2),
    vnd_hook(H_LPA),
    vnd_out(CMD_SET_HW_MII, 0, 0),
    vnd_out(CMD_WRITE_MEDIUM, VND_VALUE, 0),
    vnd_hook(H_LINK_DONE),
};

static constexpr uint8_t CMD_WRITE_ENABLE = 0x0D;
static constexpr uint8_t CMD_WRITE_DISABLE = 0x0E;
static constexpr uint16_t GPIO_GPO_1 = 0x08;
static constexpr uint16_t GPIO_GPO1EN = 0x04;
static constexpr uint8_t MII_CTRL1000 = 0x09;
static constexpr uint8_t MII_STAT1000 = 0x0A;
static constexpr uint8_t MII_MARVELL_CTRL = 0x14;
static constexpr uint8_t MII_MARVELL_LED_CTRL = 0x18;
static constexpr uint16_t MARVELL_CTRL_DELAYS = 0x0002 | 0x0080;
static constexpr uint16_t BMCR_RESET_AN = 0x8000 | 0x1000;
static constexpr uint16_t BMCR_RESET = 0x8000;
static constexpr uint16_t BMCR_AN_ENABLE = 0x1000;
static constexpr uint16_t BMCR_RESTART = 0x0200;
static constexpr uint16_t ADVERTISE_ALL_CSMA_PAUSE = 0x01E0 | 0x0001 | 0x0400;
static constexpr uint16_t ADVERTISE_1000FULL = 0x0200;
static constexpr uint16_t LPA_1000FULL = 0x0800;
static constexpr uint16_t LPA_1000HALF = 0x0400;
static constexpr uint16_t MEDIUM_GM = 0x0001;
static constexpr uint16_t MEDIUM_ENCK = 0x0008;
static constexpr uint16_t MEDIUM_RFC = 0x0010;
static constexpr uint16_t MEDIUM_TFC = 0x0020;
static constexpr uint16_t MEDIUM_JFE = 0x0040;
static constexpr uint16_t MEDIUM_178_DEFAULT =
    MEDIUM_PS | MEDIUM_FD | MEDIUM_AC | MEDIUM_RFC | MEDIUM_TFC | MEDIUM_JFE | MEDIUM_RE;
static constexpr uint8_t PHY_MODE_MARVELL = 0x00;
static constexpr uint8_t PHY_MODE_RTL8211CL = 0x0C;

enum Asix178Hook : uint8_t {
  H178_MAC = 64,
  H178_EEPROM,
  H178_IF_GPIO_NORMAL,
  H178_IF_GPIO_LED1,
  H178_IF_MARVELL,
  H178_IF_MARVELL_LED,
  H178_MARVELL_LED,
  H178_IF_RTL8211,
  H178_BIT_80,
  H178_IF_RTL8211_LED,
  H178_RESTART,
  H178_LPA,
  H178_CTRL1000,
  H178_STAT1000,
};

static constexpr VndOp INIT_178_OPS[] = {
    vnd_in_mac(CMD_READ_NODE_ID, 0, 0),
    vnd_hook(H178_MAC),
    vnd_in(CMD_READ_PHY_ID, 0, 0, 2),
    vnd_hook(H_PHY),
    vnd_out(CMD_SW_RESET, RESET_CLEAR, 0),
    vnd_delay(150),
    vnd_out(CMD_SW_RESET, RESET_PRL | RESET_IPPD, 0),
    vnd_delay(150),
    vnd_out(CMD_WRITE_ENABLE, 0, 0),
    vnd_in(CMD_READ_EEPROM, 0x17, 0, 2),
    vnd_hook(H178_EEPROM),
    vnd_out(CMD_WRITE_DISABLE, 0, 0),
    vnd_out(CMD_WRITE_GPIOS, ASIX_GPIO_RSE | GPIO_GPO_1 | GPIO_GPO1EN, 0),
    vnd_delay(40),
    vnd_hook(H178_IF_GPIO_NORMAL),
    vnd_out(CMD_WRITE_GPIOS, 0x003C, 0),
    vnd_delay(30),
    vnd_out(CMD_WRITE_GPIOS, 0x001C, 0),
    vnd_delay(300),
    vnd_out(CMD_WRITE_GPIOS, 0x003C, 0),
    vnd_delay(30),
    vnd_hook(H178_IF_GPIO_LED1),
    vnd_out(CMD_WRITE_GPIOS, GPIO_GPO1EN, 0),
    vnd_delay(30),
    vnd_out(CMD_WRITE_GPIOS, GPIO_GPO1EN | GPIO_GPO_1, 0),
    vnd_delay(30),
    vnd_out(CMD_SW_PHY_SELECT, 0, 0),
    vnd_out(CMD_SW_RESET, RESET_CLEAR, 0),
    vnd_delay(150),
    vnd_out(CMD_SW_RESET, RESET_PRL | RESET_IPPD, 0),
    vnd_delay(150),
    vnd_out(CMD_WRITE_RX_CTL, 0, 0),
    vnd_hook(H178_IF_MARVELL),
    ASIX_ENTER_MII,
    vnd_out16(CMD_WRITE_MII, VND_PHY, MII_MARVELL_CTRL, MARVELL_CTRL_DELAYS),
    vnd_hook(H178_IF_MARVELL_LED),
    vnd_in(CMD_READ_MII, VND_PHY, MII_MARVELL_LED_CTRL, 2),
    vnd_hook(H178_MARVELL_LED),
    vnd_out_data(CMD_WRITE_MII, VND_PHY, MII_MARVELL_LED_CTRL, 2),
    vnd_out(CMD_SET_HW_MII, 0, 0),
    vnd_delay(60),
    vnd_hook(H178_IF_RTL8211),
    ASIX_ENTER_MII,
    vnd_out16(CMD_WRITE_MII, VND_PHY, 0x1F, 0x0005),
    vnd_out16(CMD_WRITE_MII, VND_PHY, 0x0C, 0x0000),
    vnd_in(CMD_READ_MII, VND_PHY, 0x01, 2),
    vnd_hook(H178_BIT_80),
    vnd_out_data(CMD_WRITE_MII, VND_PHY, 0x01, 2),
    vnd_out16(CMD_WRITE_MII, VND_PHY, 0x1F, 0x0000),
    vnd_hook(H178_IF_RTL8211_LED),
    vnd_out16(CMD_WRITE_MII, VND_PHY, 0x1F, 0x0002),
    vnd_out16(CMD_WRITE_MII, VND_PHY, 0x1A, 0x00CB),
    vnd_out16(CMD_WRITE_MII, VND_PHY, 0x1F, 0x0000),
    vnd_out(CMD_SET_HW_MII, 0, 0),
    ASIX_ENTER_MII,
    vnd_out16(CMD_WRITE_MII, VND_PHY, MII_BMCR, BMCR_RESET_AN),
    vnd_delay(1),
    vnd_in(CMD_READ_MII, VND_PHY, MII_BMCR, 2),
    vnd_until_clear(BMCR_RESET, 1, 500, 1),
    vnd_out16(CMD_WRITE_MII, VND_PHY, MII_ADVERTISE, ADVERTISE_ALL_CSMA_PAUSE),
    vnd_out16(CMD_WRITE_MII, VND_PHY, MII_CTRL1000, ADVERTISE_1000FULL),
    vnd_out(CMD_SET_HW_MII, 0, 0),
    vnd_out(CMD_WRITE_MEDIUM, MEDIUM_178_DEFAULT, 0),
    ASIX_ENTER_MII,
    vnd_in(CMD_READ_MII, VND_PHY, MII_BMCR, 2),
    vnd_hook(H178_RESTART),
    vnd_out_data(CMD_WRITE_MII, VND_PHY, MII_BMCR, 2),
    vnd_out(CMD_SET_HW_MII, 0, 0),
    vnd_out_mac(CMD_WRITE_NODE_ID, 0, 0),
    vnd_out(CMD_WRITE_RX_CTL, RX_CTL_ALL_MULTI, 0),
    vnd_hook(H_UP_DONE),
};

static constexpr VndOp LINK_178_OPS[] = {
    ASIX_ENTER_MII,
    vnd_in(CMD_READ_MII, VND_PHY, MII_ADVERTISE, 2),
    vnd_hook(H_ADVERTISE),
    vnd_in(CMD_READ_MII, VND_PHY, MII_LPA, 2),
    vnd_hook(H178_LPA),
    vnd_in(CMD_READ_MII, VND_PHY, MII_CTRL1000, 2),
    vnd_hook(H178_CTRL1000),
    vnd_in(CMD_READ_MII, VND_PHY, MII_STAT1000, 2),
    vnd_hook(H178_STAT1000),
    vnd_out(CMD_SET_HW_MII, 0, 0),
    vnd_out(CMD_WRITE_MEDIUM, VND_VALUE, 0),
    vnd_hook(H_LINK_DONE),
};

#undef ASIX_ENTER_MII

const USBEthernet::VidPid USBEthernet::ASIX_178_IDS[] = {
    {0x08DD, 0x0114}, {0x0411, 0x006E}, {0x0DF6, 0x0056}, {0x0DF6, 0x061C}, {0x0B95, 0x1780},
    {0x0789, 0x0160}, {0x1737, 0x0039}, {0x04BB, 0x0930}, {0x050D, 0x5055}, {0x14EA, 0xAB11},
};
const size_t USBEthernet::ASIX_178_ID_COUNT = sizeof(USBEthernet::ASIX_178_IDS) / sizeof(USBEthernet::ASIX_178_IDS[0]);

const USBEthernet::VidPid USBEthernet::ASIX_IDS[] = {
    {0x13B1, 0x0018}, {0x1557, 0x7720}, {0x07D1, 0x3C05}, {0x2001, 0x3C05}, {0x2001, 0x1A02},
    {0x05AC, 0x1402}, {0x0B95, 0x772A}, {0x0DB0, 0xA877}, {0x0B95, 0x7720},
};
const size_t USBEthernet::ASIX_ID_COUNT = sizeof(USBEthernet::ASIX_IDS) / sizeof(USBEthernet::ASIX_IDS[0]);

const USBEthernet::VidPid USBEthernet::ASIX_EEPROM_MAC_IDS[] = {
    {0x17EF, 0x7203},
    {0x0B95, 0x772B},
    {0x0B95, 0x7E2B},
    {0x066B, 0x20F9},
};
const size_t USBEthernet::ASIX_EEPROM_MAC_ID_COUNT =
    sizeof(USBEthernet::ASIX_EEPROM_MAC_IDS) / sizeof(USBEthernet::ASIX_EEPROM_MAC_IDS[0]);

void USBEthernet::asix_begin_(uint32_t generation) {
  if (!this->alloc_rx_assembly_()) {
    this->vnd_fail_();
    return;
  }
  this->asix_rx_reset_();
  const usb_device_desc_t *dev = this->get_device_desc_();
  this->asix_178_ = dev != nullptr && std::any_of(ASIX_178_IDS, ASIX_178_IDS + ASIX_178_ID_COUNT, [dev](const VidPid &id) {
    return id.vid == dev->idVendor && id.pid == dev->idProduct;
  });
  if (this->asix_178_) {
    this->vnd_script_(generation, INIT_178_OPS, vnd_count(INIT_178_OPS));
    return;
  }
  this->asix_eeprom_mac_ = dev != nullptr && std::any_of(ASIX_EEPROM_MAC_IDS, ASIX_EEPROM_MAC_IDS + ASIX_EEPROM_MAC_ID_COUNT,
                                                         [dev](const VidPid &id) {
                                                           return id.vid == dev->idVendor && id.pid == dev->idProduct;
                                                         });
  if (this->asix_eeprom_mac_) {
    this->vnd_script_(generation, MAC_EEPROM_OPS, vnd_count(MAC_EEPROM_OPS));
  } else {
    this->vnd_script_(generation, MAC_NODE_OPS, vnd_count(MAC_NODE_OPS));
  }
}

uint8_t USBEthernet::asix_hook_(uint32_t generation, uint8_t id) {
  const bool embedded = (this->vnd_phy_ & 0x1F) == EMBEDDED_PHY;
  switch (id) {
    case H_EE0:
    case H_EE1:
    case H_EE2: {
      const size_t word = id - H_EE0;
      this->mac_[word * 2] = static_cast<uint8_t>(this->vnd_data_);
      this->mac_[word * 2 + 1] = static_cast<uint8_t>(this->vnd_data_ >> 8);
      if (id == H_EE2)
        this->mac_valid_ = true;
      return 0;
    }
    case H_MAC:
      this->vnd_check_mac_();
      this->vnd_script_(generation, PROBE_OPS, vnd_count(PROBE_OPS));
      return 0;
    case H_PHY: {
      const uint8_t phy = static_cast<uint8_t>(this->vnd_data_ >> 8);
      if (phy >= 32) {
        this->vnd_fail_();
        return 0;
      }
      this->vnd_phy_ = phy;
      return 0;
    }
    case H_CHIP:
      this->vnd_chip_ = static_cast<uint8_t>(this->vnd_data_) & CHIPCODE_MASK;
      ESP_LOGI(TAG, "ASIX chip code 0x%02X, PHY %u", this->vnd_chip_, this->vnd_phy_);
      if (this->vnd_chip_ == CHIP_AX88772) {
        this->vnd_script_(generation, RESET_772_OPS, vnd_count(RESET_772_OPS));
      } else {
        this->vnd_script_(generation, RESET_A_OPS, vnd_count(RESET_A_OPS));
      }
      return 0;
    case H_SELECT_772:
      this->vnd_data_ = embedded ? 1 : 0;
      return 0;
    case H_SELECT_A:
      this->vnd_data_ = (embedded ? 1 : 0) | PHYSEL_SSEN;
      return 0;
    case H_IF_EMBEDDED:
      return embedded ? 0 : 5;
    case H_IF_NOT_EMBEDDED:
      return embedded ? 1 : 0;
    case H_IF_B:
      return this->vnd_chip_ == CHIP_AX88772B ? 0 : 1;
    case H_IF_A:
      return this->vnd_chip_ == CHIP_AX88772A ? 0 : 8;
    case H_RESET_DONE:
      this->vnd_script_(generation, UP_OPS, vnd_count(UP_OPS));
      return 0;
    case H_UP_DONE:
      this->vnd_start_data_(generation);
      return 0;
    case H_ADVERTISE:
      this->vnd_saved_ = static_cast<uint16_t>(this->vnd_data_);
      return 0;
    case H_LPA: {
      const uint16_t common = this->vnd_saved_ & static_cast<uint16_t>(this->vnd_data_);
      const bool speed_100 = (common & (LPA_100FULL | LPA_100HALF)) != 0;
      const bool full = (common & LPA_100FULL) != 0 || (!speed_100 && (common & LPA_10FULL) != 0);
      uint16_t mode = MEDIUM_DEFAULT;
      if (!speed_100)
        mode &= ~MEDIUM_PS;
      if (!full)
        mode &= ~MEDIUM_FD;
      this->vnd_data_ = mode;
      this->link_speed_.store(speed_100 ? 100000000 : 10000000, std::memory_order_release);
      return 0;
    }
    case H_LINK_DONE:
      this->vnd_link_done_(generation);
      return 0;
    case H178_MAC:
      this->vnd_check_mac_();
      return 0;
    case H178_EEPROM: {
      const uint16_t eeprom = static_cast<uint16_t>(this->vnd_data_);
      this->vnd_saved_ = eeprom;
      this->vnd_chip_ = eeprom == 0xFFFF ? PHY_MODE_MARVELL : (eeprom & 0x7F);
      this->asix_ledmode_ = eeprom == 0xFFFF ? 0 : static_cast<uint8_t>(eeprom >> 8);
      return 0;
    }
    case H178_IF_GPIO_NORMAL:
      return (this->vnd_saved_ >> 8) != 1 ? 0 : 6;
    case H178_IF_GPIO_LED1:
      return (this->vnd_saved_ >> 8) == 1 ? 0 : 4;
    case H178_IF_MARVELL:
      return this->vnd_chip_ == PHY_MODE_MARVELL ? 0 : 11;
    case H178_IF_MARVELL_LED:
      return this->asix_ledmode_ != 0 ? 0 : 3;
    case H178_MARVELL_LED:
      this->vnd_data_ = (this->vnd_data_ & 0xF8FF) | (1 + 0x0100);
      return 0;
    case H178_IF_RTL8211:
      return this->vnd_chip_ == PHY_MODE_RTL8211CL ? 0 : 15;
    case H178_BIT_80:
      this->vnd_data_ |= 0x0080;
      return 0;
    case H178_IF_RTL8211_LED:
      return this->asix_ledmode_ == 12 ? 0 : 3;
    case H178_RESTART:
      if ((this->vnd_data_ & BMCR_AN_ENABLE) == 0)
        return 1;
      this->vnd_data_ |= BMCR_RESTART;
      return 0;
    case H178_LPA:
      this->vnd_saved_ &= static_cast<uint16_t>(this->vnd_data_);
      return 0;
    case H178_CTRL1000:
      this->asix_gmii_ = static_cast<uint16_t>(this->vnd_data_ << 2);
      return 0;
    case H178_STAT1000: {
      const uint16_t giga = this->asix_gmii_ & static_cast<uint16_t>(this->vnd_data_);
      const bool speed_1000 = (giga & (LPA_1000FULL | LPA_1000HALF)) != 0;
      const bool speed_100 = !speed_1000 && (this->vnd_saved_ & (LPA_100FULL | LPA_100HALF)) != 0;
      const bool full = speed_1000 ? (giga & LPA_1000FULL) != 0
                        : speed_100 ? (this->vnd_saved_ & LPA_100FULL) != 0
                                    : (this->vnd_saved_ & LPA_10FULL) != 0;
      uint16_t mode = MEDIUM_178_DEFAULT | MEDIUM_ENCK;
      if (speed_1000) {
        mode |= MEDIUM_GM;
      } else if (!speed_100) {
        mode &= ~(MEDIUM_PS | MEDIUM_GM);
      }
      if (full) {
        mode |= MEDIUM_FD;
      } else {
        mode &= ~MEDIUM_FD;
      }
      this->vnd_data_ = mode;
      this->link_speed_.store(speed_1000 ? 1000000000 : speed_100 ? 100000000 : 10000000, std::memory_order_release);
      return 0;
    }
    default:
      return 0;
  }
}

bool USBEthernet::asix_link_script_(uint32_t generation) {
  if (this->asix_178_) {
    this->vnd_script_(generation, LINK_178_OPS, vnd_count(LINK_178_OPS));
  } else {
    this->vnd_script_(generation, LINK_OPS, vnd_count(LINK_OPS));
  }
  return true;
}

void USBEthernet::asix_rx_reset_() {
  this->asix_remaining_ = 0;
  this->asix_split_ = false;
  this->asix_header_ = 0;
  this->rx_assembly_len_ = 0;
}

void USBEthernet::handle_asix_rx_(const uint8_t *data, size_t len) {
  if (this->rx_assembly_ == nullptr)
    return;
  size_t offset = 0;
  if (this->asix_remaining_ != 0 && ((this->asix_remaining_ + 1u) & 0xFFFEu) + 4u <= len) {
    const uint32_t header = vnd_le32(data + ((this->asix_remaining_ + 1) & 0xFFFE));
    if ((header & RX_SIZE_MASK) != ((~header >> 16) & RX_SIZE_MASK))
      this->asix_rx_reset_();
  }
  while (offset + 2 <= len) {
    if (this->asix_remaining_ == 0) {
      if (len - offset == 2) {
        this->asix_header_ = vnd_le16(data + offset);
        this->asix_split_ = true;
        offset += 2;
        break;
      }
      if (this->asix_split_) {
        this->asix_header_ |= static_cast<uint32_t>(vnd_le16(data + offset)) << 16;
        this->asix_split_ = false;
        offset += 2;
      } else {
        if (len - offset < 4) {
          this->asix_rx_reset_();
          return;
        }
        this->asix_header_ = vnd_le32(data + offset);
        offset += 4;
      }
      const uint16_t size = static_cast<uint16_t>(this->asix_header_) & RX_SIZE_MASK;
      if (size != ((~this->asix_header_ >> 16) & RX_SIZE_MASK) || size > FRAME_MAX) {
        this->asix_rx_reset_();
        return;
      }
      this->asix_remaining_ = size;
      this->rx_assembly_len_ = 0;
    }
    const size_t copy = std::min<size_t>(this->asix_remaining_, len - offset);
    this->asix_remaining_ -= copy;
    if (this->asix_remaining_ == 0 && this->rx_assembly_len_ == 0) {
      this->deliver_(data + offset, copy);
    } else {
      memcpy(this->rx_assembly_ + this->rx_assembly_len_, data + offset, copy);
      this->rx_assembly_len_ += copy;
      if (this->asix_remaining_ == 0) {
        this->deliver_(this->rx_assembly_, this->rx_assembly_len_);
        this->rx_assembly_len_ = 0;
      }
    }
    offset += (copy + 1) & 0xFFFE;
  }
  if (offset != len)
    this->asix_rx_reset_();
}

size_t USBEthernet::asix_fill_tx_(uint8_t *out, const uint8_t *frame, size_t len) {
  if (len + TX_HEADER_SIZE + TX_PADDING_SIZE > this->tx_size_)
    return 0;
  vnd_put32(out, ((static_cast<uint32_t>(len) ^ 0x0000FFFF) << 16) + static_cast<uint32_t>(len));
  memcpy(out + TX_HEADER_SIZE, frame, len);
  size_t total = TX_HEADER_SIZE + len;
  if ((total & (this->out_mps_ - 1u)) == 0) {
    vnd_put32(out + total, PAD_BYTES);
    total += TX_PADDING_SIZE;
  }
  return total;
}

}

#endif
