#pragma once

#include "esphome/core/defines.h"

#ifdef USE_USB_ETHERNET

#include "esphome/components/network/ip_address.h"
#include "esphome/components/usb_host/usb_host.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"
#include "esphome/core/optional.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <atomic>
#include <cstdint>
#include <vector>

namespace esphome::usb_ethernet {

struct ManualIP {
  network::IPAddress static_ip;
  network::IPAddress gateway;
  network::IPAddress subnet;
  network::IPAddress dns1;
  network::IPAddress dns2;
};

class USBEthernet;
struct RtlOp;
struct VndOp;

struct DataXfer {
  usb_transfer_t *xfer{nullptr};
  std::atomic<uint8_t> state{0};
  USBEthernet *owner{nullptr};
};

struct NetifGlue {
  esp_netif_driver_base_t base;
  USBEthernet *owner;
};

class USBEthernet : public usb_host::USBClient {
 public:
  USBEthernet(uint16_t vid, uint16_t pid);

  void setup() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::ETHERNET; }

#ifdef USE_USB_ETHERNET_MANUAL_IP
  void set_manual_ip(const ManualIP &manual_ip) { this->manual_ip_ = manual_ip; }
#endif

  bool is_connected() const;
  bool link_is_up() const { return this->link_up_; }
  uint32_t tx_sent() const { return this->tx_sent_.load(std::memory_order_relaxed); }
  uint32_t tx_failed() const { return this->tx_failed_.load(std::memory_order_relaxed); }
  uint32_t tx_dropped() const { return this->tx_dropped_.load(std::memory_order_relaxed); }
  int32_t tx_last_status() const { return this->tx_last_status_.load(std::memory_order_relaxed); }
  uint32_t rx_frames() const { return this->rx_frames_.load(std::memory_order_relaxed); }
  uint32_t rtl_register(size_t index) const { return this->rtl_registers_[index]; }
  esp_netif_t *get_esp_netif() const { return this->netif_; }
  network::IPAddresses get_ip_addresses();

 protected:
  enum class Proto : uint8_t { NONE, ECM, NCM, RTL8152, ASIX, AX88179, DM9601, SR9700, RNDIS };
  enum class Step : uint8_t {
    IDLE,
    MAC,
    NTB_PARAMS,
    NTB_INPUT,
    FILTER,
    ALT,
    RUNNING,
    VND,
    RNDIS,
    RTL_VERSION,
    RTL_INIT,
    RTL_SPEED,
    RTL_ENABLE
  };

  static constexpr uint8_t XFER_FREE = 0;
  static constexpr uint8_t XFER_IDLE = 1;
  static constexpr uint8_t XFER_BUSY = 2;
  static constexpr size_t RX_COUNT = 2;
  static constexpr size_t TX_COUNT = 2;
  static constexpr size_t NTB_LIMIT = 2048;
  static constexpr size_t NTB_MAX = 16384;
  struct VidPid {
    uint16_t vid;
    uint16_t pid;
  };
  static const VidPid RTL8152_IDS[];
  static const size_t RTL8152_ID_COUNT;
  static constexpr uint8_t RTL8152_CLASS = 0xFF;
  static constexpr size_t RTL8152_RX_SIZE = 512;
  static constexpr size_t RTL8152_TX_DESC_SIZE = 8;
  static const VidPid ASIX_IDS[];
  static const size_t ASIX_ID_COUNT;
  static const VidPid ASIX_EEPROM_MAC_IDS[];
  static const size_t ASIX_EEPROM_MAC_ID_COUNT;
  static const VidPid ASIX_178_IDS[];
  static const size_t ASIX_178_ID_COUNT;
  static const VidPid AX88179_IDS[];
  static const size_t AX88179_ID_COUNT;
  static const VidPid DM9601_IDS[];
  static const size_t DM9601_ID_COUNT;
  static const VidPid SR9700_IDS[];
  static const size_t SR9700_ID_COUNT;

  void on_connected() override;
  void on_disconnected() override;
  bool reports_connection_itself() const override { return true; }
  usb_host::MatchTier refine_match_(const usb_host::RoutedDevice &dev, usb_host::MatchTier tier) const override;
  uint32_t skipped_endpoints_(const usb_device_desc_t *desc, const usb_config_desc_t *config,
                              const usb_intf_desc_t *alt) const override;

