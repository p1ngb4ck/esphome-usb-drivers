#include "usb_ethernet.h"

#ifdef USE_USB_ETHERNET

#include "esphome/core/log.h"

#include "esp_heap_caps.h"
#include "usb/usb_helpers.h"

#include <algorithm>
#include <cinttypes>
#include <cstring>

namespace esphome::usb_ethernet {

static const char *const TAG = "usb_ethernet";

enum class RtlKind : uint8_t {
  WRITE,
  WRITE_SPEED,
  WRITE_VALUE,
  OCP_WRITE,
  OCP_MODIFY,
  CHAIN,
  SKIP_UNLESS_ALL,
  SKIP_NOT_EXT,
  MODIFY,
  MODIFY_V1,
  WAIT_SET,
  WAIT_CLEAR,
  WAIT_PHY,
  DELAY,
  MAC_LOW,
  MAC_HIGH,
  MAC_READ_LOW,
  MAC_READ_HIGH,
  STATUS,
  EEE,
  SNAPSHOT,
  ONLY,
  SKIP_ZERO,
  SKIP_NONZERO,
  SKIP_NOT_HALF,
  EFUSE_HIGH,
  EFUSE_LOW,
};

struct RtlOp {
  RtlKind kind;
  uint8_t width;
  uint16_t mcu;
  uint16_t reg;
  uint32_t clear;
  uint32_t set;
};

static constexpr uint8_t RTL_REQT_READ = 0xC0;
static constexpr uint8_t RTL_REQT_WRITE = 0x40;
static constexpr uint8_t RTL_REQ_REGS = 0x05;
static constexpr uint16_t MCU_PLA = 0x0100;
static constexpr uint16_t MCU_USB = 0x0000;
static constexpr uint8_t BYTE_ENABLE_DWORD = 0xFF;
static constexpr uint8_t BYTE_ENABLE_WORD = 0x33;
static constexpr uint8_t BYTE_ENABLE_BYTE = 0x11;

static constexpr uint16_t RTL_REG_IDR = 0xC000;
static constexpr uint16_t RTL_REG_RCR = 0xC010;
static constexpr uint16_t RTL_REG_RMS = 0xC016;
static constexpr uint16_t RTL_REG_RXFIFO_CTRL0 = 0xC0A0;
static constexpr uint16_t RTL_REG_RXFIFO_CTRL1 = 0xC0A4;
static constexpr uint16_t RTL_REG_RXFIFO_CTRL2 = 0xC0A8;
static constexpr uint16_t RTL_REG_FMC = 0xC0B4;
static constexpr uint16_t RTL_REG_TEREDO_CFG = 0xC0BC;
static constexpr uint16_t RTL_REG_MAR = 0xCD00;
static constexpr uint16_t RTL_REG_TEREDO_TIMER = 0xD2CC;
static constexpr uint16_t RTL_REG_REALWOW_TIMER = 0xD2E8;
static constexpr uint16_t RTL_REG_LED_FEATURE = 0xDD92;
static constexpr uint16_t RTL_REG_GPHY_INTR_IMR = 0xE022;
static constexpr uint16_t RTL_REG_EEEP_CR = 0xE080;
static constexpr uint16_t RTL_REG_MAC_PWR_CTRL = 0xE0C0;
static constexpr uint16_t RTL_REG_WDT6_CTRL = 0xE428;
static constexpr uint16_t RTL_REG_TCR0 = 0xE610;
static constexpr uint16_t RTL_REG_TXFIFO_CTRL = 0xE618;
static constexpr uint16_t RTL_REG_RSTTALLY = 0xE800;
static constexpr uint16_t RTL_REG_CR = 0xE813;
static constexpr uint16_t RTL_REG_CRWECR = 0xE81C;
static constexpr uint16_t RTL_REG_PHY_PWR = 0xE84C;
static constexpr uint16_t RTL_REG_OOB_CTRL = 0xE84F;
static constexpr uint16_t RTL_REG_CPCR = 0xE854;
static constexpr uint16_t RTL_REG_MISC_1 = 0xE85A;
static constexpr uint16_t RTL_REG_OCP_GPHY_BASE = 0xE86C;
static constexpr uint16_t RTL_REG_SFF_STS_7 = 0xE8DE;
static constexpr uint16_t RTL_REG_PHYSTATUS = 0xE908;
static constexpr uint16_t RTL_REG_USB_CTRL = 0xD406;
static constexpr uint16_t RTL_REG_TX_AGG = 0xD40A;
static constexpr uint16_t RTL_REG_RX_BUF_TH = 0xD40C;
static constexpr uint16_t RTL_REG_PM_CTRL_STATUS = 0xD432;
static constexpr uint16_t RTL_REG_TX_DMA = 0xD434;
static constexpr uint16_t RTL_REG_UPS_CTRL = 0xD800;

static constexpr uint16_t RTL_REG_BACKUP = 0xD000;
static constexpr uint16_t RTL_REG_BOOT_CTRL = 0xE004;
static constexpr uint16_t RTL_REG_MTPS = 0xE615;
static constexpr uint16_t RTL_REG_DMY_REG0 = 0xC0B0;
static constexpr uint16_t RTL_REG_CONFIG6 = 0xE90A;
static constexpr uint16_t RTL_REG_LWAKE_CTRL = 0xE007;
static constexpr uint16_t RTL_REG_EXTRA_STATUS = 0xD398;
static constexpr uint16_t RTL_REG_INDICATE_FLAG = 0xD38C;
static constexpr uint16_t RTL_REG_SUSPEND_FLAG = 0xD38A;
static constexpr uint16_t RTL_REG_MAC_PWR_CTRL2 = 0xE0CA;
static constexpr uint16_t RTL_REG_MAC_PWR_CTRL3 = 0xE0CC;
static constexpr uint16_t RTL_REG_MAC_PWR_CTRL4 = 0xE0CE;
static constexpr uint16_t RTL_REG_TCR1 = 0xE612;
static constexpr uint16_t RTL_REG_EFUSE_DATA = 0xDD00;
static constexpr uint16_t RTL_REG_EFUSE_CMD = 0xDD02;
static constexpr uint16_t RTL_REG_EEE_CR = 0xE040;
static constexpr uint16_t RTL_REG_ALDPS_DONE = 0xE000;
static constexpr uint16_t RTL_REG_TEST_IO = 0xDC00;
static constexpr uint16_t USB_REG_TOLERANCE = 0xD490;
static constexpr uint16_t USB_REG_U2P3_CTRL = 0xB460;
static constexpr uint16_t USB_REG_LPM_CTRL = 0xD41A;
static constexpr uint16_t USB_REG_AFE_CTRL2 = 0xD824;
static constexpr uint16_t USB_REG_CONNECT_TIMER = 0xCBF8;
static constexpr uint16_t USB_REG_POWER_CUT = 0xD80A;
static constexpr uint16_t USB_REG_MISC_0 = 0xD81A;
static constexpr uint16_t USB_REG_MISC_2 = 0xCFFF;
static constexpr uint16_t USB_REG_SSPHYLINK1 = 0xB426;
static constexpr uint16_t USB_REG_SSPHYLINK2 = 0xB428;
static constexpr uint16_t USB_REG_USB2PHY = 0xB41E;
static constexpr uint16_t USB_REG_BURST_SIZE = 0xCFC0;
static constexpr uint16_t USB_REG_CSR_DUMMY1 = 0xB464;
static constexpr uint16_t USB_REG_CSR_DUMMY2 = 0xB466;
static constexpr uint16_t USB_REG_WDT11_CTRL = 0xE43C;
static constexpr uint16_t USB_REG_RX_EARLY_TIMEOUT = 0xD42C;
static constexpr uint16_t USB_REG_RX_EARLY_SIZE = 0xD42E;
static constexpr uint16_t USB_REG_RX_EXTRA_AGGR = 0xD432;
static constexpr uint16_t USB_REG_BMU_RESET = 0xD4B0;
static constexpr uint16_t USB_REG_LPM_CONFIG = 0xCFD8;
static constexpr uint16_t USB_REG_MSC_TIMER = 0xCBFC;
static constexpr uint16_t USB_REG_U1U2_TIMER = 0xD4DA;
static constexpr uint16_t USB_REG_FW_TASK = 0xD4E8;
static constexpr uint16_t USB_REG_UPT_RXDMA_OWN = 0xD437;

static constexpr uint16_t OCP_PAGE_ALDPS = 0x2000;
static constexpr uint16_t OCP_PAGE_MII = 0xA000;
static constexpr uint16_t OCP_PAGE_ADC = 0xB000;
static constexpr uint16_t OCP_ALDPS = 0xB010;
static constexpr uint16_t OCP_MII_BMCR = 0xB400;
static constexpr uint16_t OCP_MII_ADVERTISE = 0xB408;
static constexpr uint16_t OCP_MII_CTRL1000 = 0xB412;
static constexpr uint16_t OCP_PHY_STATUS = 0xB420;
static constexpr uint16_t OCP_NCTL_CFG = 0xB42C;
static constexpr uint16_t OCP_POWER_CFG = 0xB430;
static constexpr uint16_t OCP_EEE_CFG = 0xB432;
static constexpr uint16_t OCP_SRAM_ADDR = 0xB436;
static constexpr uint16_t OCP_SRAM_DATA = 0xB438;
static constexpr uint16_t OCP_DOWN_SPEED = 0xB442;
static constexpr uint16_t OCP_EEE_ADV = 0xB5D0;
static constexpr uint16_t OCP_ADC_CFG = 0xBC06;
static constexpr uint16_t OCP_ADC_IOFFSET = 0xBCFC;
static constexpr uint16_t SRAM_GREEN_CFG = 0x8011;
static constexpr uint16_t SRAM_LPF_CFG = 0x8012;
static constexpr uint16_t SRAM_10M_AMP1 = 0x8080;
static constexpr uint16_t SRAM_10M_AMP2 = 0x8082;
static constexpr uint16_t SRAM_IMPEDANCE = 0x8084;

static constexpr uint32_t ALDPS_OFF = 0x0200 | 0x0100 | 0x0010;
static constexpr uint32_t ALDPS_ON = 0x8000 | ALDPS_OFF;
static constexpr uint32_t PHY_POWER_DOWN = 0x0800;
static constexpr uint32_t PHY_AUTONEG_RESTART = 0x8000 | 0x1000 | 0x0200;
static constexpr uint32_t ADVERTISE_ALL = 0x0020 | 0x0040 | 0x0080 | 0x0100;
static constexpr uint32_t ADVERTISE_PAUSE = 0x0400 | 0x0800;
static constexpr uint32_t ADVERTISE_1000FULL = 0x0200;
static constexpr uint32_t ADVERTISE_1000_MASK = 0x0200 | 0x0100;
static constexpr uint32_t RCR_ACCEPT_ALL = 0x0F;
static constexpr uint32_t RCR_ACCEPT_NORMAL = 0x02 | 0x04 | 0x08;
static constexpr uint32_t CR_RESET = 0x10;
static constexpr uint32_t CR_RX_TX = 0x08 | 0x04;
static constexpr uint32_t OOB_NOW = 0x80;
static constexpr uint32_t OOB_LINK_LIST_READY = 0x02;
static constexpr uint32_t SFF_RE_INIT_LL = 0x8000;
static constexpr uint32_t SFF_MCU_BORW = 0x4000;
static constexpr uint32_t RXDY_GATED = 0x0008;
static constexpr uint32_t FMC_MCU_ENABLE = 0x0001;
static constexpr uint32_t EEE_PLUS_TX = 0x0002;
static constexpr uint32_t TEREDO_OFF_MASK = 0x8000 | 0x00FE | 0x0001;
static constexpr uint32_t MCU_CLOCK_MASK = 0x0F0F0F0F;
static constexpr uint32_t MCU_CLOCK_RATIO = 0x07010F07 | 0x00004000;

static constexpr uint32_t AUTOLOAD_DONE = 0x0002;
static constexpr uint32_t PHY_STAT_ANY = 0;
static constexpr uint32_t PHY_STAT_EXT_INIT = 2;
static constexpr uint32_t PHY_STAT_LAN_ON = 3;
static constexpr uint32_t PHY_STAT_PWRDN = 5;
static constexpr uint32_t PHY_STAT_MASK = 0x0007;
static constexpr uint32_t EN_ALDPS = 0x0004;
static constexpr uint32_t EEE_RX_TX = 0x0003;
static constexpr uint32_t EEE10_EN = 0x0010;
static constexpr uint32_t EEE_CLKDIV_EN = 0x8000;
static constexpr uint32_t EN_10M_BGOFF = 0x0080;
static constexpr uint32_t EN_10M_PLLOFF = 0x0001;
static constexpr uint32_t CTAP_SHORT_EN = 0x0040;
static constexpr uint32_t PFM_PWM_SWITCH = 0x0040;
static constexpr uint32_t ADC_CFG_DEFAULT = 0x0100 | 0x0080 | 0x0040;
static constexpr uint32_t U2P3_ENABLE = 0x0001;
static constexpr uint32_t CUR_LINK_OK = 0x8000;
static constexpr uint32_t POLL_LINK_CHG = 0x0001;
static constexpr uint32_t LINK_CHANGE_FLAG = 0x0100;
static constexpr uint32_t LINK_STATUS = 0x02;
static constexpr uint32_t RX_AGG_OFF_MASK = 0x0010 | 0x0080;
static constexpr uint32_t BMU_RESET_BOTH = 0x01 | 0x02;
static constexpr uint32_t PWR_EN = 0x0001;
static constexpr uint32_t PHASE2_EN = 0x0008;
static constexpr uint32_t PCUT_STATUS = 0x0001;
static constexpr uint32_t LPM_U1U2_EN = 0x0001;
static constexpr uint32_t UPS_EN_PREWAKE = 0x10 | 0x20;
static constexpr uint32_t UPS_FORCE_PWR_DOWN = 0x01;
static constexpr uint32_t MAC_CLK_SPDWN_EN = 0x8000;
static constexpr uint32_t PLA_MCU_SPDWN_EN = 0x4000;
static constexpr uint32_t TEST_IO_OFF = 0x0010;
static constexpr uint32_t TX10MIDLE_EN = 0x0100;
static constexpr uint32_t IFG_MASK = 0x0008 | 0x0200 | 0x0100;
static constexpr uint32_t IFG_96NS = 0x0200 | 0x0100;
static constexpr uint32_t IFG_144NS = 0x0200;
static constexpr uint32_t GREEN_ETH_EN = 0x8000;
static constexpr uint32_t R_TUNE_EN = 0x0800;
static constexpr uint32_t PGA_RETURN_EN = 0x0002;
static constexpr uint32_t EFUSE_READ_ADC = 0x8000 | 0x7D;
static constexpr uint32_t FC_PATCH_TASK = 0x0002;
static constexpr uint32_t OWN_UPDATE_CLEAR = 0x01 | 0x02;

static constexpr uint32_t CHIP_VERSION_MASK = 0x7CF0;
static constexpr uint32_t CHIP_VERSION_01 = 0x4C00;
static constexpr uint32_t CHIP_VERSION_02 = 0x4C10;
static constexpr uint32_t CHIP_VERSION_03 = 0x5C00;
static constexpr uint32_t CHIP_VERSION_04 = 0x5C10;
static constexpr uint32_t CHIP_VERSION_05 = 0x5C20;
static constexpr uint32_t CHIP_VERSION_06 = 0x5C30;
static constexpr uint32_t CHIP_VERSION_07 = 0x4800;
static constexpr uint32_t CHIP_VERSION_08 = 0x6000;
static constexpr uint32_t CHIP_VERSION_09 = 0x6010;

static constexpr uint16_t STATUS_1000 = 0x10;
static constexpr uint16_t STATUS_100 = 0x08;
static constexpr uint16_t STATUS_10 = 0x04;
static constexpr uint32_t RTL_LINK_1000 = 1000000000;
static constexpr uint32_t RTL_LINK_100 = 100000000;
static constexpr uint32_t RTL_LINK_10 = 10000000;
static constexpr uint16_t INTERRUPT_LINK = 0x0004;

static constexpr size_t RX_DESC_SIZE = 24;
static constexpr size_t RX_ALIGN = 8;
static constexpr uint32_t RX_LENGTH_MASK = 0x7FFF;
static constexpr size_t FRAME_MIN = 60;
static constexpr size_t FRAME_MAX = 1536;
static constexpr size_t RX_ASSEMBLY_LIMIT = 2048;
static constexpr size_t FCS_SIZE = 4;
static constexpr uint32_t TX_FIRST_LAST = 0xC0000000;
static constexpr uint16_t WAIT_TRIES = 1000;
static constexpr uint16_t DELAYED_WAIT_TRIES = 500;
static constexpr uint32_t ALDPS_SETTLE_MS = 20;
static constexpr uint16_t HIGH_SPEED_BULK = 512;

static constexpr RtlOp write_op(uint8_t width, uint16_t mcu, uint16_t reg, uint32_t value) {
  return {RtlKind::WRITE, width, mcu, reg, 0, value};
}
static constexpr RtlOp modify_op(uint8_t width, uint16_t mcu, uint16_t reg, uint32_t clear, uint32_t set) {
  return {RtlKind::MODIFY, width, mcu, reg, clear, set};
}
static constexpr RtlOp wait_ready_op() {
  return {RtlKind::WAIT_SET, 1, MCU_PLA, RTL_REG_OOB_CTRL, 0, OOB_LINK_LIST_READY};
}
static constexpr RtlOp delay_op(uint32_t ms) { return {RtlKind::DELAY, 0, 0, 0, 0, ms}; }
static constexpr RtlOp ocp_base_op(uint16_t base) { return write_op(2, MCU_PLA, RTL_REG_OCP_GPHY_BASE, base); }
static constexpr RtlOp wait_phy_op(uint32_t desired) {
  return {RtlKind::WAIT_PHY, 2, MCU_PLA, OCP_PHY_STATUS, 20, desired};
}
static constexpr RtlOp only_op(uint8_t count, uint32_t versions) { return {RtlKind::ONLY, count, 0, 0, 0, versions}; }
static constexpr RtlOp skip_zero_op(uint8_t width, uint16_t mcu, uint16_t reg, uint32_t mask, uint8_t count) {
  return {RtlKind::SKIP_ZERO, width, mcu, reg, count, mask};
}
static constexpr RtlOp sram_addr_op(uint16_t address) { return write_op(2, MCU_PLA, OCP_SRAM_ADDR, address); }
static constexpr RtlOp sram_data_op(uint16_t value) { return write_op(2, MCU_PLA, OCP_SRAM_DATA, value); }
static constexpr uint32_t version_bit(uint8_t version) { return 1u << version; }

static constexpr uint32_t V03 = version_bit(3);
static constexpr uint32_t V04 = version_bit(4);
static constexpr uint32_t V05 = version_bit(5);
static constexpr uint32_t V06 = version_bit(6);
static constexpr uint32_t V09 = version_bit(9);

static constexpr RtlOp INIT_OPS[] = {
    write_op(2, MCU_PLA, RTL_REG_OCP_GPHY_BASE, OCP_PAGE_MII),
    modify_op(2, MCU_PLA, OCP_MII_BMCR, PHY_POWER_DOWN, 0),
    write_op(2, MCU_PLA, RTL_REG_OCP_GPHY_BASE, OCP_PAGE_ALDPS),
    write_op(2, MCU_PLA, OCP_ALDPS, ALDPS_OFF),
    delay_op(ALDPS_SETTLE_MS),
    {RtlKind::MODIFY_V1, 2, MCU_PLA, RTL_REG_LED_FEATURE, 0x0700, 0},
    modify_op(2, MCU_USB, RTL_REG_UPS_CTRL, 0x0100, 0),
    modify_op(2, MCU_USB, RTL_REG_PM_CTRL_STATUS, 0x0001, 0),
    modify_op(2, MCU_PLA, RTL_REG_PHY_PWR, 0, 0x0080 | 0x0040),
    modify_op(4, MCU_PLA, RTL_REG_MAC_PWR_CTRL, MCU_CLOCK_MASK, MCU_CLOCK_RATIO),
    write_op(2, MCU_PLA, RTL_REG_GPHY_INTR_IMR, 0x000F),
    modify_op(2, MCU_PLA, RTL_REG_RSTTALLY, 0, 0x0001),
    modify_op(2, MCU_USB, RTL_REG_USB_CTRL, 0x0010 | 0x0080, 0),
    write_op(2, MCU_PLA, OCP_ALDPS, ALDPS_OFF),
    delay_op(ALDPS_SETTLE_MS),
    modify_op(4, MCU_PLA, RTL_REG_RCR, RCR_ACCEPT_ALL, 0),
    modify_op(2, MCU_PLA, RTL_REG_MISC_1, 0, RXDY_GATED),
    modify_op(2, MCU_PLA, RTL_REG_TEREDO_CFG, TEREDO_OFF_MASK, 0),
    write_op(2, MCU_PLA, RTL_REG_WDT6_CTRL, 0x0010),
    write_op(2, MCU_PLA, RTL_REG_REALWOW_TIMER, 0),
    write_op(4, MCU_PLA, RTL_REG_TEREDO_TIMER, 0),
    write_op(1, MCU_PLA, RTL_REG_CRWECR, 0x00),
    write_op(1, MCU_PLA, RTL_REG_CR, 0x00),
    modify_op(1, MCU_PLA, RTL_REG_OOB_CTRL, OOB_NOW, 0),
    modify_op(2, MCU_PLA, RTL_REG_SFF_STS_7, SFF_MCU_BORW, 0),
    wait_ready_op(),
    modify_op(2, MCU_PLA, RTL_REG_SFF_STS_7, 0, SFF_RE_INIT_LL),
    wait_ready_op(),
    write_op(1, MCU_PLA, RTL_REG_CR, CR_RESET),
    {RtlKind::WAIT_CLEAR, 1, MCU_PLA, RTL_REG_CR, 0, CR_RESET},
    write_op(4, MCU_PLA, RTL_REG_RXFIFO_CTRL0, 0x00080002),
    {RtlKind::WRITE_SPEED, 4, MCU_PLA, RTL_REG_RXFIFO_CTRL1, 0x00000060, 0x00000038},
    {RtlKind::WRITE_SPEED, 4, MCU_PLA, RTL_REG_RXFIFO_CTRL2, 0x00000078, 0x00000048},
    write_op(4, MCU_PLA, RTL_REG_TXFIFO_CTRL, 0x01000008),
    write_op(1, MCU_USB, RTL_REG_TX_AGG, 0x03),
    write_op(4, MCU_USB, RTL_REG_RX_BUF_TH, 0x7A120180),
    write_op(4, MCU_USB, RTL_REG_TX_DMA, 0x00000001 | 0x00000100),
    modify_op(2, MCU_PLA, RTL_REG_CPCR, 0, 0x0040),
    write_op(2, MCU_PLA, RTL_REG_RMS, 1518),
    modify_op(2, MCU_PLA, RTL_REG_TCR0, 0, 0x0080),
    write_op(2, MCU_PLA, OCP_ALDPS, ALDPS_ON),
    write_op(1, MCU_PLA, RTL_REG_CRWECR, 0xC0),
    {RtlKind::MAC_LOW, 4, MCU_PLA, RTL_REG_IDR, 0, 0},
    {RtlKind::MAC_HIGH, 2, MCU_PLA, RTL_REG_IDR + 4, 0, 0},
    write_op(1, MCU_PLA, RTL_REG_CRWECR, 0x00),
};

static constexpr RtlOp SPEED_OPS[] = {
    write_op(2, MCU_PLA, RTL_REG_OCP_GPHY_BASE, OCP_PAGE_MII),
    modify_op(2, MCU_PLA, OCP_MII_ADVERTISE, ADVERTISE_ALL, ADVERTISE_ALL),
    write_op(2, MCU_PLA, OCP_MII_BMCR, PHY_AUTONEG_RESTART),
};

static constexpr RtlOp ENABLE_OPS[] = {
    {RtlKind::STATUS, 2, MCU_PLA, RTL_REG_PHYSTATUS, 0, 0},
    {RtlKind::EEE, 2, MCU_PLA, RTL_REG_EEEP_CR, EEE_PLUS_TX, 0},
    modify_op(2, MCU_PLA, RTL_REG_FMC, FMC_MCU_ENABLE, 0),
    modify_op(2, MCU_PLA, RTL_REG_FMC, 0, FMC_MCU_ENABLE),
    modify_op(1, MCU_PLA, RTL_REG_CR, 0, CR_RX_TX),
    modify_op(2, MCU_PLA, RTL_REG_MISC_1, RXDY_GATED, 0),
    write_op(4, MCU_PLA, RTL_REG_MAR, 0xFFFFFFFF),
    write_op(4, MCU_PLA, RTL_REG_MAR + 4, 0xFFFFFFFF),
    modify_op(4, MCU_PLA, RTL_REG_RCR, RCR_ACCEPT_ALL, RCR_ACCEPT_NORMAL),
    {RtlKind::SNAPSHOT, 4, MCU_PLA, RTL_REG_CR, 0, 0},
    {RtlKind::SNAPSHOT, 4, MCU_PLA, RTL_REG_TCR0, 0, 1},
    {RtlKind::SNAPSHOT, 4, MCU_PLA, RTL_REG_PHY_PWR, 0, 2},
    {RtlKind::SNAPSHOT, 4, MCU_USB, RTL_REG_TX_DMA, 0, 3},
};

#define R8153_TOLERANCE(value) \
  write_op(4, MCU_USB, USB_REG_TOLERANCE, value), write_op(2, MCU_USB, USB_REG_TOLERANCE + 4, (value) & 0xFFFF)
#define R8153_BOOT_AND_PHY \
  {RtlKind::WAIT_SET, 2, MCU_PLA, RTL_REG_BOOT_CTRL, 20, AUTOLOAD_DONE}, ocp_base_op(OCP_PAGE_MII), \
      wait_phy_op(PHY_STAT_ANY)
#define R8153_PHY_ON \
  ocp_base_op(OCP_PAGE_MII), modify_op(2, MCU_PLA, OCP_MII_BMCR, PHY_POWER_DOWN, 0), wait_phy_op(PHY_STAT_LAN_ON), \
      modify_op(2, MCU_USB, USB_REG_U2P3_CTRL, U2P3_ENABLE, 0)
#define R8153_QUEUE_WAKE_OFF \
  modify_op(1, MCU_PLA, RTL_REG_INDICATE_FLAG, 0x01, 0), modify_op(1, MCU_PLA, RTL_REG_SUSPEND_FLAG, 0x01, 0), \
      modify_op(2, MCU_PLA, RTL_REG_EXTRA_STATUS, LINK_CHANGE_FLAG, 0)
#define R8153_EXTRA_STATUS \
  modify_op(2, MCU_PLA, RTL_REG_EXTRA_STATUS, CUR_LINK_OK, POLL_LINK_CHG), \
      skip_zero_op(2, MCU_PLA, RTL_REG_PHYSTATUS, LINK_STATUS, 1), \
      modify_op(2, MCU_PLA, RTL_REG_EXTRA_STATUS, 0, CUR_LINK_OK | POLL_LINK_CHG)
#define R8153_ALDPS_OFF \
  ocp_base_op(OCP_PAGE_MII), modify_op(2, MCU_PLA, OCP_POWER_CFG, EN_ALDPS, 0), \
      {RtlKind::WAIT_SET, 2, MCU_PLA, RTL_REG_ALDPS_DONE, 2, 0x0100}
#define R8153_ALDPS_ON ocp_base_op(OCP_PAGE_MII), modify_op(2, MCU_PLA, OCP_POWER_CFG, 0, EN_ALDPS)
#define R8153_EEE_OFF \
  modify_op(2, MCU_PLA, RTL_REG_EEE_CR, EEE_RX_TX, 0), ocp_base_op(OCP_PAGE_MII), \
      modify_op(2, MCU_PLA, OCP_EEE_CFG, EEE10_EN, 0), write_op(2, MCU_PLA, OCP_EEE_ADV, 0)
#define R8153_EEE_ON \
  modify_op(2, MCU_PLA, RTL_REG_EEE_CR, 0, EEE_RX_TX), ocp_base_op(OCP_PAGE_MII), \
      modify_op(2, MCU_PLA, OCP_EEE_CFG, 0, EEE10_EN), write_op(2, MCU_PLA, OCP_EEE_ADV, 0x0006)
#define R8153_FLOW_CONTROL \
  ocp_base_op(OCP_PAGE_MII), modify_op(2, MCU_PLA, OCP_MII_ADVERTISE, 0, ADVERTISE_PAUSE)
#define R8153_NIC_RESET \
  write_op(1, MCU_PLA, RTL_REG_CR, CR_RESET), {RtlKind::WAIT_CLEAR, 1, MCU_PLA, RTL_REG_CR, 0, CR_RESET}
#define R8153_FIRST_INIT \
  modify_op(2, MCU_PLA, RTL_REG_MISC_1, 0, RXDY_GATED), write_op(2, MCU_PLA, RTL_REG_WDT6_CTRL, 0x0010), \
      write_op(2, MCU_PLA, RTL_REG_REALWOW_TIMER, 0), write_op(4, MCU_PLA, RTL_REG_TEREDO_TIMER, 0), \
      modify_op(4, MCU_PLA, RTL_REG_RCR, RCR_ACCEPT_ALL, 0), R8153_NIC_RESET, \
      modify_op(1, MCU_USB, USB_REG_BMU_RESET, BMU_RESET_BOTH, 0), \
      modify_op(1, MCU_USB, USB_REG_BMU_RESET, 0, BMU_RESET_BOTH), \
      modify_op(1, MCU_PLA, RTL_REG_OOB_CTRL, OOB_NOW, 0), \
      modify_op(2, MCU_PLA, RTL_REG_SFF_STS_7, SFF_MCU_BORW, 0), wait_ready_op(), \
      modify_op(2, MCU_PLA, RTL_REG_SFF_STS_7, 0, SFF_RE_INIT_LL), wait_ready_op(), \
      modify_op(2, MCU_PLA, RTL_REG_CPCR, 0, 0x0040), write_op(2, MCU_PLA, RTL_REG_RMS, 1522), \
      write_op(1, MCU_PLA, RTL_REG_MTPS, 0xC0), modify_op(2, MCU_PLA, RTL_REG_TCR0, 0, 0x0080), R8153_NIC_RESET, \
      write_op(4, MCU_PLA, RTL_REG_RXFIFO_CTRL0, 0x00080002), write_op(2, MCU_PLA, RTL_REG_RXFIFO_CTRL1, 0x00A0), \
      write_op(2, MCU_PLA, RTL_REG_RXFIFO_CTRL2, 0x0110), write_op(4, MCU_PLA, RTL_REG_TXFIFO_CTRL, 0x01000008)
#define R8153_WRITE_MAC \
  write_op(1, MCU_PLA, RTL_REG_CRWECR, 0xC0), {RtlKind::MAC_LOW, 4, MCU_PLA, RTL_REG_IDR, 0, 0}, \
      {RtlKind::MAC_HIGH, 2, MCU_PLA, RTL_REG_IDR + 4, 0, 0}, write_op(1, MCU_PLA, RTL_REG_CRWECR, 0x00)
#define R8153_READ_MAC \
  {RtlKind::MAC_READ_LOW, 4, MCU_PLA, RTL_REG_BACKUP, 0, 0}, \
      {RtlKind::MAC_READ_HIGH, 4, MCU_PLA, RTL_REG_BACKUP + 4, 0, 0}
#define R8153_TAIL_RX_AGG \
  modify_op(2, MCU_USB, RTL_REG_USB_CTRL, RX_AGG_OFF_MASK, 0), modify_op(2, MCU_PLA, RTL_REG_RSTTALLY, 0, 0x0001)

static constexpr RtlOp INIT_8153_OPS[] = {
    R8153_READ_MAC,
    R8153_TOLERANCE(0),
    R8153_BOOT_AND_PHY,
    only_op(2, V03 | V04 | V05),
    ocp_base_op(OCP_PAGE_ADC),
    write_op(2, MCU_PLA, OCP_ADC_CFG, ADC_CFG_DEFAULT),
    R8153_PHY_ON,
    only_op(2, V04),
    modify_op(2, MCU_USB, USB_REG_SSPHYLINK2, 0x3FFE, 96 << 1),
    modify_op(1, MCU_USB, USB_REG_USB2PHY, 0, 0x03),
    only_op(1, V05),
    modify_op(1, MCU_PLA, RTL_REG_DMY_REG0, 0x02, 0),
    only_op(3, V05 | V06),
    modify_op(1, MCU_USB, USB_REG_CSR_DUMMY1, 0x01, 0),
    skip_zero_op(2, MCU_USB, USB_REG_BURST_SIZE, 0xFFFF, 1),
    modify_op(1, MCU_USB, USB_REG_CSR_DUMMY1, 0, 0x01),
    only_op(6, V06),
    R8153_QUEUE_WAKE_OFF,
    R8153_EXTRA_STATUS,
    modify_op(1, MCU_USB, USB_REG_CSR_DUMMY2, 0, 0x01),
    modify_op(2, MCU_USB, USB_REG_WDT11_CTRL, 0x0001, 0),
    modify_op(2, MCU_PLA, RTL_REG_LED_FEATURE, 0x0700, 0),
    only_op(1, V04),
    write_op(1, MCU_USB, USB_REG_LPM_CTRL, 0x30 | 0x02 | 0x04),
    only_op(1, V03 | V05 | V06),
    write_op(1, MCU_USB, USB_REG_LPM_CTRL, 0x30 | 0x02 | 0x0C),
    modify_op(2, MCU_USB, USB_REG_AFE_CTRL2, 0xF800, 0xA000 | 0x0100),
    write_op(2, MCU_USB, USB_REG_CONNECT_TIMER, 0x0001),
    modify_op(2, MCU_USB, USB_REG_POWER_CUT, PWR_EN | PHASE2_EN, 0),
    modify_op(2, MCU_USB, USB_REG_MISC_0, PCUT_STATUS, 0),
    modify_op(2, MCU_PLA, RTL_REG_MAC_PWR_CTRL2, MAC_CLK_SPDWN_EN, 0),
    R8153_TOLERANCE(0xFFFFFFFF),
    modify_op(1, MCU_PLA, RTL_REG_CONFIG6, 0, 0x01),
    modify_op(1, MCU_PLA, RTL_REG_LWAKE_CTRL, 0x80, 0),
    R8153_TAIL_RX_AGG,

    R8153_ALDPS_OFF,
    R8153_EEE_OFF,
    only_op(1, V03),
    modify_op(2, MCU_PLA, OCP_EEE_CFG, CTAP_SHORT_EN, 0),
    modify_op(2, MCU_PLA, OCP_POWER_CFG, 0, EEE_CLKDIV_EN),
    modify_op(2, MCU_PLA, OCP_DOWN_SPEED, 0, EN_10M_BGOFF),
    modify_op(2, MCU_PLA, OCP_POWER_CFG, 0, EN_10M_PLLOFF),
    sram_addr_op(SRAM_IMPEDANCE),
    sram_data_op(0x0B13),
    modify_op(2, MCU_PLA, RTL_REG_PHY_PWR, 0, PFM_PWM_SWITCH),
    sram_addr_op(SRAM_LPF_CFG),
    sram_data_op(0xF70F),
    sram_addr_op(SRAM_10M_AMP1),
    sram_data_op(0x00AF),
    sram_addr_op(SRAM_10M_AMP2),
    sram_data_op(0x0208),
    R8153_EEE_ON,
    R8153_ALDPS_ON,
    R8153_FLOW_CONTROL,
    only_op(1, V05 | V06),
    modify_op(2, MCU_USB, USB_REG_U2P3_CTRL, 0, U2P3_ENABLE),

    R8153_TOLERANCE(0),
    modify_op(2, MCU_USB, USB_REG_U2P3_CTRL, U2P3_ENABLE, 0),
    R8153_ALDPS_OFF,
    modify_op(2, MCU_PLA, RTL_REG_TEREDO_CFG, TEREDO_OFF_MASK, 0),
    R8153_FIRST_INIT,
    modify_op(1, MCU_PLA, RTL_REG_CONFIG6, 0, 0x01),
    modify_op(1, MCU_PLA, RTL_REG_LWAKE_CTRL, 0x80, 0),
    modify_op(2, MCU_USB, USB_REG_SSPHYLINK1, 0x0002, 0),
    R8153_ALDPS_ON,
    only_op(1, V05 | V06),
    modify_op(2, MCU_USB, USB_REG_U2P3_CTRL, 0, U2P3_ENABLE),
    R8153_TOLERANCE(0xFFFFFFFF),
    R8153_WRITE_MAC,
};

static constexpr RtlOp INIT_8153B_OPS[] = {
    R8153_READ_MAC,
    modify_op(2, MCU_USB, USB_REG_LPM_CONFIG, LPM_U1U2_EN, 0),
    R8153_BOOT_AND_PHY,
    R8153_PHY_ON,
    write_op(2, MCU_USB, USB_REG_MSC_TIMER, 0x0FFF),
    modify_op(2, MCU_USB, USB_REG_POWER_CUT, PWR_EN, 0),
    modify_op(2, MCU_USB, USB_REG_MISC_0, PCUT_STATUS, 0),
    modify_op(1, MCU_USB, USB_REG_POWER_CUT, UPS_EN_PREWAKE, 0),
    modify_op(1, MCU_USB, USB_REG_MISC_2, UPS_FORCE_PWR_DOWN, 0),
    R8153_QUEUE_WAKE_OFF,
    R8153_EXTRA_STATUS,
    modify_op(2, MCU_PLA, RTL_REG_MAC_PWR_CTRL2, 0, MAC_CLK_SPDWN_EN),
    modify_op(2, MCU_PLA, RTL_REG_MAC_PWR_CTRL3, PLA_MCU_SPDWN_EN, 0),
    only_op(2, V09),
    skip_zero_op(1, MCU_PLA, RTL_REG_TEST_IO, 0x20, 1),
    modify_op(2, MCU_PLA, RTL_REG_PHY_PWR, 0, TEST_IO_OFF),
    R8153_TAIL_RX_AGG,

    R8153_ALDPS_OFF,
    R8153_EEE_OFF,
    write_op(2, MCU_USB, USB_REG_U1U2_TIMER, 500),
    ocp_base_op(OCP_PAGE_MII),
    modify_op(2, MCU_PLA, OCP_MII_BMCR, PHY_POWER_DOWN, 0),
    sram_addr_op(0x8045),
    sram_data_op(0x0000),
    sram_addr_op(0x804D),
    sram_data_op(0x1222),
    sram_addr_op(0x805D),
    sram_data_op(0x0022),
    sram_addr_op(SRAM_GREEN_CFG),
    modify_op(2, MCU_PLA, OCP_SRAM_DATA, 0, GREEN_ETH_EN),
    sram_addr_op(SRAM_GREEN_CFG),
    modify_op(2, MCU_PLA, OCP_SRAM_DATA, 0, R_TUNE_EN),
    modify_op(2, MCU_PLA, OCP_NCTL_CFG, 0, PGA_RETURN_EN),
    write_op(2, MCU_PLA, RTL_REG_EFUSE_CMD, EFUSE_READ_ADC),
    {RtlKind::EFUSE_HIGH, 2, MCU_PLA, RTL_REG_EFUSE_CMD, 0, 0},
    {RtlKind::EFUSE_LOW, 2, MCU_PLA, RTL_REG_EFUSE_DATA, 2, 0},
    ocp_base_op(OCP_PAGE_ADC),
    {RtlKind::WRITE_VALUE, 2, MCU_PLA, OCP_ADC_IOFFSET, 0, 0},
    modify_op(2, MCU_PLA, RTL_REG_PHY_PWR, 0, PFM_PWM_SWITCH),
    R8153_EEE_ON,
    R8153_ALDPS_ON,
    R8153_FLOW_CONTROL,

    modify_op(2, MCU_USB, USB_REG_LPM_CONFIG, LPM_U1U2_EN, 0),
    modify_op(2, MCU_USB, USB_REG_U2P3_CTRL, U2P3_ENABLE, 0),
    R8153_ALDPS_OFF,
    write_op(1, MCU_PLA, RTL_REG_TEREDO_CFG, 0xFF),
    R8153_FIRST_INIT,
    write_op(4, MCU_USB, RTL_REG_RX_BUF_TH, 0x00010001),
    modify_op(2, MCU_PLA, RTL_REG_MAC_PWR_CTRL3, PLA_MCU_SPDWN_EN, 0),
    R8153_ALDPS_ON,
    R8153_WRITE_MAC,
};

static constexpr RtlOp SPEED_8153_OPS[] = {
    ocp_base_op(OCP_PAGE_MII),
    modify_op(2, MCU_PLA, OCP_MII_ADVERTISE, ADVERTISE_ALL, ADVERTISE_ALL),
    modify_op(2, MCU_PLA, OCP_MII_CTRL1000, ADVERTISE_1000_MASK, ADVERTISE_1000FULL),
    write_op(2, MCU_PLA, OCP_MII_BMCR, PHY_AUTONEG_RESTART),
    delay_op(20),
    {RtlKind::WAIT_CLEAR, 2, MCU_PLA, OCP_MII_BMCR, 20, 0x8000},
};

#define R8153_SET_IFG \
  modify_op(2, MCU_PLA, RTL_REG_TCR1, IFG_MASK, IFG_96NS), modify_op(2, MCU_PLA, RTL_REG_MAC_PWR_CTRL4, 0, TX10MIDLE_EN), \
      {RtlKind::SKIP_NOT_HALF, 2, MCU_PLA, RTL_REG_PHYSTATUS, 2, 0}, \
      modify_op(2, MCU_PLA, RTL_REG_TCR1, IFG_MASK, IFG_144NS), \
      modify_op(2, MCU_PLA, RTL_REG_MAC_PWR_CTRL4, TX10MIDLE_EN, 0)
#define R8153_RX_MODE \
  modify_op(2, MCU_PLA, RTL_REG_MISC_1, RXDY_GATED, 0), write_op(4, MCU_PLA, RTL_REG_MAR, 0xFFFFFFFF), \
      write_op(4, MCU_PLA, RTL_REG_MAR + 4, 0xFFFFFFFF), \
      modify_op(4, MCU_PLA, RTL_REG_RCR, RCR_ACCEPT_ALL, RCR_ACCEPT_NORMAL)

static constexpr RtlOp ENABLE_8153_OPS[] = {
    {RtlKind::STATUS, 2, MCU_PLA, RTL_REG_PHYSTATUS, 0, 0},
    {RtlKind::EEE, 2, MCU_PLA, RTL_REG_EEEP_CR, EEE_PLUS_TX, 0},
    {RtlKind::WRITE_SPEED, 2, MCU_USB, USB_REG_RX_EARLY_TIMEOUT, 65535, 31250},
    write_op(2, MCU_USB, USB_REG_RX_EARLY_SIZE, 3707),
    R8153_SET_IFG,
    modify_op(2, MCU_PLA, RTL_REG_FMC, FMC_MCU_ENABLE, 0),
    modify_op(2, MCU_PLA, RTL_REG_FMC, 0, FMC_MCU_ENABLE),
    modify_op(1, MCU_PLA, RTL_REG_CR, 0, CR_RX_TX),
    R8153_RX_MODE,
};

static constexpr RtlOp ENABLE_8153B_OPS[] = {
    {RtlKind::STATUS, 2, MCU_PLA, RTL_REG_PHYSTATUS, 0, 0},
    {RtlKind::EEE, 2, MCU_PLA, RTL_REG_EEEP_CR, EEE_PLUS_TX, 0},
    write_op(2, MCU_USB, USB_REG_RX_EARLY_TIMEOUT, 16),
    write_op(2, MCU_USB, USB_REG_RX_EXTRA_AGGR, 1875),
    write_op(2, MCU_USB, USB_REG_RX_EARLY_SIZE, 3901),
    R8153_SET_IFG,
    only_op(3, V09 | version_bit(14)),
    modify_op(2, MCU_USB, USB_REG_FW_TASK, FC_PATCH_TASK, 0),
    delay_op(2),
    modify_op(2, MCU_USB, USB_REG_FW_TASK, 0, FC_PATCH_TASK),
    modify_op(2, MCU_PLA, RTL_REG_FMC, FMC_MCU_ENABLE, 0),
    modify_op(2, MCU_PLA, RTL_REG_FMC, 0, FMC_MCU_ENABLE),
    modify_op(1, MCU_PLA, RTL_REG_CR, 0, CR_RX_TX),
    write_op(1, MCU_USB, USB_REG_UPT_RXDMA_OWN, OWN_UPDATE_CLEAR),
    R8153_RX_MODE,
};

static constexpr uint32_t CHIP_VERSION_10 = 0x7020;
static constexpr uint32_t CHIP_VERSION_11 = 0x7030;
static constexpr uint32_t CHIP_VERSION_12 = 0x7400;
static constexpr uint32_t CHIP_VERSION_13 = 0x7410;
static constexpr uint32_t CHIP_VERSION_14 = 0x6400;
static constexpr uint32_t CHIP_VERSION_15 = 0x7420;

static constexpr uint32_t V10 = version_bit(10);
static constexpr uint32_t V11 = version_bit(11);
static constexpr uint32_t V12 = version_bit(12);
static constexpr uint32_t V13 = version_bit(13);
static constexpr uint32_t V15 = version_bit(15);
static constexpr uint32_t V_8156A = V10 | V11;
static constexpr uint32_t V_8156B = V12 | V13 | V15;

static constexpr uint16_t RTL_REG_CONFIG34 = 0xE820;
static constexpr uint16_t RTL_REG_CONFIG5 = 0xE822;
static constexpr uint16_t RTL_REG_RCR1 = 0xC012;
static constexpr uint16_t RTL_REG_RXFIFO_FULL = 0xC0A2;
static constexpr uint16_t RTL_REG_RX_FIFO_FULL = 0xC0A6;
static constexpr uint16_t RTL_REG_RX_FIFO_EMPTY = 0xC0AA;
static constexpr uint16_t RTL_REG_TXFIFO_FULL = 0xE61A;
static constexpr uint16_t RTL_REG_EEE_TXTWSYS = 0xE04C;
static constexpr uint16_t RTL_REG_EEE_TXTWSYS_2P5G = 0xE058;
static constexpr uint16_t RTL_REG_USB_CFG = 0xE952;
static constexpr uint16_t RTL_REG_GPHY_CTRL = 0xD3AE;
static constexpr uint16_t RTL_REG_POL_GPIO_CTRL = 0xDC6A;
static constexpr uint16_t USB_REG_ECM_OP = 0xD26B;
static constexpr uint16_t USB_REG_SPEED_OPTION = 0xD32A;
static constexpr uint16_t USB_REG_ECM_OPTION = 0xCFEE;
static constexpr uint16_t USB_REG_FC_TIMER = 0xD340;
static constexpr uint16_t USB_REG_FW_CTRL = 0xD334;
static constexpr uint16_t USB_REG_BMU_CONFIG = 0xD4B4;
static constexpr uint16_t USB_REG_L1_CTRL = 0xB45E;
static constexpr uint16_t USB_REG_RX_AGGR_NUM = 0xD4EE;
static constexpr uint16_t USB_REG_GPHY_CTRL = 0xD284;
static constexpr uint32_t CRWECR_CONFIG = 0xC0;
static constexpr uint32_t CDC_ECM_EN = 0x0008;
static constexpr uint32_t BMU_RESET_EP_IN = 0x01;
static constexpr uint32_t CR_TE = 0x04;
static constexpr uint32_t CR_RE = 0x08;
static constexpr uint32_t IDLE_SPDWN_EN = 0x0040;
static constexpr uint32_t EEE_SPDWN_EN = 0x0001;
static constexpr uint16_t OCP_ADDR_PHY_PATCH_STAT = 0xB800;
static constexpr uint16_t OCP_ADDR_PHY_PATCH_CMD = 0xB820;
static constexpr uint16_t OCP_ADDR_EEE_ADV2 = 0xA6D4;
static constexpr uint16_t OCP_ADDR_DOWN_SPEED = 0xA442;
static constexpr uint16_t OCP_ADDR_POWER_CFG = 0xA430;
static constexpr uint16_t OCP_ADDR_SYSCLK_CFG = 0xC416;
static constexpr uint16_t OCP_ADDR_INTR_EN = 0xA424;
static constexpr uint16_t OCP_ADDR_10GBT_CTRL = 0xA5D4;
static constexpr uint32_t PATCH_REQUEST = 0x0010;
static constexpr uint32_t PATCH_READY = 0x0040;
static constexpr uint32_t GPHY_FLASH = 0x0002;
static constexpr uint32_t BYPASS_FLASH = 0x0020;
static constexpr uint32_t GPHY_PATCH_DONE = 0x0004;
static constexpr uint32_t DACK_DET_EN = 0x8000;
static constexpr uint32_t FLOW_BOTH = 0x0040 | 0x0020;

static constexpr RtlOp ocp_w(uint16_t address, uint16_t value) {
  return {RtlKind::OCP_WRITE, 2, MCU_PLA, address, 0, value};
}
static constexpr RtlOp ocp_m(uint16_t address, uint16_t clear, uint16_t set) {
  return {RtlKind::OCP_MODIFY, 2, MCU_PLA, address, clear, set};
}
static constexpr RtlOp chain_op(uint32_t stage) { return {RtlKind::CHAIN, 0, 0, 0, 0, stage}; }
static constexpr RtlOp skip_nonzero_op(uint8_t width, uint16_t mcu, uint16_t reg, uint32_t mask, uint8_t count) {
  return {RtlKind::SKIP_NONZERO, width, mcu, reg, count, mask};
}

#define R8156_SRAM(address, value) ocp_w(0xA436, address), ocp_w(0xA438, value)
#define R8156_SRAM_W01(address, clear, set) ocp_w(0xA436, address), ocp_m(0xA438, clear, set)
#define R8156_SRAM2(address, value) ocp_w(0xB87C, address), ocp_w(0xB87E, value)
#define R8156_SRAM2_W01(address, clear, set) ocp_w(0xB87C, address), ocp_m(0xB87E, clear, set)
#define R8156_PATCH_REQUEST(skip) \
  ocp_m(OCP_ADDR_PHY_PATCH_CMD, 0, PATCH_REQUEST), ocp_base_op(OCP_PAGE_ADC), \
      {RtlKind::WAIT_SET, 2, MCU_PLA, OCP_ADDR_PHY_PATCH_STAT, 2, PATCH_READY}, \
      skip_zero_op(2, MCU_PLA, OCP_ADDR_PHY_PATCH_STAT, PATCH_READY, skip)
#define R8156_PATCH_RELEASE \
  ocp_m(OCP_ADDR_PHY_PATCH_CMD, PATCH_REQUEST, 0), ocp_base_op(OCP_PAGE_ADC), \
      {RtlKind::WAIT_CLEAR, 2, MCU_PLA, OCP_ADDR_PHY_PATCH_STAT, 2, PATCH_READY}
#define R8156_WAIT_FLASH \
  skip_zero_op(2, MCU_PLA, RTL_REG_GPHY_CTRL, GPHY_FLASH, 2), \
      skip_nonzero_op(2, MCU_USB, USB_REG_GPHY_CTRL, BYPASS_FLASH, 1), \
      {RtlKind::WAIT_SET, 2, MCU_USB, USB_REG_GPHY_CTRL, 2, GPHY_PATCH_DONE}
#define R8156_PHY_HEAD \
  modify_op(2, MCU_USB, USB_REG_MISC_0, PCUT_STATUS, 0), R8153_ALDPS_OFF, R8153_EEE_OFF, ocp_m(OCP_ADDR_EEE_ADV2, 0x0003, 0), \
      ocp_base_op(OCP_PAGE_MII), wait_phy_op(PHY_STAT_LAN_ON), modify_op(2, MCU_PLA, RTL_REG_PHY_PWR, 0, PFM_PWM_SWITCH)
#define R8156_PHY_TAIL \
  R8156_SRAM_W01(SRAM_GREEN_CFG, 0, GREEN_ETH_EN), ocp_m(0xA428, 0x0200, 0), ocp_m(0xA5EA, 0x0001, 0), \
      only_op(4, V11 | V_8156B), R8153_EEE_ON, R8153_ALDPS_ON, R8153_FLOW_CONTROL, \
      modify_op(2, MCU_USB, USB_REG_U2P3_CTRL, 0, U2P3_ENABLE), chain_op(2)
#define R8156B_ADVANCE_EEE \
  ocp_m(OCP_ADDR_INTR_EN, 0, 0x0008), R8156_PATCH_REQUEST(6), \
      modify_op(2, MCU_PLA, RTL_REG_MAC_PWR_CTRL4, 0, EEE_SPDWN_EN), ocp_m(OCP_ADDR_DOWN_SPEED, 0x3000, 0x0800), \
      ocp_m(OCP_ADDR_POWER_CFG, 0x8000, 0), R8156_PATCH_RELEASE

static constexpr RtlOp INIT_8156_OPS[] = {
    R8153_READ_MAC,
    modify_op(1, MCU_USB, USB_REG_ECM_OP, 0x01, 0),
    write_op(2, MCU_USB, USB_REG_SPEED_OPTION, 0),
    modify_op(2, MCU_USB, USB_REG_ECM_OPTION, 0, 0x0020),
    only_op(1, V_8156B),
    modify_op(2, MCU_USB, USB_REG_U2P3_CTRL, 0, 0x0008),
    modify_op(2, MCU_USB, USB_REG_LPM_CONFIG, LPM_U1U2_EN, 0),
    only_op(3, V13 | V15),
    R8156_WAIT_FLASH,
    R8153_BOOT_AND_PHY,
    {RtlKind::SKIP_NOT_EXT, 2, MCU_PLA, OCP_PHY_STATUS, 3, 0},
    ocp_m(0xA468, 0x000A, 0),
    only_op(1, V_8156B),
    ocp_m(0xA466, 0x0001, 0),
    R8153_PHY_ON,
    write_op(2, MCU_USB, USB_REG_MSC_TIMER, 0x0FFF),
    write_op(2, MCU_USB, USB_REG_U1U2_TIMER, 500),
    modify_op(2, MCU_USB, USB_REG_POWER_CUT, PWR_EN, 0),
    modify_op(2, MCU_USB, USB_REG_MISC_0, PCUT_STATUS, 0),
    modify_op(1, MCU_USB, USB_REG_POWER_CUT, UPS_EN_PREWAKE, 0),
    modify_op(1, MCU_USB, USB_REG_MISC_2, UPS_FORCE_PWR_DOWN, 0),
    R8153_QUEUE_WAKE_OFF,
    only_op(7, V_8156B),
    modify_op(2, MCU_PLA, RTL_REG_RCR, 0x0800, 0),
    modify_op(2, MCU_PLA, RTL_REG_CPCR, 0, 0x0001),
    write_op(2, MCU_USB, USB_REG_FC_TIMER, 0x8000 | (600 / 8)),
    modify_op(2, MCU_USB, USB_REG_FW_CTRL, 0x0008, 0),
    skip_nonzero_op(2, MCU_PLA, RTL_REG_POL_GPIO_CTRL, DACK_DET_EN, 1),
    modify_op(2, MCU_USB, USB_REG_FW_CTRL, 0, 0x0100),
    modify_op(2, MCU_USB, USB_REG_FW_TASK, 0, FC_PATCH_TASK),
    write_op(2, MCU_PLA, RTL_REG_MAC_PWR_CTRL, 0x0403),
    modify_op(2, MCU_PLA, RTL_REG_MAC_PWR_CTRL2, 0x00FF, MAC_CLK_SPDWN_EN | 0x03),
    modify_op(2, MCU_PLA, RTL_REG_MAC_PWR_CTRL3, PLA_MCU_SPDWN_EN, 0),
    R8153_EXTRA_STATUS,
    modify_op(2, MCU_USB, RTL_REG_USB_CTRL, RX_AGG_OFF_MASK, 0),
    only_op(1, V_8156A),
    modify_op(1, MCU_USB, USB_REG_BMU_CONFIG, 0, 0x02),
    ocp_m(0xA5B4, 0x8000, 0),
    modify_op(2, MCU_PLA, RTL_REG_RSTTALLY, 0, 0x0001),
    chain_op(1),
};

static constexpr RtlOp TUNE_8156_V10_OPS[] = {
    R8156_PHY_HEAD,
    ocp_m(0xAD40, 0x03FF, 0x0084),
    ocp_m(0xAD4E, 0, 0x0010),
    ocp_m(0xAD16, 0x03FF, 0x0006),
    ocp_m(0xAD32, 0x003F, 0x0006),
    ocp_m(0xAC08, 0x1100, 0),
    ocp_m(0xAC8A, 0x8000, 0x7000),
    ocp_m(0xAD18, 0, 0x0400),
    ocp_m(0xAD1A, 0, 0x03FF),
    ocp_m(0xAD1C, 0, 0x03FF),
    R8156_SRAM_W01(0x80EA, 0xFF00, 0xC400),
    R8156_SRAM_W01(0x80EB, 0x0700, 0x0300),
    R8156_SRAM_W01(0x80F8, 0xFF00, 0x1C00),
    R8156_SRAM_W01(0x80F1, 0xFF00, 0x3000),
    R8156_SRAM_W01(0x80FE, 0xFF00, 0xA500),
    R8156_SRAM_W01(0x8102, 0xFF00, 0x5000),
    R8156_SRAM_W01(0x8015, 0xFF00, 0x3300),
    R8156_SRAM_W01(0x8100, 0xFF00, 0x7000),
    R8156_SRAM_W01(0x8014, 0xFF00, 0xF000),
    R8156_SRAM_W01(0x8016, 0xFF00, 0x6500),
    R8156_SRAM_W01(0x80DC, 0xFF00, 0xED00),
    R8156_SRAM_W01(0x80DF, 0, 0x0100),
    R8156_SRAM_W01(0x80E1, 0x0100, 0),
    ocp_m(0xBF06, 0x003F, 0x0038),
    R8156_SRAM(0x819F, 0xDDB6),
    ocp_w(0xBC34, 0x5555),
    ocp_m(0xBF0A, 0x0E00, 0x0A00),
    ocp_m(0xBD2C, 0x2000, 0),
    R8156_PHY_TAIL,
};

static constexpr RtlOp TUNE_8156_V11_OPS[] = {
    R8156_PHY_HEAD,
    ocp_m(0xAD16, 0, 0x03FF),
    ocp_m(0xAD32, 0x003F, 0x0006),
    ocp_m(0xAC08, 0x1100, 0),
    ocp_m(0xACC0, 0x0003, 0x0002),
    ocp_m(0xAD40, 0x00E7, 0x0044),
    ocp_m(0xAC14, 0x0080, 0),
    ocp_m(0xAC80, 0x0300, 0),
    ocp_m(0xAC5E, 0x0007, 0x0002),
    ocp_w(0xAD4C, 0x00A8),
    ocp_w(0xAC5C, 0x01FF),
    ocp_m(0xAC8A, 0x00F0, 0x0030),
    R8156_SRAM2_W01(0x8157, 0xFF00, 0x0500),
    R8156_SRAM2_W01(0x8159, 0xFF00, 0x0700),
    R8156_SRAM2(0x80A2, 0x0153),
    R8156_SRAM2(0x809C, 0x0153),
    write_op(2, MCU_PLA, RTL_REG_EEE_TXTWSYS_2P5G, 0x0056),
    modify_op(2, MCU_PLA, RTL_REG_USB_CFG, 0, 0x0006),
    R8156_SRAM(0x8257, 0x020F),
    R8156_SRAM(0x80EA, 0x7843),
    R8156_PATCH_REQUEST(8),
    modify_op(2, MCU_PLA, RTL_REG_MAC_PWR_CTRL4, 0, EEE_SPDWN_EN),
    ocp_m(OCP_ADDR_DOWN_SPEED, 0x3000, 0x0800),
    ocp_m(OCP_ADDR_POWER_CFG, 0x8000, 0),
    ocp_w(OCP_ADDR_SYSCLK_CFG, 0),
    ocp_w(OCP_ADDR_SYSCLK_CFG, 0x0500),
    R8156_PATCH_RELEASE,
    ocp_m(0xD068, 0, 0x2000),
    R8156_SRAM_W01(0x81A2, 0x0100, 0),
    ocp_m(0xB54C, 0xFF00, 0xDB00),
    ocp_m(0xA454, 0x0001, 0),
    ocp_m(OCP_ADDR_10GBT_CTRL, 0, 0x0020),
    ocp_m(0xAD4E, 0x0010, 0),
    ocp_m(0xA86A, 0x0001, 0),
    ocp_m(OCP_ADDR_INTR_EN, 0, 0x0008),
    R8156_PHY_TAIL,
};

static constexpr RtlOp TUNE_8156_V12_OPS[] = {
    ocp_w(0xBF86, 0x9000),
    ocp_m(0xC402, 0, 0x0400),
    ocp_m(0xC402, 0x0400, 0),
    ocp_w(0xBD86, 0x1010),
    ocp_w(0xBD88, 0x1010),
    ocp_m(0xBD4E, 0x0C00, 0x0800),
    ocp_m(0xBF46, 0x0F00, 0x0700),
    R8156_PHY_HEAD,
    ocp_m(0xBC08, 0, 0x000C),
    R8156_SRAM_W01(0x8FFF, 0xFF00, 0x0400),
    ocp_m(0xACDA, 0, 0xFF00),
    ocp_m(0xACDE, 0, 0xF000),
    ocp_w(0xAC8C, 0x0FFC),
    ocp_w(0xAC46, 0xB7B4),
    ocp_w(0xAC50, 0x0FBC),
    ocp_w(0xAC3C, 0x9240),
    ocp_w(0xAC4E, 0x0DB4),
    ocp_w(0xACC6, 0x0707),
    ocp_w(0xACC8, 0xA0D3),
    ocp_w(0xAD08, 0x0007),
    R8156_SRAM2(0x8560, 0x19CC),
    R8156_SRAM2(0x8562, 0x19CC),
    R8156_SRAM2(0x8564, 0x19CC),
    R8156_SRAM2(0x8566, 0x147D),
    R8156_SRAM2(0x8568, 0x147D),
    R8156_SRAM2(0x856A, 0x147D),
    R8156_SRAM2(0x8FFE, 0x0907),
    R8156_SRAM2(0x80D6, 0x2801),
    R8156_SRAM2(0x80F2, 0x2801),
    R8156_SRAM2(0x80F4, 0x6077),
    ocp_w(0xB506, 0x01E7),
    R8156_SRAM2(0x8013, 0x0700),
    R8156_SRAM2(0x8FB9, 0x2801),
    R8156_SRAM2(0x8FBA, 0x0100),
    R8156_SRAM2(0x8FBC, 0x1900),
    R8156_SRAM2(0x8FBE, 0xE100),
    R8156_SRAM2(0x8FC0, 0x0800),
    R8156_SRAM2(0x8FC2, 0xE500),
    R8156_SRAM2(0x8FC4, 0x0F00),
    R8156_SRAM2(0x8FC6, 0xF100),
    R8156_SRAM2(0x8FC8, 0x0400),
    R8156_SRAM2(0x8FCA, 0xF300),
    R8156_SRAM2(0x8FCC, 0xFD00),
    R8156_SRAM2(0x8FCE, 0xFF00),
    R8156_SRAM2(0x8FD0, 0xFB00),
    R8156_SRAM2(0x8FD2, 0x0100),
    R8156_SRAM2(0x8FD4, 0xF400),
    R8156_SRAM2(0x8FD6, 0xFF00),
    R8156_SRAM2(0x8FD8, 0xF600),
    modify_op(1, MCU_PLA, RTL_REG_USB_CFG, 0, 0x06),
    R8156_SRAM2(0x813D, 0x390E),
    R8156_SRAM2(0x814F, 0x790E),
    R8156_SRAM2(0x80B0, 0x0F31),
    ocp_m(0xBF4C, 0, 0x0002),
    ocp_m(0xBCCA, 0, 0x0300),
    R8156_SRAM2(0x8141, 0x320E),
    R8156_SRAM2(0x8153, 0x720E),
    R8156_SRAM2(0x8529, 0x050E),
    ocp_m(0xA432, 0x0040, 0),
    R8156_SRAM(0x816C, 0xC4A0),
    R8156_SRAM(0x8170, 0xC4A0),
    R8156_SRAM(0x8174, 0x04A0),
    R8156_SRAM(0x8178, 0x04A0),
    R8156_SRAM(0x817C, 0x0719),
    R8156_SRAM(0x8FF4, 0x0400),
    R8156_SRAM(0x8FF1, 0x0404),
    ocp_w(0xBF4A, 0x001B),
    R8156_SRAM2(0x8033, 0x7C13),
    R8156_SRAM2(0x8037, 0x7C13),
    R8156_SRAM2(0x803B, 0xFC32),
    R8156_SRAM2(0x803F, 0x7C13),
    R8156_SRAM2(0x8043, 0x7C13),
    R8156_SRAM2(0x8047, 0x7C13),
    R8156_SRAM2(0x8145, 0x370E),
    R8156_SRAM2(0x8157, 0x770E),
    R8156_SRAM2(0x8169, 0x0D0A),
    R8156_SRAM2(0x817B, 0x1D0A),
    R8156_SRAM_W01(0x8217, 0xFF00, 0x5000),
    R8156_SRAM_W01(0x821A, 0xFF00, 0x5000),
    R8156_SRAM(0x80DA, 0x0403),
    R8156_SRAM_W01(0x80DC, 0xFF00, 0x1000),
    R8156_SRAM(0x80B3, 0x0384),
    R8156_SRAM(0x80B7, 0x2007),
    R8156_SRAM_W01(0x80BA, 0xFF00, 0x6C00),
    R8156_SRAM(0x80B5, 0xF009),
    R8156_SRAM_W01(0x80BD, 0xFF00, 0x9F00),
    R8156_SRAM(0x80C7, 0xF083),
    R8156_SRAM(0x80DD, 0x03F0),
    R8156_SRAM_W01(0x80DF, 0xFF00, 0x1000),
    R8156_SRAM(0x80CB, 0x2007),
    R8156_SRAM_W01(0x80CE, 0xFF00, 0x6C00),
    R8156_SRAM(0x80C9, 0x8009),
    R8156_SRAM_W01(0x80D1, 0xFF00, 0x8000),
    R8156_SRAM(0x80A3, 0x200A),
    R8156_SRAM(0x80A5, 0xF0AD),
    R8156_SRAM(0x809F, 0x6073),
    R8156_SRAM(0x80A1, 0x000B),
    R8156_SRAM_W01(0x80A9, 0xFF00, 0xC000),
    R8156_PATCH_REQUEST(20),
    ocp_m(0xB896, 0x0001, 0),
    ocp_m(0xB892, 0xFF00, 0),
    ocp_w(0xB88E, 0xC23E),
    ocp_w(0xB890, 0x0000),
    ocp_w(0xB88E, 0xC240),
    ocp_w(0xB890, 0x0103),
    ocp_w(0xB88E, 0xC242),
    ocp_w(0xB890, 0x0507),
    ocp_w(0xB88E, 0xC244),
    ocp_w(0xB890, 0x090B),
    ocp_w(0xB88E, 0xC246),
    ocp_w(0xB890, 0x0C0E),
    ocp_w(0xB88E, 0xC248),
    ocp_w(0xB890, 0x1012),
    ocp_w(0xB88E, 0xC24A),
    ocp_w(0xB890, 0x1416),
    ocp_m(0xB896, 0, 0x0001),
    R8156_PATCH_RELEASE,
    ocp_m(0xA86A, 0, 0x0001),
    ocp_m(0xA6F0, 0, 0x0001),
    ocp_w(0xBFA0, 0xD70D),
    ocp_w(0xBFA2, 0x4100),
    ocp_w(0xBFA4, 0xE868),
    ocp_w(0xBFA6, 0xDC59),
    ocp_w(0xB54C, 0x3C18),
    ocp_m(0xBFA4, 0x0020, 0),
    R8156_SRAM_W01(0x817D, 0, 0x1000),
    R8156B_ADVANCE_EEE,
    R8156_PHY_TAIL,
};

static constexpr RtlOp TUNE_8156_V13_15_OPS[] = {
    R8156_WAIT_FLASH,
    R8156_PHY_HEAD,
    only_op(2, V13),
    ocp_m(0xAC46, 0x00F0, 0x0090),
    ocp_m(0xAD30, 0x0003, 0x0001),
    R8156_SRAM2(0x80F5, 0x760E),
    R8156_SRAM2(0x8107, 0x360E),
    R8156_SRAM2_W01(0x8551, 0xFF00, 0x0800),
    ocp_m(0xBF00, 0xE000, 0xA000),
    ocp_m(0xBF46, 0x0F00, 0x0300),
    R8156_SRAM(0x8044, 0x2417),
    R8156_SRAM(0x804A, 0x2417),
    R8156_SRAM(0x8050, 0x2417),
    R8156_SRAM(0x8056, 0x2417),
    R8156_SRAM(0x805C, 0x2417),
    R8156_SRAM(0x8062, 0x2417),
    R8156_SRAM(0x8068, 0x2417),
    R8156_SRAM(0x806E, 0x2417),
    R8156_SRAM(0x8074, 0x2417),
    R8156_SRAM(0x807A, 0x2417),
    ocp_m(0xBF84, 0xE000, 0xA000),
    R8156B_ADVANCE_EEE,
    R8156_PHY_TAIL,
};

static constexpr RtlOp UP_8156_OPS[] = {
    modify_op(2, MCU_USB, USB_REG_LPM_CONFIG, LPM_U1U2_EN, 0),
    modify_op(2, MCU_USB, USB_REG_U2P3_CTRL, U2P3_ENABLE, 0),
    R8153_ALDPS_OFF,
    modify_op(2, MCU_PLA, RTL_REG_MISC_1, 0, RXDY_GATED),
    write_op(1, MCU_PLA, RTL_REG_TEREDO_CFG, 0xFF),
    write_op(2, MCU_PLA, RTL_REG_WDT6_CTRL, 0x0010),
    write_op(2, MCU_PLA, RTL_REG_REALWOW_TIMER, 0),
    write_op(4, MCU_PLA, RTL_REG_TEREDO_TIMER, 0),
    modify_op(4, MCU_PLA, RTL_REG_RCR, RCR_ACCEPT_ALL, 0),
    only_op(6, V_8156A),
    modify_op(1, MCU_PLA, RTL_REG_CR, CR_TE, 0),
    modify_op(2, MCU_USB, USB_REG_BMU_RESET, BMU_RESET_EP_IN, 0),
    modify_op(2, MCU_USB, RTL_REG_USB_CTRL, 0, CDC_ECM_EN),
    modify_op(1, MCU_PLA, RTL_REG_CR, CR_RE, 0),
    modify_op(2, MCU_USB, USB_REG_BMU_RESET, 0, BMU_RESET_EP_IN),
    modify_op(2, MCU_USB, RTL_REG_USB_CTRL, CDC_ECM_EN, 0),
    only_op(2, V_8156B),
    R8153_NIC_RESET,
    modify_op(1, MCU_USB, USB_REG_BMU_RESET, BMU_RESET_BOTH, 0),
    modify_op(1, MCU_USB, USB_REG_BMU_RESET, 0, BMU_RESET_BOTH),
    modify_op(1, MCU_PLA, RTL_REG_OOB_CTRL, OOB_NOW, 0),
    modify_op(2, MCU_PLA, RTL_REG_SFF_STS_7, SFF_MCU_BORW, 0),
    modify_op(2, MCU_PLA, RTL_REG_RCR1, 0, 0x00C0),
    write_op(2, MCU_PLA, RTL_REG_RMS, 1522),
    write_op(1, MCU_PLA, RTL_REG_MTPS, 0xC0),
    write_op(2, MCU_PLA, RTL_REG_RX_FIFO_FULL, 512),
    write_op(2, MCU_PLA, RTL_REG_RX_FIFO_EMPTY, 1024),
    write_op(2, MCU_PLA, RTL_REG_TXFIFO_CTRL, 8),
    write_op(2, MCU_PLA, RTL_REG_TXFIFO_FULL, 128),
    only_op(1, V_8156A),
    modify_op(2, MCU_USB, USB_REG_BMU_CONFIG, 0, 0x0002),
    modify_op(2, MCU_PLA, RTL_REG_RXFIFO_FULL, 0x0FFF, 0x0008),
    modify_op(2, MCU_PLA, RTL_REG_MAC_PWR_CTRL3, PLA_MCU_SPDWN_EN, 0),
    modify_op(2, MCU_USB, USB_REG_SPEED_OPTION, 0x0300, 0),
    write_op(4, MCU_USB, RTL_REG_RX_BUF_TH, 0x00600400),
    R8153_ALDPS_ON,
    modify_op(2, MCU_USB, USB_REG_U2P3_CTRL, 0, U2P3_ENABLE),
    R8153_WRITE_MAC,
};

static constexpr RtlOp SPEED_8156_OPS[] = {
    ocp_base_op(OCP_PAGE_MII),
    modify_op(2, MCU_PLA, OCP_MII_ADVERTISE, ADVERTISE_ALL, ADVERTISE_ALL),
    modify_op(2, MCU_PLA, OCP_MII_CTRL1000, ADVERTISE_1000_MASK, ADVERTISE_1000FULL),
    only_op(1, V10 | V11 | V12 | V13),
    ocp_m(OCP_ADDR_10GBT_CTRL, 0x0180, 0),
    ocp_base_op(OCP_PAGE_MII),
    write_op(2, MCU_PLA, OCP_MII_BMCR, PHY_AUTONEG_RESTART),
    delay_op(20),
    {RtlKind::WAIT_CLEAR, 2, MCU_PLA, OCP_MII_BMCR, 20, 0x8000},
};

static constexpr RtlOp ENABLE_8156_OPS[] = {
    {RtlKind::STATUS, 2, MCU_PLA, RTL_REG_PHYSTATUS, 0, 0},
    only_op(2, V_8156A),
    write_op(2, MCU_PLA, RTL_REG_RX_FIFO_FULL, 512),
    write_op(2, MCU_PLA, RTL_REG_RX_FIFO_EMPTY, 1024),
    {RtlKind::EEE, 2, MCU_PLA, RTL_REG_EEEP_CR, EEE_PLUS_TX, 0},
    only_op(1, V_8156B),
    modify_op(2, MCU_USB, USB_REG_RX_AGGR_NUM, 0x01FF, 0),
    write_op(2, MCU_USB, USB_REG_RX_EARLY_TIMEOUT, 640 / 8),
    write_op(2, MCU_USB, USB_REG_RX_EXTRA_AGGR, 1875),
    write_op(2, MCU_USB, USB_REG_RX_EARLY_SIZE, 5949),
    R8153_SET_IFG,
    modify_op(2, MCU_PLA, RTL_REG_MAC_PWR_CTRL4, 0, IDLE_SPDWN_EN),
    only_op(2, V_8156A),
    skip_zero_op(2, MCU_PLA, RTL_REG_PHYSTATUS, STATUS_1000, 1),
    write_op(2, MCU_PLA, RTL_REG_EEE_TXTWSYS, 0x0011),
    modify_op(2, MCU_USB, USB_REG_L1_CTRL, 0x000F, 0x0001),
    {RtlKind::SKIP_UNLESS_ALL, 2, MCU_PLA, RTL_REG_PHYSTATUS, 1, FLOW_BOTH},
    modify_op(2, MCU_USB, USB_REG_L1_CTRL, 0x000F, 0x000F),
    modify_op(2, MCU_USB, USB_REG_FW_TASK, FC_PATCH_TASK, 0),
    delay_op(2),
    modify_op(2, MCU_USB, USB_REG_FW_TASK, 0, FC_PATCH_TASK),
    modify_op(2, MCU_PLA, RTL_REG_FMC, FMC_MCU_ENABLE, 0),
    modify_op(2, MCU_PLA, RTL_REG_FMC, 0, FMC_MCU_ENABLE),
    modify_op(1, MCU_PLA, RTL_REG_CR, 0, CR_RX_TX),
    write_op(1, MCU_USB, USB_REG_UPT_RXDMA_OWN, OWN_UPDATE_CLEAR),
    R8153_RX_MODE,
};

static constexpr RtlOp INIT_8153C_OPS[] = {
    R8153_READ_MAC,
    modify_op(2, MCU_USB, USB_REG_LPM_CONFIG, LPM_U1U2_EN, 0),
    write_op(1, MCU_PLA, RTL_REG_CRWECR, CRWECR_CONFIG),
    modify_op(2, MCU_PLA, RTL_REG_CONFIG5, 0x0008, 0),
    modify_op(2, MCU_USB, 0xCBF0, 0, 0x0002),
    R8153_BOOT_AND_PHY,
    R8153_PHY_ON,
    write_op(2, MCU_USB, USB_REG_MSC_TIMER, 0x0FFF),
    modify_op(2, MCU_USB, USB_REG_POWER_CUT, PWR_EN, 0),
    modify_op(2, MCU_USB, USB_REG_MISC_0, PCUT_STATUS, 0),
    modify_op(1, MCU_USB, USB_REG_POWER_CUT, UPS_EN_PREWAKE, 0),
    modify_op(1, MCU_USB, USB_REG_MISC_2, UPS_FORCE_PWR_DOWN, 0),
    write_op(1, MCU_PLA, RTL_REG_CRWECR, CRWECR_CONFIG),
    modify_op(2, MCU_PLA, RTL_REG_CONFIG34, 0, 0x0100),
    write_op(1, MCU_PLA, RTL_REG_CRWECR, 0x00),
    R8153_QUEUE_WAKE_OFF,
    R8153_EXTRA_STATUS,
    modify_op(2, MCU_USB, USB_REG_LPM_CONFIG, 0, LPM_U1U2_EN),
    modify_op(2, MCU_PLA, RTL_REG_MAC_PWR_CTRL2, 0, MAC_CLK_SPDWN_EN),
    modify_op(1, MCU_USB, USB_REG_MISC_2, 0x80, 0),
    R8153_TAIL_RX_AGG,

    R8153_ALDPS_OFF,
    R8153_EEE_OFF,
    write_op(2, MCU_USB, USB_REG_U1U2_TIMER, 500),
    ocp_base_op(OCP_PAGE_MII),
    modify_op(2, MCU_PLA, OCP_MII_BMCR, PHY_POWER_DOWN, 0),
    sram_addr_op(0x8045),
    sram_data_op(0x0000),
    sram_addr_op(0x804D),
    sram_data_op(0x1222),
    sram_addr_op(0x805D),
    sram_data_op(0x0022),
    sram_addr_op(SRAM_GREEN_CFG),
    modify_op(2, MCU_PLA, OCP_SRAM_DATA, 0, GREEN_ETH_EN),
    sram_addr_op(SRAM_GREEN_CFG),
    modify_op(2, MCU_PLA, OCP_SRAM_DATA, 0, R_TUNE_EN),
    modify_op(2, MCU_PLA, OCP_NCTL_CFG, 0, PGA_RETURN_EN),
    write_op(2, MCU_PLA, RTL_REG_EFUSE_CMD, EFUSE_READ_ADC),
    {RtlKind::EFUSE_HIGH, 2, MCU_PLA, RTL_REG_EFUSE_CMD, 0, 0},
    {RtlKind::EFUSE_LOW, 2, MCU_PLA, RTL_REG_EFUSE_DATA, 2, 0},
    ocp_base_op(OCP_PAGE_ADC),
    {RtlKind::WRITE_VALUE, 2, MCU_PLA, OCP_ADC_IOFFSET, 0, 0},
    modify_op(2, MCU_PLA, RTL_REG_PHY_PWR, 0, PFM_PWM_SWITCH),
    R8153_EEE_ON,
    R8153_ALDPS_ON,
    R8153_FLOW_CONTROL,

    modify_op(2, MCU_USB, USB_REG_LPM_CONFIG, LPM_U1U2_EN, 0),
    modify_op(2, MCU_USB, USB_REG_U2P3_CTRL, U2P3_ENABLE, 0),
    R8153_ALDPS_OFF,
    modify_op(2, MCU_PLA, RTL_REG_MISC_1, 0, RXDY_GATED),
    write_op(1, MCU_PLA, RTL_REG_TEREDO_CFG, 0xFF),
    write_op(2, MCU_PLA, RTL_REG_WDT6_CTRL, 0x0010),
    write_op(2, MCU_PLA, RTL_REG_REALWOW_TIMER, 0),
    write_op(4, MCU_PLA, RTL_REG_TEREDO_TIMER, 0),
    modify_op(4, MCU_PLA, RTL_REG_RCR, RCR_ACCEPT_ALL, 0),
    R8153_NIC_RESET,
    modify_op(1, MCU_USB, USB_REG_BMU_RESET, BMU_RESET_BOTH, 0),
    modify_op(1, MCU_USB, USB_REG_BMU_RESET, 0, BMU_RESET_BOTH),
    modify_op(1, MCU_PLA, RTL_REG_OOB_CTRL, OOB_NOW, 0),
    modify_op(2, MCU_PLA, RTL_REG_SFF_STS_7, SFF_MCU_BORW, 0),
    wait_ready_op(),
    modify_op(2, MCU_PLA, RTL_REG_SFF_STS_7, 0, SFF_RE_INIT_LL),
    wait_ready_op(),
    modify_op(2, MCU_PLA, RTL_REG_CPCR, 0, 0x0040),
    write_op(2, MCU_PLA, RTL_REG_RMS, 1522),
    write_op(1, MCU_PLA, RTL_REG_MTPS, 10 * 1024 / 64),
    write_op(2, MCU_PLA, RTL_REG_TXFIFO_CTRL, 8),
    write_op(2, MCU_PLA, RTL_REG_TXFIFO_FULL, 256),
    R8153_NIC_RESET,
    write_op(1, MCU_PLA, RTL_REG_RXFIFO_CTRL0, 0x02),
    write_op(1, MCU_PLA, RTL_REG_RXFIFO_FULL, 0x08),
    write_op(2, MCU_PLA, RTL_REG_RXFIFO_CTRL1, 0x00A0),
    write_op(2, MCU_PLA, RTL_REG_RXFIFO_CTRL2, 0x0110),
    write_op(4, MCU_USB, RTL_REG_RX_BUF_TH, 0x00010001),
    write_op(1, MCU_PLA, RTL_REG_CRWECR, CRWECR_CONFIG),
    modify_op(2, MCU_PLA, RTL_REG_CONFIG34, 0, 0x0100),
    write_op(1, MCU_PLA, RTL_REG_CRWECR, 0x00),
    modify_op(2, MCU_PLA, RTL_REG_MAC_PWR_CTRL3, PLA_MCU_SPDWN_EN, 0),
    R8153_ALDPS_ON,
    modify_op(2, MCU_USB, USB_REG_LPM_CONFIG, 0, LPM_U1U2_EN),
    R8153_WRITE_MAC,
};

#undef R8156_SRAM
#undef R8156_SRAM_W01
#undef R8156_SRAM2
#undef R8156_SRAM2_W01
#undef R8156_PATCH_REQUEST
#undef R8156_PATCH_RELEASE
#undef R8156_WAIT_FLASH
#undef R8156_PHY_HEAD
#undef R8156_PHY_TAIL
#undef R8156B_ADVANCE_EEE
#undef R8153_TOLERANCE
#undef R8153_BOOT_AND_PHY
#undef R8153_PHY_ON
#undef R8153_QUEUE_WAKE_OFF
#undef R8153_EXTRA_STATUS
#undef R8153_ALDPS_OFF
#undef R8153_ALDPS_ON
#undef R8153_EEE_OFF
#undef R8153_EEE_ON
#undef R8153_FLOW_CONTROL
#undef R8153_NIC_RESET
#undef R8153_FIRST_INIT
#undef R8153_WRITE_MAC
#undef R8153_READ_MAC
#undef R8153_TAIL_RX_AGG
#undef R8153_SET_IFG
#undef R8153_RX_MODE

template<size_t N> static constexpr size_t op_count(const RtlOp (&)[N]) { return N; }

static bool is_8153(uint8_t version) { return version >= 3 && version != 7; }
static bool is_8153b(uint8_t version) { return version == 8 || version == 9; }
static bool is_8156(uint8_t version) { return version >= 10 && version != 14; }

static uint32_t read32(const uint8_t *p) {
  return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
         (static_cast<uint32_t>(p[3]) << 24);
}
static void write32(uint8_t *p, uint32_t v) {
  p[0] = static_cast<uint8_t>(v);
  p[1] = static_cast<uint8_t>(v >> 8);
  p[2] = static_cast<uint8_t>(v >> 16);
  p[3] = static_cast<uint8_t>(v >> 24);
}

const USBEthernet::VidPid USBEthernet::RTL8152_IDS[] = {
    {0x0BDA, 0x8050}, {0x0BDA, 0x8053}, {0x0BDA, 0x8152}, {0x0BDA, 0x8153}, {0x0BDA, 0x8155}, {0x0BDA, 0x8156}, {0x045E, 0x07AB},
    {0x045E, 0x07C6}, {0x045E, 0x0927}, {0x045E, 0x0C5E}, {0x04E8, 0xA101}, {0x17EF, 0x304F}, {0x17EF, 0x3054},
    {0x17EF, 0x3062}, {0x17EF, 0x3069}, {0x17EF, 0x3082}, {0x17EF, 0x3098}, {0x17EF, 0x7205}, {0x17EF, 0x720C},
    {0x17EF, 0x7214}, {0x17EF, 0x721E}, {0x17EF, 0xA359}, {0x17EF, 0xA387}, {0x13B1, 0x0041}, {0x0955, 0x09FF},
    {0x2357, 0x0601}, {0x2357, 0x0602}, {0x2001, 0xB301}, {0x413C, 0xB097}, {0x0B05, 0x1976}, {0x20F4, 0xE02B},
    {0x20F4, 0xE02C},
};
const size_t USBEthernet::RTL8152_ID_COUNT = sizeof(USBEthernet::RTL8152_IDS) / sizeof(USBEthernet::RTL8152_IDS[0]);

bool USBEthernet::parse_rtl8152_() {
  const usb_device_desc_t *dev = this->get_device_desc_();
  const usb_config_desc_t *cfg = this->get_config_desc_();
  if (dev == nullptr || cfg == nullptr)
    return false;
  const bool known = std::any_of(RTL8152_IDS, RTL8152_IDS + RTL8152_ID_COUNT, [dev](const VidPid &id) {
    return id.vid == dev->idVendor && id.pid == dev->idProduct;
  });
  if (!known)
    return false;
  bool in_vendor = false;
  uint8_t intf_number = 0xFF;
  uint8_t in_ep = 0, out_ep = 0, notify_ep = 0;
  uint16_t in_mps = 0, out_mps = 0, notify_mps = 0;
  int offset = 0;
  const auto *desc = reinterpret_cast<const usb_standard_desc_t *>(cfg);
  while ((desc = usb_parse_next_descriptor(desc, cfg->wTotalLength, &offset)) != nullptr) {
    if (desc->bDescriptorType == USB_W_VALUE_DT_INTERFACE) {
      const auto *intf = reinterpret_cast<const usb_intf_desc_t *>(desc);
      in_vendor = intf_number == 0xFF && intf->bAlternateSetting == 0 && intf->bInterfaceClass == RTL8152_CLASS &&
                  this->interface_routed_(intf->bInterfaceNumber);
      if (in_vendor)
        intf_number = intf->bInterfaceNumber;
      continue;
    }
    if (!in_vendor || desc->bDescriptorType != USB_W_VALUE_DT_ENDPOINT)
      continue;
    const auto *ep = reinterpret_cast<const usb_ep_desc_t *>(desc);
    const bool in = USB_EP_DESC_GET_EP_DIR(ep);
    if (USB_EP_DESC_GET_XFERTYPE(ep) == USB_BM_ATTRIBUTES_XFER_INT && in) {
      notify_ep = ep->bEndpointAddress;
      notify_mps = USB_EP_DESC_GET_MPS(ep);
    } else if (USB_EP_DESC_GET_XFERTYPE(ep) == USB_BM_ATTRIBUTES_XFER_BULK && in) {
      in_ep = ep->bEndpointAddress;
      in_mps = USB_EP_DESC_GET_MPS(ep);
    } else if (USB_EP_DESC_GET_XFERTYPE(ep) == USB_BM_ATTRIBUTES_XFER_BULK) {
      out_ep = ep->bEndpointAddress;
      out_mps = USB_EP_DESC_GET_MPS(ep);
    }
  }
  if (in_ep == 0 || out_ep == 0 || notify_ep == 0)
    return false;
  this->proto_ = Proto::RTL8152;
  this->data_intf_ = intf_number;
  this->data_alt_ = 0;
  this->in_ep_ = in_ep;
  this->in_mps_ = in_mps;
  this->out_ep_ = out_ep;
  this->out_mps_ = out_mps;
  this->notify_ep_ = notify_ep;
  this->notify_mps_ = notify_mps;
  this->mac_index_ = dev->iSerialNumber;
  return true;
}

void USBEthernet::rtl_read_(uint32_t generation, uint16_t mcu, uint16_t reg) {
  const uint8_t ep0 = this->get_device_desc_() != nullptr ? this->get_device_desc_()->bMaxPacketSize0 : 64;
  this->control_(generation, RTL_REQT_READ, RTL_REQ_REGS, reg & ~3, mcu, std::vector<uint8_t>(ep0), 4);
}

void USBEthernet::rtl_write_(uint32_t generation, uint16_t mcu, uint16_t reg, uint8_t width, uint32_t value) {
  const uint8_t shift = width == 4 ? 0 : reg & (width == 2 ? 2 : 3);
  uint8_t enable = BYTE_ENABLE_DWORD;
  if (width != 4)
    enable = static_cast<uint8_t>((width == 2 ? BYTE_ENABLE_WORD : BYTE_ENABLE_BYTE) << shift);
  std::vector<uint8_t> data(4);
  write32(data.data(), value << (shift * 8));
  this->rtl_writing_ = true;
  this->control_(generation, RTL_REQT_WRITE, RTL_REQ_REGS, reg & ~3, mcu | enable, data, -1);
}

void USBEthernet::rtl_start_(uint32_t generation) {
  this->step_state_ = Step::RTL_VERSION;
  this->rtl_read_(generation, MCU_PLA, RTL_REG_TCR0);
}

void USBEthernet::rtl_begin_init_(uint32_t generation) {
  if (!this->alloc_rx_assembly_()) {
    this->rtl_fail_();
    return;
  }
  this->rtl_ocp_base_ = 0xFFFF;
  this->rtl_base_pending_ = false;
  if (is_8156(this->rtl_version_)) {
    this->rtl_script_(generation, Step::RTL_INIT, INIT_8156_OPS, op_count(INIT_8156_OPS));
  } else if (this->rtl_version_ == 14) {
    this->rtl_script_(generation, Step::RTL_INIT, INIT_8153C_OPS, op_count(INIT_8153C_OPS));
  } else if (is_8153b(this->rtl_version_)) {
    this->rtl_script_(generation, Step::RTL_INIT, INIT_8153B_OPS, op_count(INIT_8153B_OPS));
  } else if (is_8153(this->rtl_version_)) {
    this->rtl_script_(generation, Step::RTL_INIT, INIT_8153_OPS, op_count(INIT_8153_OPS));
  } else {
    this->rtl_script_(generation, Step::RTL_INIT, INIT_OPS, op_count(INIT_OPS));
  }
}

void USBEthernet::rtl_script_(uint32_t generation, Step step, const RtlOp *ops, size_t count) {
  this->step_state_ = step;
  this->rtl_op_ = ops;
  this->rtl_left_ = static_cast<uint16_t>(count);
  this->rtl_polls_ = 0;
  this->rtl_exec_(generation);
}

void USBEthernet::rtl_exec_(uint32_t generation) {
  while (this->rtl_left_ != 0) {
    const RtlOp &op = *this->rtl_op_;
    this->rtl_writing_ = false;
    switch (op.kind) {
      case RtlKind::MODIFY_V1:
        if (this->rtl_version_ != 1) {
          this->rtl_op_++;
          this->rtl_left_--;
          continue;
        }
        this->rtl_read_(generation, op.mcu, op.reg);
        return;
      case RtlKind::ONLY:
        this->rtl_op_++;
        this->rtl_left_--;
        if ((version_bit(this->rtl_version_) & op.set) == 0) {
          this->rtl_op_ += op.width;
          this->rtl_left_ -= op.width;
        }
        continue;
      case RtlKind::MODIFY:
      case RtlKind::WAIT_SET:
      case RtlKind::WAIT_CLEAR:
      case RtlKind::WAIT_PHY:
      case RtlKind::STATUS:
      case RtlKind::EEE:
      case RtlKind::SNAPSHOT:
      case RtlKind::MAC_READ_LOW:
      case RtlKind::MAC_READ_HIGH:
      case RtlKind::SKIP_ZERO:
      case RtlKind::SKIP_NONZERO:
      case RtlKind::SKIP_NOT_HALF:
      case RtlKind::SKIP_UNLESS_ALL:
      case RtlKind::SKIP_NOT_EXT:
      case RtlKind::EFUSE_HIGH:
      case RtlKind::EFUSE_LOW:
        this->rtl_read_(generation, op.mcu, op.reg);
        return;
      case RtlKind::WRITE:
        if (op.reg == RTL_REG_OCP_GPHY_BASE)
          this->rtl_ocp_base_ = static_cast<uint16_t>(op.set);
        this->rtl_write_(generation, op.mcu, op.reg, op.width, op.set);
        return;
      case RtlKind::OCP_WRITE:
      case RtlKind::OCP_MODIFY: {
        const uint16_t base = op.reg & 0xF000;
        if (base != this->rtl_ocp_base_) {
          this->rtl_ocp_base_ = base;
          this->rtl_base_pending_ = true;
          this->rtl_write_(generation, MCU_PLA, RTL_REG_OCP_GPHY_BASE, 2, base);
          return;
        }
        const uint16_t index = (op.reg & 0x0FFF) | 0xB000;
        if (op.kind == RtlKind::OCP_WRITE) {
          this->rtl_write_(generation, MCU_PLA, index, 2, op.set);
        } else {
          this->rtl_read_(generation, MCU_PLA, index);
        }
        return;
      }
      case RtlKind::CHAIN:
        this->rtl_chain_(generation, op.set);
        return;
      case RtlKind::WRITE_VALUE:
        this->rtl_write_(generation, op.mcu, op.reg, op.width, this->rtl_work_);
        return;
      case RtlKind::WRITE_SPEED:
        this->rtl_write_(generation, op.mcu, op.reg, op.width, this->in_mps_ < HIGH_SPEED_BULK ? op.clear : op.set);
        return;
      case RtlKind::MAC_LOW:
        this->rtl_write_(generation, op.mcu, op.reg, op.width, read32(this->mac_));
        return;
      case RtlKind::MAC_HIGH:
        this->rtl_write_(generation, op.mcu, op.reg, op.width,
                         static_cast<uint32_t>(this->mac_[4]) | (static_cast<uint32_t>(this->mac_[5]) << 8));
        return;
      case RtlKind::DELAY:
        this->set_timeout(op.set, [this, generation]() {
          if (generation == this->generation_.load(std::memory_order_acquire))
            this->rtl_advance_(generation);
        });
        return;
    }
  }
  this->rtl_done_(generation);
}

void USBEthernet::rtl_advance_(uint32_t generation) {
  this->rtl_polls_ = 0;
  this->rtl_op_++;
  this->rtl_left_--;
  this->rtl_exec_(generation);
}

void USBEthernet::rtl_fail_() {
  ESP_LOGE(TAG, "Realtek register access failed");
  this->rtl_left_ = 0;
  this->step_state_ = this->started_ ? Step::RUNNING : Step::IDLE;
  this->report_connected_();
}

void USBEthernet::rtl_step_(uint32_t generation, bool ok) {
  if (!ok) {
    this->rtl_fail_();
    return;
  }
  if (this->step_state_ == Step::RTL_VERSION) {
    const uint32_t version = (this->rtl_value_ >> 16) & CHIP_VERSION_MASK;
    this->rtl_version_ = 0;
    if (version == CHIP_VERSION_01) {
      this->rtl_version_ = 1;
    } else if (version == CHIP_VERSION_02) {
      this->rtl_version_ = 2;
    } else if (version == CHIP_VERSION_03) {
      this->rtl_version_ = 3;
    } else if (version == CHIP_VERSION_04) {
      this->rtl_version_ = 4;
    } else if (version == CHIP_VERSION_05) {
      this->rtl_version_ = 5;
    } else if (version == CHIP_VERSION_06) {
      this->rtl_version_ = 6;
    } else if (version == CHIP_VERSION_07) {
      this->rtl_version_ = 7;
    } else if (version == CHIP_VERSION_08) {
      this->rtl_version_ = 8;
    } else if (version == CHIP_VERSION_09) {
      this->rtl_version_ = 9;
    } else if (version == CHIP_VERSION_10) {
      this->rtl_version_ = 10;
    } else if (version == CHIP_VERSION_11) {
      this->rtl_version_ = 11;
    } else if (version == CHIP_VERSION_12) {
      this->rtl_version_ = 12;
    } else if (version == CHIP_VERSION_13) {
      this->rtl_version_ = 13;
    } else if (version == CHIP_VERSION_14) {
      this->rtl_version_ = 14;
    } else if (version == CHIP_VERSION_15) {
      this->rtl_version_ = 15;
    }
    if (this->rtl_version_ == 0) {
      ESP_LOGE(TAG, "Unknown Realtek chip version 0x%04" PRIX32, version);
      this->step_state_ = Step::IDLE;
      this->report_connected_();
      return;
    }
    ESP_LOGI(TAG, "Realtek chip version %u", this->rtl_version_);
    if (is_8153(this->rtl_version_)) {
      this->mac_valid_ = false;
      this->rtl_begin_init_(generation);
      return;
    }
    this->next_(generation, Step::MAC);
    return;
  }
  if (this->rtl_base_pending_) {
    this->rtl_base_pending_ = false;
    this->rtl_exec_(generation);
    return;
  }
  const RtlOp &op = *this->rtl_op_;
  if (this->rtl_writing_) {
    this->rtl_advance_(generation);
    return;
  }
  const uint8_t shift = op.width == 4 ? 0 : op.reg & (op.width == 2 ? 2 : 3);
  const uint32_t mask = op.width == 4 ? 0xFFFFFFFF : op.width == 2 ? 0xFFFF : 0xFF;
  const uint32_t value = (this->rtl_value_ >> (shift * 8)) & mask;
  switch (op.kind) {
    case RtlKind::MODIFY:
    case RtlKind::MODIFY_V1:
      this->rtl_write_(generation, op.mcu, op.reg, op.width, (value & ~op.clear) | op.set);
      return;
    case RtlKind::EEE: {
      const bool slow = this->link_speed_.load(std::memory_order_acquire) == RTL_LINK_10;
      this->rtl_write_(generation, op.mcu, op.reg, op.width, slow ? value | op.clear : value & ~op.clear);
      return;
    }
    case RtlKind::WAIT_SET:
    case RtlKind::WAIT_CLEAR:
    case RtlKind::WAIT_PHY: {
      bool done;
      if (op.kind == RtlKind::WAIT_PHY) {
        const uint32_t phy = value & PHY_STAT_MASK;
        done = op.set != PHY_STAT_ANY ? phy == op.set
                                      : phy == PHY_STAT_LAN_ON || phy == PHY_STAT_PWRDN || phy == PHY_STAT_EXT_INIT;
      } else {
        const bool set = (value & op.set) != 0;
        done = set == (op.kind == RtlKind::WAIT_SET);
      }
      if (!done && ++this->rtl_polls_ < (op.clear != 0 ? DELAYED_WAIT_TRIES : WAIT_TRIES)) {
        if (op.clear != 0) {
          this->set_timeout(op.clear, [this, generation]() {
            if (generation == this->generation_.load(std::memory_order_acquire))
              this->rtl_exec_(generation);
          });
        } else {
          this->rtl_read_(generation, op.mcu, op.reg);
        }
        return;
      }
      break;
    }
    case RtlKind::SNAPSHOT:
      this->rtl_registers_[op.set] = value;
      break;
    case RtlKind::STATUS:
      this->link_speed_.store((value & STATUS_1000) != 0  ? RTL_LINK_1000
                              : (value & STATUS_100) != 0 ? RTL_LINK_100
                              : (value & STATUS_10) != 0  ? RTL_LINK_10
                                                          : 0,
                              std::memory_order_release);
      break;
    case RtlKind::MAC_READ_LOW:
      write32(this->mac_, value);
      break;
    case RtlKind::MAC_READ_HIGH:
      this->mac_[4] = static_cast<uint8_t>(value);
      this->mac_[5] = static_cast<uint8_t>(value >> 8);
      this->mac_valid_ = true;
      this->vnd_check_mac_();
      break;
    case RtlKind::SKIP_ZERO:
    case RtlKind::SKIP_NONZERO:
      if (((value & op.set) == 0) == (op.kind == RtlKind::SKIP_ZERO)) {
        this->rtl_op_ += op.clear;
        this->rtl_left_ -= op.clear;
      }
      break;
    case RtlKind::SKIP_NOT_HALF:
      if ((value & (STATUS_10 | STATUS_100)) == 0 || (value & 0x01) != 0) {
        this->rtl_op_ += op.clear;
        this->rtl_left_ -= op.clear;
      }
      break;
    case RtlKind::OCP_MODIFY:
      this->rtl_write_(generation, MCU_PLA, (op.reg & 0x0FFF) | 0xB000, 2, (value & ~op.clear) | op.set);
      return;
    case RtlKind::SKIP_UNLESS_ALL:
      if ((value & op.set) != op.set) {
        this->rtl_op_ += op.clear;
        this->rtl_left_ -= op.clear;
      }
      break;
    case RtlKind::SKIP_NOT_EXT:
      if ((value & PHY_STAT_MASK) != PHY_STAT_EXT_INIT) {
        this->rtl_op_ += op.clear;
        this->rtl_left_ -= op.clear;
      }
      break;
    case RtlKind::EFUSE_HIGH:
      this->rtl_work_ = (value & 0x80) << 9;
      break;
    case RtlKind::EFUSE_LOW: {
      const uint32_t fuse = this->rtl_work_ | value;
      this->rtl_work_ = static_cast<uint16_t>(((fuse & 0x1FFF0) >> 1) | (fuse & 0x07));
      if (this->rtl_work_ == 0xFFFF) {
        this->rtl_op_ += op.clear;
        this->rtl_left_ -= op.clear;
      }
      break;
    }
    default:
      break;
  }
  this->rtl_advance_(generation);
}

void USBEthernet::rtl_done_(uint32_t generation) {
  switch (this->step_state_) {
    case Step::RTL_INIT:
      if (!this->start_data_()) {
        this->rtl_fail_();
        return;
      }
      this->report_connected_();
      if (is_8156(this->rtl_version_)) {
        this->rtl_script_(generation, Step::RTL_SPEED, SPEED_8156_OPS, op_count(SPEED_8156_OPS));
      } else if (is_8153(this->rtl_version_)) {
        this->rtl_script_(generation, Step::RTL_SPEED, SPEED_8153_OPS, op_count(SPEED_8153_OPS));
      } else {
        this->rtl_script_(generation, Step::RTL_SPEED, SPEED_OPS, op_count(SPEED_OPS));
      }
      return;
    case Step::RTL_SPEED:
      this->step_state_ = Step::RUNNING;
      this->rtl_link_(generation);
      return;
    case Step::RTL_ENABLE:
      this->step_state_ = Step::RUNNING;
      this->apply_link_(generation);
      this->rtl_link_(generation);
      return;
    default:
      return;
  }
}

void USBEthernet::rtl_link_(uint32_t generation) {
  if (generation != this->generation_.load(std::memory_order_acquire) || this->step_state_ != Step::RUNNING ||
      !this->started_)
    return;
  if (this->link_target_.load(std::memory_order_acquire) && !this->link_up_) {
    if (is_8156(this->rtl_version_)) {
      this->rtl_script_(generation, Step::RTL_ENABLE, ENABLE_8156_OPS, op_count(ENABLE_8156_OPS));
    } else if (is_8153b(this->rtl_version_) || this->rtl_version_ == 14) {
      this->rtl_script_(generation, Step::RTL_ENABLE, ENABLE_8153B_OPS, op_count(ENABLE_8153B_OPS));
    } else if (is_8153(this->rtl_version_)) {
      this->rtl_script_(generation, Step::RTL_ENABLE, ENABLE_8153_OPS, op_count(ENABLE_8153_OPS));
    } else {
      this->rtl_script_(generation, Step::RTL_ENABLE, ENABLE_OPS, op_count(ENABLE_OPS));
    }
  } else {
    this->apply_link_(generation);
  }
}

void USBEthernet::rtl_chain_(uint32_t generation, uint32_t stage) {
  if (stage != 1) {
    this->rtl_script_(generation, Step::RTL_INIT, UP_8156_OPS, op_count(UP_8156_OPS));
    return;
  }
  switch (this->rtl_version_) {
    case 10:
      this->rtl_script_(generation, Step::RTL_INIT, TUNE_8156_V10_OPS, op_count(TUNE_8156_V10_OPS));
      return;
    case 11:
      this->rtl_script_(generation, Step::RTL_INIT, TUNE_8156_V11_OPS, op_count(TUNE_8156_V11_OPS));
      return;
    case 12:
      this->rtl_script_(generation, Step::RTL_INIT, TUNE_8156_V12_OPS, op_count(TUNE_8156_V12_OPS));
      return;
    default:
      this->rtl_script_(generation, Step::RTL_INIT, TUNE_8156_V13_15_OPS, op_count(TUNE_8156_V13_15_OPS));
      return;
  }
}

void USBEthernet::rtl_capture_(const uint8_t *data, size_t len) {
  if (len >= 4)
    this->rtl_value_ = read32(data);
}

void USBEthernet::handle_rtl_notification_(const uint8_t *data, size_t len) {
  if (len < 2)
    return;
  const bool up = ((data[0] | (data[1] << 8)) & INTERRUPT_LINK) != 0;
  if (this->link_target_.exchange(up, std::memory_order_acq_rel) == up)
    return;
  const uint32_t generation = this->generation_.load(std::memory_order_acquire);
  this->defer([this, generation]() { this->rtl_link_(generation); });
}

void USBEthernet::handle_rtl_rx_(const uint8_t *data, size_t len) {
  if (this->rx_assembly_ == nullptr)
    return;
  size_t taken = 0;
  while (taken < len) {
    const size_t count = std::min<size_t>(RX_ASSEMBLY_LIMIT - this->rx_assembly_len_, len - taken);
    memcpy(this->rx_assembly_ + this->rx_assembly_len_, data + taken, count);
    this->rx_assembly_len_ += count;
    taken += count;
    size_t offset = 0;
    while (offset + RX_DESC_SIZE <= this->rx_assembly_len_) {
      const size_t frame_len = read32(this->rx_assembly_ + offset) & RX_LENGTH_MASK;
      const size_t frame = offset + RX_DESC_SIZE;
      if (frame_len < FRAME_MIN || frame_len > FRAME_MAX) {
        this->rx_assembly_len_ = 0;
        return;
      }
      if (frame + frame_len > this->rx_assembly_len_)
        break;
      this->deliver_(this->rx_assembly_ + frame, frame_len - FCS_SIZE);
      offset = std::min<size_t>((frame + frame_len + RX_ALIGN - 1) / RX_ALIGN * RX_ALIGN, this->rx_assembly_len_);
    }
    if (offset != 0) {
      this->rx_assembly_len_ -= offset;
      memmove(this->rx_assembly_, this->rx_assembly_ + offset, this->rx_assembly_len_);
    }
  }
  if (len < this->rx_size_)
    this->rx_assembly_len_ = 0;
}

size_t USBEthernet::rtl_fill_tx_(uint8_t *out, const uint8_t *frame, size_t len) {
  write32(out, static_cast<uint32_t>(len) | TX_FIRST_LAST);
  write32(out + 4, 0);
  memcpy(out + RTL8152_TX_DESC_SIZE, frame, len);
  return RTL8152_TX_DESC_SIZE + len;
}

}

#endif
