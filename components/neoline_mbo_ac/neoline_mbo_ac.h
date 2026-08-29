#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/select/select.h"

namespace esphome {
namespace neoline_mbo_ac {

class NeolineMBOACClimate : public climate::Climate, public Component, public uart::UARTDevice {
 public:
  // Дополнительные железные контроли для вывода в HA
  switch_::Switch *h_swing_switch{nullptr};
  switch_::Switch *display_switch{nullptr};
  select::Select *turbo_select{nullptr};

  void setup() override {
    this->target_temperature = 22.0;
    this->mode = climate::CLIMATE_MODE_COOL;
    this->fan_mode = climate::CLIMATE_FAN_AUTO;
    this->swing_mode = climate::CLIMATE_SWING_OFF;
    this->current_temperature = NAN;
  }

  void loop() override {
    while (this->available() > 0) {
      uint8_t b;
      this->read_byte(&b);
      this->rx_buf_.push_back(b);
      this->last_rx_time_ = millis();
    }

    if (!this->rx_buf_.empty() && (millis() - this->last_rx_time_ > 20)) {
      this->parse_packet_();
      this->rx_buf_.clear();
    }

    if (millis() - this->last_ping_time_ > 1500) {
      uint8_t ping_packet[] = {0x55, 0xAA, 0x00, 0x08, 0x00, 0x00, 0x07};
      this->write_array(ping_packet, 7);
      this->last_ping_time_ = millis();
    }
  }

  // Специфический метод для включения Турбо-режима вентилятора
  void set_turbo_mode(bool enable) {
    if (enable) {
      uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x05, 0x04, 0x00, 0x01, 0x01, 0x15};
      this->write_array(p, 12);
      this->fan_mode = climate::CLIMATE_FAN_HIGH; // Для HA выставим как High
    } else {
      uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x05, 0x04, 0x00, 0x01, 0x00, 0x14};
      this->write_array(p, 12);
      this->fan_mode = climate::CLIMATE_FAN_AUTO;
    }
    this->publish_state();
  }