  bool parse_descriptors_();
  void step_(uint32_t generation, bool ok);
  void next_(uint32_t generation, Step step);
  void control_(uint32_t generation, uint8_t type, uint8_t request, uint16_t value, uint16_t index,
                const std::vector<uint8_t> &data, int32_t w_length);
  void read_mac_(const uint8_t *data, size_t len);
  void read_ntb_params_(const uint8_t *data, size_t len);
  void fallback_mac_();
  bool start_data_();
  bool alloc_(DataXfer &x, size_t size, uint8_t ep, usb_transfer_cb_t cb);
  void stop_data_();
  void retire_(DataXfer &x);
  void release_slot_(DataXfer &x);
  bool submit_(DataXfer &x, size_t len);

  void apply_link_(uint32_t generation);
  void link_up_actions_();
  void link_down_actions_();

  static void rx_cb_(usb_transfer_t *xfer);
  static void tx_cb_(usb_transfer_t *xfer);
  static void notify_cb_(usb_transfer_t *xfer);
  void handle_rx_(const uint8_t *data, size_t len);
  void handle_ntb_(const uint8_t *data, size_t len);
  void deliver_(const uint8_t *data, size_t len);
  void handle_notification_(const uint8_t *data, size_t len);

  bool parse_rtl8152_();
  void rtl_read_(uint32_t generation, uint16_t mcu, uint16_t reg);
  void rtl_write_(uint32_t generation, uint16_t mcu, uint16_t reg, uint8_t width, uint32_t value);
  void rtl_start_(uint32_t generation);
  void rtl_begin_init_(uint32_t generation);
  void rtl_script_(uint32_t generation, Step step, const RtlOp *ops, size_t count);
  void rtl_exec_(uint32_t generation);
  void rtl_advance_(uint32_t generation);
  void rtl_step_(uint32_t generation, bool ok);
  void rtl_done_(uint32_t generation);
  void rtl_fail_();
  void rtl_link_(uint32_t generation);
  void rtl_capture_(const uint8_t *data, size_t len);
  void rtl_chain_(uint32_t generation, uint32_t stage);
  void handle_rtl_notification_(const uint8_t *data, size_t len);
  void handle_rtl_rx_(const uint8_t *data, size_t len);
  size_t rtl_fill_tx_(uint8_t *out, const uint8_t *frame, size_t len);
  bool alloc_rx_assembly_();
  void free_rx_assembly_();

  Proto vendor_family_(const usb_device_desc_t *dev) const;
  bool parse_vendor_();
  void vnd_start_(uint32_t generation);
  void vnd_script_(uint32_t generation, const VndOp *ops, size_t count);
  void vnd_exec_(uint32_t generation);
  void vnd_advance_(uint32_t generation);
  void vnd_step_(uint32_t generation, bool ok);
  void vnd_capture_(const uint8_t *data, size_t len);
  uint8_t vnd_hook_(uint32_t generation, uint8_t id);
  void vnd_start_data_(uint32_t generation);
  void vnd_fail_();
  void vnd_check_mac_();
  void vnd_link_(uint32_t generation);
  void vnd_link_done_(uint32_t generation);
  bool vnd_link_script_(uint32_t generation);
  void vnd_sizes_();
  void handle_vnd_notification_(const uint8_t *data, size_t len);
  void handle_vnd_rx_(const uint8_t *data, size_t len);
  size_t vnd_fill_tx_(uint8_t *out, const uint8_t *frame, size_t len);

  void asix_begin_(uint32_t generation);
  uint8_t asix_hook_(uint32_t generation, uint8_t id);
  bool asix_link_script_(uint32_t generation);
  void asix_rx_reset_();
  void handle_asix_rx_(const uint8_t *data, size_t len);
  size_t asix_fill_tx_(uint8_t *out, const uint8_t *frame, size_t len);

  void ax88179_begin_(uint32_t generation);
  uint8_t ax88179_hook_(uint32_t generation, uint8_t id);
  bool ax88179_link_script_(uint32_t generation);
  void handle_ax88179_rx_(const uint8_t *data, size_t len);
  size_t ax88179_fill_tx_(uint8_t *out, const uint8_t *frame, size_t len);

  void dm9601_begin_(uint32_t generation);
  uint8_t dm9601_hook_(uint32_t generation, uint8_t id);
  void handle_dm9601_rx_(const uint8_t *data, size_t len);
  size_t dm9601_fill_tx_(uint8_t *out, const uint8_t *frame, size_t len);
  void sr9700_begin_(uint32_t generation);
  uint8_t sr9700_hook_(uint32_t generation, uint8_t id);
  void handle_sr9700_rx_(const uint8_t *data, size_t len);
  size_t sr9700_fill_tx_(uint8_t *out, const uint8_t *frame, size_t len);

