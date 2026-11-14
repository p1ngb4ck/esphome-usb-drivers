#include "usb_ethernet_vendor.h"

#ifdef USE_USB_ETHERNET

#include "esphome/core/log.h"

#include "esp_heap_caps.h"
#include "usb/usb_helpers.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace esphome::usb_ethernet {

static const char *const TAG = "usb_ethernet";

static constexpr uint8_t VENDOR_OUT = usb_host::USB_DIR_OUT | usb_host::USB_TYPE_VENDOR | usb_host::USB_RECIP_DEVICE;
static constexpr uint8_t VENDOR_IN = usb_host::USB_DIR_IN | usb_host::USB_TYPE_VENDOR | usb_host::USB_RECIP_DEVICE;
static constexpr size_t RX_ASSEMBLY_SIZE = 2048;

USBEthernet::Proto USBEthernet::vendor_family_(const usb_device_desc_t *dev) const {
  auto has = [dev](const VidPid *table, size_t count) {
    return std::any_of(table, table + count,
                       [dev](const VidPid &id) { return id.vid == dev->idVendor && id.pid == dev->idProduct; });
  };
  if (has(ASIX_IDS, ASIX_ID_COUNT) || has(ASIX_EEPROM_MAC_IDS, ASIX_EEPROM_MAC_ID_COUNT) ||
      has(ASIX_178_IDS, ASIX_178_ID_COUNT))
    return Proto::ASIX;
  if (has(AX88179_IDS, AX88179_ID_COUNT))
    return Proto::AX88179;
  if (has(DM9601_IDS, DM9601_ID_COUNT))
    return Proto::DM9601;
  if (has(SR9700_IDS, SR9700_ID_COUNT))
    return Proto::SR9700;
  return Proto::NONE;
}