  // Метод переключения горизонтальных шторок
  void set_h_swing(bool enable) {
    if (enable) {
      uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x6B, 0x01, 0x00, 0x01, 0x01, 0x78};
      this->write_array(p, 12);
    } else {
      uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x6B, 0x01, 0x00, 0x01, 0x00, 0x77};
      this->write_array(p, 12);
    }
  }

  // Метод переключения дисплея
  void set_display(bool enable) {
    if (enable) {
      uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x68, 0x01, 0x00, 0x01, 0x01, 0x75};
      this->write_array(p, 12);
    } else {
      uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x68, 0x01, 0x00, 0x01, 0x00, 0x74};
      this->write_array(p, 12);
    }
  }

 protected:
  std::vector<uint8_t> rx_buf_;
  uint32_t last_rx_time_{0};
  uint32_t last_ping_time_{0};

  climate::ClimateTraits traits() override {
    auto traits = climate::ClimateTraits();
    traits.set_visual_min_temperature(16.0);
    traits.set_visual_max_temperature(30.0);
    traits.set_visual_temperature_step(1.0);
    
    traits.set_supported_modes({
      climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_COOL,
      climate::CLIMATE_MODE_HEAT, climate::CLIMATE_MODE_AUTO,
      climate::CLIMATE_MODE_DRY, climate::CLIMATE_MODE_FAN_ONLY
    });
    
    traits.set_supported_fan_modes({
      climate::CLIMATE_FAN_AUTO, climate::CLIMATE_FAN_LOW,
      climate::CLIMATE_FAN_MIDDLE, climate::CLIMATE_FAN_HIGH
    });
    
    traits.set_supported_swing_modes({
      climate::CLIMATE_SWING_OFF, climate::CLIMATE_SWING_VERTICAL
    });
    
    return traits;
  }

  void control(const climate::ClimateCall &call) override {
    if (call.get_mode().has_value()) {
      auto new_mode = *call.get_mode();
      this->mode = new_mode;
      if (new_mode == climate::CLIMATE_MODE_OFF) {
        uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x01, 0x01, 0x00, 0x01, 0x00, 0x0D};
        this->write_array(p, 12);
      }
      else if (new_mode == climate::CLIMATE_MODE_COOL) { uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x04, 0x04, 0x00, 0x01, 0x00, 0x13}; this->write_array(p, 12); }
      else if (new_mode == climate::CLIMATE_MODE_HEAT) { uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x04, 0x04, 0x00, 0x01, 0x01, 0x14}; this->write_array(p, 12); }
      else if (new_mode == climate::CLIMATE_MODE_AUTO) { uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x04, 0x04, 0x00, 0x01, 0x04, 0x17}; this->write_array(p, 12); }
      else if (new_mode == climate::CLIMATE_MODE_DRY)  { uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x15}; this->write_array(p, 12); }
      else if (new_mode == climate::CLIMATE_MODE_FAN_ONLY) { uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x04, 0x04, 0x00, 0x01, 0x03, 0x16}; this->write_array(p, 12); }
    }

    if (call.get_fan_mode().has_value() && this->mode != climate::CLIMATE_MODE_OFF) {
      this->fan_mode = *call.get_fan_mode();
      if (this->fan_mode == climate::CLIMATE_FAN_AUTO) { uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x05, 0x04, 0x00, 0x01, 0x00, 0x14}; this->write_array(p, 12); }
      else if (this->fan_mode == climate::CLIMATE_FAN_LOW) { uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x05, 0x04, 0x00, 0x01, 0x02, 0x16}; this->write_array(p, 12); }
      else if (this->fan_mode == climate::CLIMATE_FAN_MIDDLE) { uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x05, 0x04, 0x00, 0x01, 0x03, 0x17}; this->write_array(p, 12); }
      else if (this->fan_mode == climate::CLIMATE_FAN_HIGH) { uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x05, 0x04, 0x00, 0x01, 0x04, 0x18}; this->write_array(p, 12); }
    }

    if (call.get_swing_mode().has_value() && this->mode != climate::CLIMATE_MODE_OFF) {
      this->swing_mode = *call.get_swing_mode();
      if (this->swing_mode == climate::CLIMATE_SWING_VERTICAL) { uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x6A, 0x01, 0x00, 0x01, 0x01, 0x77}; this->write_array(p, 12); }
      else { uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x6A, 0x01, 0x00, 0x01, 0x00, 0x76}; this->write_array(p, 12); }
    }

    if (call.get_target_temperature().has_value() && this->mode != climate::CLIMATE_MODE_OFF) {
      this->target_temperature = *call.get_target_temperature();
      int target = (int)this->target_temperature;
      uint8_t packet[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x08, 0x02, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00};
      packet[13] = (uint8_t)target;
      uint8_t crc = 0; for (int i = 2; i < 14; i++) crc += packet[i];
      packet[14] = crc - 1;
      this->write_array(packet, 15);
    }
    this->publish_state();
  }

  void parse_packet_() {
    if (this->rx_buf_.size() == 12 && this->rx_buf_[0] == 0x55 && this->rx_buf_[1] == 0xAA && this->rx_buf_[2] == 0x03) {
      uint8_t reg = this->rx_buf_[6];       
      uint8_t status = this->rx_buf_[10];   

      if (reg == 0x01) {
        if (status == 0x00) this->mode = climate::CLIMATE_MODE_OFF;
        if (this->display_switch != nullptr) this->display_switch->publish_state(status == 0x01);
      }
      else if (reg == 0x6A) {
        this->swing_mode = (status == 0x01) ? climate::CLIMATE_SWING_VERTICAL : climate::CLIMATE_SWING_OFF;
      }
      else if (reg == 0x6B && this->h_swing_switch != nullptr) {
        this->h_swing_switch->publish_state(status == 0x01);
      }
      else if (reg == 0x68 && this->display_switch != nullptr) {
        this->display_switch->publish_state(status == 0x01);
      }
      else if (reg == 0x04) {
        if (status == 0x00) this->mode = climate::CLIMATE_MODE_COOL;
        else if (status == 0x01) this->mode = climate::CLIMATE_MODE_HEAT;
        else if (status == 0x04) this->mode = climate::CLIMATE_MODE_AUTO;
        else if (status == 0x02) this->mode = climate::CLIMATE_MODE_DRY;
        else if (status == 0x03) this->mode = climate::CLIMATE_MODE_FAN_ONLY;
      }
      else if (reg == 0x05) {
        if (status == 0x00) this->fan_mode = climate::CLIMATE_FAN_AUTO;
        else if (status == 0x02) this->fan_mode = climate::CLIMATE_FAN_LOW;
        else if (status == 0x03) this->fan_mode = climate::CLIMATE_FAN_MIDDLE;
        else if (status == 0x04) this->fan_mode = climate::CLIMATE_FAN_HIGH;
        else if (status == 0x01 && this->turbo_select != nullptr) this->turbo_select->publish_state("Turbo");
      }
      this->publish_state();
    }

    if (this->rx_buf_.size() == 15 && this->rx_buf_[0] == 0x55 && this->rx_buf_[1] == 0xAA && this->rx_buf_[2] == 0x03 && this->rx_buf_[5] == 0x08) {
      uint8_t room_temp = this->rx_buf_[13]; 
      if (room_temp >= 10 && room_temp <= 40) {
        this->current_temperature = (float)room_temp;
        this->publish_state();
      }
    }
  }
};

}  // namespace neoline_mbo_ac
}  // namespace esphome