  bool parse_rndis_();
  void rndis_start_(uint32_t generation);
  void rndis_send_(uint32_t generation);
  void rndis_get_(uint32_t generation);
  void rndis_step_(uint32_t generation, bool ok);
  void rndis_capture_(const uint8_t *data, size_t len);
  void rndis_fail_();
  void rndis_poll_(uint32_t generation);
  void handle_rndis_rx_(const uint8_t *data, size_t len);
  size_t rndis_fill_tx_(uint8_t *out, const uint8_t *frame, size_t len);

  static esp_err_t post_attach_(esp_netif_t *netif, esp_netif_iodriver_handle h);
  static esp_err_t transmit_(void *h, void *buffer, size_t len);
  static void free_rx_buffer_(void *h, void *buffer);
  esp_err_t send_frame_(const uint8_t *frame, size_t len);
  static void ip_event_handler_(void *arg, esp_event_base_t base, int32_t id, void *data);

  esp_netif_t *netif_{nullptr};
  NetifGlue glue_{};
#ifdef USE_USB_ETHERNET_MANUAL_IP
  optional<ManualIP> manual_ip_{};
#endif

  Proto proto_{Proto::NONE};
  Step step_state_{Step::IDLE};
  uint8_t comm_intf_{0xFF};
  uint8_t data_intf_{0xFF};
  uint8_t data_alt_{0};
  bool comm_claimed_{false};
  bool data_claimed_{false};
  uint8_t notify_ep_{0};
  uint16_t notify_mps_{0};
  uint8_t in_ep_{0};
  uint16_t in_mps_{0};
  uint8_t out_ep_{0};
  uint16_t out_mps_{0};
  uint8_t mac_index_{0};
  uint8_t ncm_caps_{0};
  uint16_t max_segment_{1514};
  uint8_t mac_[6]{};
  bool mac_valid_{false};

  uint32_t ntb_in_max_{0};
  uint32_t ntb_out_max_{0};
  uint16_t ntb_out_divisor_{4};
  uint16_t ntb_out_remainder_{0};
  uint16_t ntb_out_alignment_{4};
  uint16_t ntb_sequence_{0};
  size_t rx_size_{0};
  size_t tx_size_{0};

  DataXfer rx_[RX_COUNT]{};
  DataXfer tx_[TX_COUNT]{};
  DataXfer notify_{};
  StaticSemaphore_t tx_sem_buf_{};
  SemaphoreHandle_t tx_sem_{nullptr};

  std::atomic<uint32_t> generation_{0};
  std::atomic<bool> running_{false};
  std::atomic<bool> link_target_{false};
  std::atomic<uint32_t> link_speed_{0};
  std::atomic<uint32_t> rx_dropped_{0};
  std::atomic<uint32_t> rx_frames_{0};
  std::atomic<uint32_t> tx_sent_{0};
  std::atomic<uint32_t> tx_failed_{0};
  std::atomic<uint32_t> tx_dropped_{0};
  std::atomic<int32_t> tx_last_status_{0};
  std::atomic<bool> got_ip4_{false};
  std::atomic<bool> got_ip6_{false};
  bool started_{false};
  bool link_up_{false};

  const RtlOp *rtl_op_{nullptr};
  uint32_t rtl_value_{0};
  uint16_t rtl_polls_{0};
  uint16_t rtl_left_{0};
  uint16_t rtl_ocp_base_{0xFFFF};
  bool rtl_base_pending_{false};
  bool asix_178_{false};
  uint8_t asix_ledmode_{0};
  uint16_t asix_gmii_{0};
  uint8_t rtl_version_{0};
  bool rtl_writing_{false};
  uint32_t rtl_work_{0};
  uint8_t *rx_assembly_{nullptr};
  size_t rx_assembly_len_{0};
  uint32_t rtl_registers_[4]{};

  const VndOp *vnd_op_{nullptr};
  uint32_t vnd_data_{0};
  uint16_t vnd_tries_{0};
  uint16_t vnd_saved_{0};
  uint8_t vnd_left_{0};
  uint8_t vnd_phy_{0};
  uint8_t vnd_chip_{0};
  bool asix_eeprom_mac_{false};
  bool asix_split_{false};
  uint16_t asix_remaining_{0};
  uint32_t asix_header_{0};

  uint32_t rndis_xid_{0};
  uint32_t rndis_tx_limit_{0};
  uint32_t rndis_event_{0};
  uint32_t rndis_value_{0};
  uint8_t rndis_phase_{0};
  uint8_t rndis_tries_{0};
  bool rndis_matched_{false};
  bool rndis_waiting_{false};
  bool rndis_again_{false};
};

extern USBEthernet *global_usb_ethernet;

}

#endif