bool USBEthernet::parse_vendor_() {
  const usb_device_desc_t *dev = this->get_device_desc_();
  const usb_config_desc_t *cfg = this->get_config_desc_();
  if (dev == nullptr || cfg == nullptr)
    return false;
  const Proto family = this->vendor_family_(dev);
  if (family == Proto::NONE)
    return false;
  bool eligible = false;
  uint8_t number = 0xFF;
  uint8_t in_ep = 0, out_ep = 0, notify_ep = 0;
  uint16_t in_mps = 0, out_mps = 0, notify_mps = 0;
  int offset = 0;
  const auto *desc = reinterpret_cast<const usb_standard_desc_t *>(cfg);
  while ((desc = usb_parse_next_descriptor(desc, cfg->wTotalLength, &offset)) != nullptr) {
    if (desc->bDescriptorType == USB_W_VALUE_DT_INTERFACE) {
      if (in_ep != 0 && out_ep != 0 && notify_ep != 0)
        break;
      const auto *intf = reinterpret_cast<const usb_intf_desc_t *>(desc);
      eligible = intf->bAlternateSetting == 0 && this->interface_routed_(intf->bInterfaceNumber);
      number = intf->bInterfaceNumber;
      in_ep = out_ep = notify_ep = 0;
      continue;
    }
    if (!eligible || desc->bDescriptorType != USB_W_VALUE_DT_ENDPOINT)
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
  this->proto_ = family;
  this->data_intf_ = number;
  this->data_alt_ = 0;
  this->in_ep_ = in_ep;
  this->in_mps_ = in_mps;
  this->out_ep_ = out_ep;
  this->out_mps_ = out_mps;
  this->notify_ep_ = notify_ep;
  this->notify_mps_ = notify_mps;
  return true;
}

bool USBEthernet::alloc_rx_assembly_() {
  this->free_rx_assembly_();
  this->rx_assembly_ = static_cast<uint8_t *>(heap_caps_malloc(RX_ASSEMBLY_SIZE, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
  if (this->rx_assembly_ == nullptr) {
    ESP_LOGE(TAG, "No %u bytes of PSRAM for received frames", static_cast<unsigned>(RX_ASSEMBLY_SIZE));
    return false;
  }
  return true;
}

void USBEthernet::free_rx_assembly_() {
  heap_caps_free(this->rx_assembly_);
  this->rx_assembly_ = nullptr;
  this->rx_assembly_len_ = 0;
}

void USBEthernet::vnd_start_(uint32_t generation) {
  this->mac_valid_ = false;
  this->vnd_tries_ = 0;
  this->link_speed_.store(0, std::memory_order_release);
  switch (this->proto_) {
    case Proto::ASIX:
      this->asix_begin_(generation);
      return;
    case Proto::AX88179:
      this->ax88179_begin_(generation);
      return;
    case Proto::DM9601:
      this->dm9601_begin_(generation);
      return;
    case Proto::SR9700:
      this->sr9700_begin_(generation);
      return;
    default:
      this->vnd_fail_();
      return;
  }
}

void USBEthernet::vnd_script_(uint32_t generation, const VndOp *ops, size_t count) {
  this->step_state_ = Step::VND;
  this->vnd_op_ = ops;
  this->vnd_left_ = static_cast<uint8_t>(count);
  this->vnd_tries_ = 0;
  this->vnd_exec_(generation);
}

void USBEthernet::vnd_advance_(uint32_t generation) {
  this->vnd_op_++;
  this->vnd_left_--;
  this->vnd_exec_(generation);
}

void USBEthernet::vnd_exec_(uint32_t generation) {
  const uint8_t ep0 = this->get_device_desc_() != nullptr ? this->get_device_desc_()->bMaxPacketSize0 : 64;
  while (this->vnd_left_ != 0) {
    const VndOp &op = *this->vnd_op_;
    const uint16_t value = op.value == VND_VALUE ? static_cast<uint16_t>(this->vnd_data_)
                           : op.value == VND_PHY ? this->vnd_phy_
                                                 : op.value;
    switch (op.kind) {
      case VndKind::OUT:
        this->control_(generation, VENDOR_OUT, op.request, value, op.index, {}, -1);
        return;
      case VndKind::OUT8:
        this->control_(generation, VENDOR_OUT, op.request, value, op.index, {static_cast<uint8_t>(op.arg)}, -1);
        return;
      case VndKind::OUT16: {
        std::vector<uint8_t> data(2);
        vnd_put16(data.data(), static_cast<uint16_t>(op.arg));
        this->control_(generation, VENDOR_OUT, op.request, value, op.index, data, -1);
        return;
      }
      case VndKind::OUT_DATA: {
        std::vector<uint8_t> data(op.arg);
        for (size_t i = 0; i < data.size(); i++)
          data[i] = static_cast<uint8_t>(this->vnd_data_ >> (i * 8));
        this->control_(generation, VENDOR_OUT, op.request, value, op.index, data, -1);
        return;
      }
      case VndKind::OUT_BYTES:
        this->control_(generation, VENDOR_OUT, op.request, value, op.index,
                       std::vector<uint8_t>(op.bytes, op.bytes + op.arg), -1);
        return;
      case VndKind::OUT_MAC:
        this->control_(generation, VENDOR_OUT, op.request, value, op.index,
                       std::vector<uint8_t>(this->mac_, this->mac_ + 6), -1);
        return;
      case VndKind::IN:
      case VndKind::IN_OPTIONAL:
      case VndKind::IN_MAC:
        this->control_(generation, VENDOR_IN, op.request, value, op.index,
                       std::vector<uint8_t>(vnd_round_up(op.arg, ep0)), static_cast<int32_t>(op.arg));
        return;
      case VndKind::DELAY:
        this->set_timeout(op.arg, [this, generation]() {
          if (generation == this->generation_.load(std::memory_order_acquire))
            this->vnd_advance_(generation);
        });
        return;
      case VndKind::HOOK: {
        this->vnd_op_++;
        this->vnd_left_--;
        const VndOp *next = this->vnd_op_;
        const uint8_t skip = this->vnd_hook_(generation, op.request);
        if (this->step_state_ != Step::VND || this->vnd_op_ != next)
          return;
        this->vnd_op_ += skip;
        this->vnd_left_ -= skip;
        continue;
      }
      case VndKind::UNTIL_SET:
      case VndKind::UNTIL_CLEAR: {
        const bool set = (this->vnd_data_ & op.arg) != 0;
        if (set == (op.kind == VndKind::UNTIL_SET) || ++this->vnd_tries_ >= op.value) {
          this->vnd_tries_ = 0;
          this->vnd_op_++;
          this->vnd_left_--;
          continue;
        }
        this->vnd_op_ -= op.request;
        this->vnd_left_ += op.request;
        if (op.index != 0) {
          this->set_timeout(op.index, [this, generation]() {
            if (generation == this->generation_.load(std::memory_order_acquire))
              this->vnd_exec_(generation);
          });
          return;
        }
        continue;
      }
    }
  }
  this->step_state_ = Step::RUNNING;
}

void USBEthernet::vnd_step_(uint32_t generation, bool ok) {
  if (!ok) {
    if (this->vnd_op_->kind != VndKind::IN_OPTIONAL) {
      this->vnd_fail_();
      return;
    }
    this->vnd_data_ = 0xFFFFFFFF;
  }
  this->vnd_advance_(generation);
}

void USBEthernet::vnd_capture_(const uint8_t *data, size_t len) {
  const VndOp &op = *this->vnd_op_;
  if (op.kind == VndKind::IN_MAC) {
    if (len >= 6) {
      memcpy(this->mac_, data, 6);
      this->mac_valid_ = true;
    }
    return;
  }
  if (op.kind != VndKind::IN && op.kind != VndKind::IN_OPTIONAL)
    return;
  uint32_t value = 0;
  const size_t count = std::min<size_t>(len, std::min<uint32_t>(op.arg, 4));
  for (size_t i = 0; i < count; i++)
    value |= static_cast<uint32_t>(data[i]) << (i * 8);
  this->vnd_data_ = value;
}

uint8_t USBEthernet::vnd_hook_(uint32_t generation, uint8_t id) {
  switch (this->proto_) {
    case Proto::ASIX:
      return this->asix_hook_(generation, id);
    case Proto::AX88179:
      return this->ax88179_hook_(generation, id);
    case Proto::DM9601:
      return this->dm9601_hook_(generation, id);
    case Proto::SR9700:
      return this->sr9700_hook_(generation, id);
    default:
      this->vnd_fail_();
      return 0;
  }
}

void USBEthernet::vnd_check_mac_() {
  const bool zero = std::all_of(this->mac_, this->mac_ + 6, [](uint8_t b) { return b == 0; });
  if (!this->mac_valid_ || zero || (this->mac_[0] & 0x01) != 0)
    this->fallback_mac_();
}

void USBEthernet::vnd_start_data_(uint32_t generation) {
  if (!this->start_data_()) {
    this->vnd_fail_();
    return;
  }
  this->step_state_ = Step::RUNNING;
  this->report_connected_();
  this->vnd_link_(generation);
}

void USBEthernet::vnd_fail_() {
  ESP_LOGE(TAG, "Adapter register access failed");
  this->vnd_left_ = 0;
  if (!this->started_) {
    this->free_rx_assembly_();
    this->step_state_ = Step::IDLE;
  } else {
    this->step_state_ = Step::RUNNING;
  }
  this->report_connected_();
}

void USBEthernet::vnd_link_(uint32_t generation) {
  if (generation != this->generation_.load(std::memory_order_acquire) || this->step_state_ != Step::RUNNING ||
      !this->started_)
    return;
  if (this->link_target_.load(std::memory_order_acquire) && !this->link_up_ && this->vnd_link_script_(generation))
    return;
  this->apply_link_(generation);
}

void USBEthernet::vnd_link_done_(uint32_t generation) {
  this->step_state_ = Step::RUNNING;
  this->apply_link_(generation);
  this->vnd_link_(generation);
}

bool USBEthernet::vnd_link_script_(uint32_t generation) {
  if (this->proto_ == Proto::ASIX)
    return this->asix_link_script_(generation);
  if (this->proto_ == Proto::AX88179)
    return this->ax88179_link_script_(generation);
  return false;
}

void USBEthernet::vnd_sizes_() {
  switch (this->proto_) {
    case Proto::ASIX:
      this->rx_size_ = vnd_round_up(2048, this->in_mps_);
      this->tx_size_ = vnd_round_up(1522, this->out_mps_);
      return;
    case Proto::AX88179:
      this->rx_size_ = vnd_round_up(1024 * 20, this->in_mps_);
      this->tx_size_ = vnd_round_up(1523, this->out_mps_);
      return;
    case Proto::DM9601:
      this->rx_size_ = vnd_round_up(1522, this->in_mps_);
      this->tx_size_ = vnd_round_up(1519, this->out_mps_);
      return;
    case Proto::SR9700:
      this->rx_size_ = vnd_round_up(3072, this->in_mps_);
      this->tx_size_ = vnd_round_up(1517, this->out_mps_);
      return;
    case Proto::RNDIS:
      this->rx_size_ = (1558 + static_cast<size_t>(this->in_mps_) + 1) & ~(static_cast<size_t>(this->in_mps_) - 1);
      this->tx_size_ = vnd_round_up(1559, this->out_mps_);
      return;
    default:
      return;
  }
}

void USBEthernet::handle_vnd_notification_(const uint8_t *data, size_t len) {
  bool up;
  switch (this->proto_) {
    case Proto::ASIX:
      if (len < 8)
        return;
      up = (data[2] & 0x01) != 0;
      break;
    case Proto::AX88179:
      if (len < 8)
        return;
      up = (vnd_le32(data) & 0x00010000) != 0;
      break;
    case Proto::DM9601:
    case Proto::SR9700:
      if (len < 8)
        return;
      up = (data[0] & 0x40) != 0;
      break;
    case Proto::RNDIS: {
      const uint32_t generation = this->generation_.load(std::memory_order_acquire);
      this->defer([this, generation]() { this->rndis_poll_(generation); });
      return;
    }
    default:
      return;
  }
  if (this->link_target_.exchange(up, std::memory_order_acq_rel) == up)
    return;
  const uint32_t generation = this->generation_.load(std::memory_order_acquire);
  this->defer([this, generation]() { this->vnd_link_(generation); });
}

void USBEthernet::handle_vnd_rx_(const uint8_t *data, size_t len) {
  switch (this->proto_) {
    case Proto::ASIX:
      this->handle_asix_rx_(data, len);
      return;
    case Proto::AX88179:
      this->handle_ax88179_rx_(data, len);
      return;
    case Proto::DM9601:
      this->handle_dm9601_rx_(data, len);
      return;
    case Proto::SR9700:
      this->handle_sr9700_rx_(data, len);
      return;
    case Proto::RNDIS:
      this->handle_rndis_rx_(data, len);
      return;
    default:
      return;
  }
}

size_t USBEthernet::vnd_fill_tx_(uint8_t *out, const uint8_t *frame, size_t len) {
  switch (this->proto_) {
    case Proto::ASIX:
      return this->asix_fill_tx_(out, frame, len);
    case Proto::AX88179:
      return this->ax88179_fill_tx_(out, frame, len);
    case Proto::DM9601:
      return this->dm9601_fill_tx_(out, frame, len);
    case Proto::SR9700:
      return this->sr9700_fill_tx_(out, frame, len);
    case Proto::RNDIS:
      return this->rndis_fill_tx_(out, frame, len);
    default:
      return 0;
  }
}

}

#endif
