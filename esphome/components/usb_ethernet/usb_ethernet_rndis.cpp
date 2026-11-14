#include "usb_ethernet_vendor.h"

#ifdef USE_USB_ETHERNET

#include "esphome/core/log.h"

#include "usb/usb_helpers.h"

#include <algorithm>
#include <cstring>
#include <vector>

namespace esphome::usb_ethernet {

static const char *const TAG = "usb_ethernet";

static constexpr uint8_t REQ_SEND_ENCAPSULATED = 0x00;
static constexpr uint8_t REQ_GET_ENCAPSULATED = 0x01;
static constexpr uint32_t MSG_COMPLETION = 0x80000000;
static constexpr uint32_t MSG_PACKET = 0x00000001;
static constexpr uint32_t MSG_INIT = 0x00000002;
static constexpr uint32_t MSG_QUERY = 0x00000004;
static constexpr uint32_t MSG_SET = 0x00000005;
static constexpr uint32_t MSG_INDICATE = 0x00000007;
static constexpr uint32_t MSG_KEEPALIVE = 0x00000008;
static constexpr uint32_t STATUS_SUCCESS = 0x00000000;
static constexpr uint32_t STATUS_MEDIA_CONNECT = 0x4001000B;
static constexpr uint32_t STATUS_MEDIA_DISCONNECT = 0x4001000C;
static constexpr uint32_t OID_PACKET_FILTER = 0x0001010E;
static constexpr uint32_t OID_PERMANENT_ADDRESS = 0x01010101;
static constexpr uint32_t PACKET_FILTER_DEFAULT = 0x00000001 | 0x00000008 | 0x00000004 | 0x00000020;
static constexpr size_t INIT_SIZE = 24;
static constexpr size_t QUERY_SIZE = 28;
static constexpr size_t QUERY_PAYLOAD = 48;
static constexpr size_t SET_SIZE = 32;
static constexpr size_t KEEPALIVE_C_SIZE = 16;
static constexpr size_t DATA_HEADER_SIZE = 44;
static constexpr size_t RESPONSE_MAX = 512;
static constexpr uint8_t GET_TRIES = 10;
static constexpr uint32_t RETRY_MS = 40;
static constexpr uint8_t CDC_CLASS_COMM = 0x02;
static constexpr uint8_t CDC_CLASS_DATA = 0x0A;
static constexpr uint8_t CDC_FUNC_UNION = 0x06;
static constexpr uint8_t CS_INTERFACE = 0x24;

enum RndisPhase : uint8_t { PHASE_INIT, PHASE_MAC, PHASE_FILTER, PHASE_EVENT };

static bool is_rndis_comm(const usb_intf_desc_t *intf) {
  return (intf->bInterfaceClass == 0xE0 && intf->bInterfaceSubClass == 0x01 && intf->bInterfaceProtocol == 0x03) ||
         (intf->bInterfaceClass == 0xEF && intf->bInterfaceSubClass == 0x04 && intf->bInterfaceProtocol == 0x01) ||
         (intf->bInterfaceClass == CDC_CLASS_COMM && intf->bInterfaceSubClass == 0x02 &&
          intf->bInterfaceProtocol == 0xFF);
}

bool USBEthernet::parse_rndis_() {
  const usb_config_desc_t *cfg = this->get_config_desc_();
  if (cfg == nullptr)
    return false;
  bool in_comm = false, have_union = false;
  uint8_t comm = 0xFF, slave = 0xFF, notify = 0;
  uint16_t notify_mps = 0;
  int offset = 0;
  const auto *desc = reinterpret_cast<const usb_standard_desc_t *>(cfg);
  while ((desc = usb_parse_next_descriptor(desc, cfg->wTotalLength, &offset)) != nullptr) {
    const auto *raw = reinterpret_cast<const uint8_t *>(desc);
    if (desc->bDescriptorType == USB_W_VALUE_DT_INTERFACE) {
      const auto *intf = reinterpret_cast<const usb_intf_desc_t *>(desc);
      if (comm != 0xFF)
        break;
      in_comm = intf->bAlternateSetting == 0 && is_rndis_comm(intf) && this->interface_routed_(intf->bInterfaceNumber);
      if (in_comm)
        comm = intf->bInterfaceNumber;
      continue;
    }
    if (!in_comm)
      continue;
    if (desc->bDescriptorType == CS_INTERFACE && desc->bLength >= 5 && raw[2] == CDC_FUNC_UNION) {
      slave = raw[4];
      have_union = true;
    } else if (desc->bDescriptorType == USB_W_VALUE_DT_ENDPOINT && notify == 0) {
      const auto *ep = reinterpret_cast<const usb_ep_desc_t *>(desc);
      if (USB_EP_DESC_GET_XFERTYPE(ep) == USB_BM_ATTRIBUTES_XFER_INT && USB_EP_DESC_GET_EP_DIR(ep)) {
        notify = ep->bEndpointAddress;
        notify_mps = USB_EP_DESC_GET_MPS(ep);
      }
    }
  }
  if (comm == 0xFF || notify == 0)
    return false;
  const uint8_t candidates[2] = {have_union ? slave : static_cast<uint8_t>(comm + 1), static_cast<uint8_t>(comm + 1)};
  uint8_t chosen = 0xFF;
  uint8_t in_ep = 0, out_ep = 0;
  uint16_t in_mps = 0, out_mps = 0;
  for (const uint8_t candidate : candidates) {
    offset = 0;
    desc = reinterpret_cast<const usb_standard_desc_t *>(cfg);
    bool in_data = false;
    in_ep = out_ep = 0;
    in_mps = out_mps = 0;
    while ((desc = usb_parse_next_descriptor(desc, cfg->wTotalLength, &offset)) != nullptr) {
      if (desc->bDescriptorType == USB_W_VALUE_DT_INTERFACE) {
        const auto *intf = reinterpret_cast<const usb_intf_desc_t *>(desc);
        in_data = intf->bInterfaceNumber == candidate && intf->bAlternateSetting == 0 &&
                  intf->bInterfaceClass == CDC_CLASS_DATA;
        continue;
      }
      if (!in_data || desc->bDescriptorType != USB_W_VALUE_DT_ENDPOINT)
        continue;
      const auto *ep = reinterpret_cast<const usb_ep_desc_t *>(desc);
      if (USB_EP_DESC_GET_XFERTYPE(ep) != USB_BM_ATTRIBUTES_XFER_BULK)
        continue;
      if (USB_EP_DESC_GET_EP_DIR(ep)) {
        in_ep = ep->bEndpointAddress;
        in_mps = USB_EP_DESC_GET_MPS(ep);
      } else {
        out_ep = ep->bEndpointAddress;
        out_mps = USB_EP_DESC_GET_MPS(ep);
      }
    }
    if (in_ep != 0 && out_ep != 0) {
      chosen = candidate;
      break;
    }
  }
  if (chosen == 0xFF)
    return false;
  this->proto_ = Proto::RNDIS;
  this->comm_intf_ = comm;
  this->data_intf_ = chosen;
  this->data_alt_ = 0;
  this->notify_ep_ = notify;
  this->notify_mps_ = notify_mps;
  this->in_ep_ = in_ep;
  this->in_mps_ = in_mps;
  this->out_ep_ = out_ep;
  this->out_mps_ = out_mps;
  return true;
}

void USBEthernet::rndis_start_(uint32_t generation) {
  this->mac_valid_ = false;
  this->rndis_xid_ = 0;
  this->rndis_again_ = false;
  this->rndis_phase_ = PHASE_INIT;
  this->rndis_send_(generation);
}

void USBEthernet::rndis_send_(uint32_t generation) {
  this->step_state_ = Step::RNDIS;
  this->rndis_waiting_ = false;
  this->rndis_tries_ = 0;
  this->rndis_matched_ = false;
  this->rndis_xid_++;
  if (this->rndis_xid_ == 0)
    this->rndis_xid_ = 1;
  std::vector<uint8_t> message;
  switch (this->rndis_phase_) {
    case PHASE_INIT: {
      message.assign(INIT_SIZE, 0);
      size_t mps = this->in_mps_;
      vnd_put32(message.data(), MSG_INIT);
      vnd_put32(message.data() + 4, INIT_SIZE);
      vnd_put32(message.data() + 8, this->rndis_xid_);
      vnd_put32(message.data() + 12, 1);
      vnd_put32(message.data() + 20, static_cast<uint32_t>((1558 + mps + 1) & ~(mps - 1)));
      break;
    }
    case PHASE_MAC:
      message.assign(QUERY_SIZE + QUERY_PAYLOAD, 0);
      vnd_put32(message.data(), MSG_QUERY);
      vnd_put32(message.data() + 4, QUERY_SIZE + QUERY_PAYLOAD);
      vnd_put32(message.data() + 8, this->rndis_xid_);
      vnd_put32(message.data() + 12, OID_PERMANENT_ADDRESS);
      vnd_put32(message.data() + 16, QUERY_PAYLOAD);
      vnd_put32(message.data() + 20, 20);
      break;
    case PHASE_FILTER:
      message.assign(SET_SIZE, 0);
      vnd_put32(message.data(), MSG_SET);
      vnd_put32(message.data() + 4, SET_SIZE);
      vnd_put32(message.data() + 8, this->rndis_xid_);
      vnd_put32(message.data() + 12, OID_PACKET_FILTER);
      vnd_put32(message.data() + 16, 4);
      vnd_put32(message.data() + 20, 20);
      vnd_put32(message.data() + 28, PACKET_FILTER_DEFAULT);
      break;
    default:
      return;
  }
  this->control_(generation, usb_host::USB_DIR_OUT | usb_host::USB_TYPE_CLASS | usb_host::USB_RECIP_INTERFACE,
                 REQ_SEND_ENCAPSULATED, 0, this->comm_intf_, message, -1);
}

void USBEthernet::rndis_get_(uint32_t generation) {
  this->rndis_waiting_ = true;
  this->rndis_matched_ = false;
  const uint8_t ep0 = this->get_device_desc_() != nullptr ? this->get_device_desc_()->bMaxPacketSize0 : 64;
  const size_t capacity = this->transfer_capacity() > 8 ? this->transfer_capacity() - 8 : 0;
  size_t size = std::min<size_t>(RESPONSE_MAX, capacity);
  size -= size % ep0;
  this->control_(generation, usb_host::USB_DIR_IN | usb_host::USB_TYPE_CLASS | usb_host::USB_RECIP_INTERFACE,
                 REQ_GET_ENCAPSULATED, 0, this->comm_intf_, std::vector<uint8_t>(size), static_cast<int32_t>(size));
}

void USBEthernet::rndis_capture_(const uint8_t *data, size_t len) {
  if (!this->rndis_waiting_ || data == nullptr || len < 12)
    return;
  const uint32_t type = vnd_le32(data);
  if (this->rndis_phase_ == PHASE_EVENT) {
    this->rndis_event_ = type;
    this->rndis_value_ = vnd_le32(data + 8);
    this->rndis_matched_ = true;
    return;
  }
  const uint32_t request = this->rndis_phase_ == PHASE_INIT  ? MSG_INIT
                           : this->rndis_phase_ == PHASE_MAC ? MSG_QUERY
                                                             : MSG_SET;
  if (len < 16 || type != (request | MSG_COMPLETION) || vnd_le32(data + 8) != this->rndis_xid_ ||
      vnd_le32(data + 12) != STATUS_SUCCESS)
    return;
  if (this->rndis_phase_ == PHASE_INIT) {
    if (len < 40)
      return;
    this->rndis_value_ = vnd_le32(data + 36);
  } else if (this->rndis_phase_ == PHASE_MAC) {
    if (len < 24)
      return;
    const uint32_t data_len = vnd_le32(data + 16);
    const uint32_t data_offset = vnd_le32(data + 20);
    if (data_len != 6 || data_offset > 1024 || 8 + data_offset + data_len > len)
      return;
    memcpy(this->mac_, data + 8 + data_offset, 6);
    this->mac_valid_ = true;
  }
  this->rndis_matched_ = true;
}

void USBEthernet::rndis_fail_() {
  ESP_LOGE(TAG, "RNDIS bring-up failed");
  this->step_state_ = this->started_ ? Step::RUNNING : Step::IDLE;
  this->report_connected_();
}

void USBEthernet::rndis_step_(uint32_t generation, bool ok) {
  if (!this->rndis_waiting_) {
    if (!ok) {
      this->rndis_fail_();
      return;
    }
    this->rndis_get_(generation);
    return;
  }
  if (this->rndis_phase_ == PHASE_EVENT) {
    this->step_state_ = Step::RUNNING;
    this->rndis_waiting_ = false;
    if (ok && this->rndis_matched_) {
      if (this->rndis_event_ == MSG_INDICATE) {
        if (this->rndis_value_ == STATUS_MEDIA_CONNECT) {
          this->link_target_.store(true, std::memory_order_release);
          this->vnd_link_(generation);
        } else if (this->rndis_value_ == STATUS_MEDIA_DISCONNECT) {
          this->link_target_.store(false, std::memory_order_release);
          this->vnd_link_(generation);
        }
      } else if (this->rndis_event_ == MSG_KEEPALIVE) {
        std::vector<uint8_t> reply(KEEPALIVE_C_SIZE, 0);
        vnd_put32(reply.data(), MSG_KEEPALIVE | MSG_COMPLETION);
        vnd_put32(reply.data() + 4, KEEPALIVE_C_SIZE);
        vnd_put32(reply.data() + 8, this->rndis_value_);
        this->control_transfer(usb_host::USB_DIR_OUT | usb_host::USB_TYPE_CLASS | usb_host::USB_RECIP_INTERFACE,
                               REQ_SEND_ENCAPSULATED, 0, this->comm_intf_, [](const usb_host::TransferStatus &) {},
                               reply, -1);
      }
    }
    if (this->rndis_again_) {
      this->rndis_again_ = false;
      this->rndis_poll_(generation);
    }
    return;
  }
  if (!ok || !this->rndis_matched_) {
    if (++this->rndis_tries_ >= GET_TRIES) {
      this->rndis_fail_();
      return;
    }
    this->set_timeout(RETRY_MS, [this, generation]() {
      if (generation == this->generation_.load(std::memory_order_acquire))
        this->rndis_get_(generation);
    });
    return;
  }
  switch (this->rndis_phase_) {
    case PHASE_INIT:
      if (this->rndis_value_ <= DATA_HEADER_SIZE) {
        this->rndis_fail_();
        return;
      }
      this->rndis_tx_limit_ = this->rndis_value_;
      this->rndis_phase_ = PHASE_MAC;
      this->rndis_send_(generation);
      return;
    case PHASE_MAC:
      this->vnd_check_mac_();
      this->rndis_phase_ = PHASE_FILTER;
      this->rndis_send_(generation);
      return;
    case PHASE_FILTER:
      this->rndis_waiting_ = false;
      if (!this->start_data_()) {
        this->rndis_fail_();
        return;
      }
      this->step_state_ = Step::RUNNING;
      this->report_connected_();
      this->link_target_.store(true, std::memory_order_release);
      this->vnd_link_(generation);
      return;
    default:
      return;
  }
}

void USBEthernet::rndis_poll_(uint32_t generation) {
  if (generation != this->generation_.load(std::memory_order_acquire) || !this->started_)
    return;
  if (this->step_state_ != Step::RUNNING) {
    this->rndis_again_ = true;
    return;
  }
  this->step_state_ = Step::RNDIS;
  this->rndis_phase_ = PHASE_EVENT;
  this->rndis_tries_ = 0;
  this->rndis_get_(generation);
}

void USBEthernet::handle_rndis_rx_(const uint8_t *data, size_t len) {
  while (len >= DATA_HEADER_SIZE) {
    const uint32_t type = vnd_le32(data);
    const uint32_t message_len = vnd_le32(data + 4);
    const uint32_t offset = vnd_le32(data + 8);
    const uint32_t frame_len = vnd_le32(data + 12);
    if (type != MSG_PACKET || message_len > len || message_len == 0 || offset > 0xFFFF || frame_len > 0xFFFF ||
        offset + frame_len + 8 > message_len)
      return;
    this->deliver_(data + 8 + offset, frame_len);
    data += message_len;
    len -= message_len;
  }
}

size_t USBEthernet::rndis_fill_tx_(uint8_t *out, const uint8_t *frame, size_t len) {
  size_t total = DATA_HEADER_SIZE + len;
  if (total > this->rndis_tx_limit_ || total + 1 > this->tx_size_)
    return 0;
  memset(out, 0, DATA_HEADER_SIZE);
  vnd_put32(out, MSG_PACKET);
  vnd_put32(out + 4, static_cast<uint32_t>(total));
  vnd_put32(out + 8, DATA_HEADER_SIZE - 8);
  vnd_put32(out + 12, static_cast<uint32_t>(len));
  memcpy(out + DATA_HEADER_SIZE, frame, len);
  if (total % this->out_mps_ == 0) {
    out[total] = 0;
    total++;
  }
  return total;
}

}

#endif
