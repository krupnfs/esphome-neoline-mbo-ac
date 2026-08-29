#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"

namespace esphome {
namespace neoline_mbo_ac {

class NeolineMBOACClimate : public climate::Climate, public Component, public uart::UARTDevice {
 public:
  void setup() override {
    this->target_temperature = 22.0;
    this->mode = climate::CLIMATE_MODE_COOL;
    this->fan_mode = climate::CLIMATE_FAN_MODE_AUTO;
    this->swing_mode = climate::CLIMATE_SWING_OFF;
  }

  void loop() override {
    // Чтение байт из UART в буфер
    while (this->available() > 0) {
      uint8_t b;
      this->read_byte(&b);
      this->rx_buf_.push_back(b);
      this->last_rx_time_ = millis();
    }

    // Сборка пакета по таймауту тишины в 20 мс
    if (!this->rx_buf_.empty() && (millis() - this->last_rx_time_ > 20)) {
      this->parse_packet_();
      this->rx_buf_.clear();
    }

    // Автоматическая отправка пинга каждые 1500 мс [0x0.1.1]
    if (millis() - this->last_ping_time_ > 1500) {
      uint8_t ping_packet[] = {0x55, 0xAA, 0x00, 0x08, 0x00, 0x00, 0x07};
      this->write_array(ping_packet, 7);
      this->last_ping_time_ = millis();
    }
  }

 protected:
  std::vector<uint8_t> rx_buf_;
  uint32_t last_rx_time_{0};
  uint32_t last_ping_time_{0};

  // Настройки возможностей пульта для Home Assistant
  climate::ClimateTraits traits() override {
    auto traits = climate::ClimateTraits();
    traits.set_supports_current_temperature(true);
    traits.set_supports_target_temperature(true);
    traits.set_visual_min_temperature(16.0);
    traits.set_visual_max_temperature(30.0);
    traits.set_visual_temperature_step(1.0);
    
    traits.set_supported_modes({
      climate::CLIMATE_MODE_OFF,
      climate::CLIMATE_MODE_COOL,
      climate::CLIMATE_MODE_HEAT,
      climate::CLIMATE_MODE_AUTO
    });
    
    traits.set_supported_fan_modes({
      climate::CLIMATE_FAN_MODE_AUTO,
      climate::CLIMATE_FAN_MODE_LOW,
      climate::CLIMATE_FAN_MODE_MIDDLE,
      climate::CLIMATE_FAN_MODE_HIGH
    });
    
    traits.set_supported_swing_modes({
      climate::CLIMATE_SWING_OFF,
      climate::CLIMATE_SWING_VERTICAL
    });
    
    return traits;
  }

  // Перехват кликов пользователя из интерфейса Home Assistant
  void control(const climate::ClimateCall &call) override {
    if (call.get_mode().has_value()) {
      this->mode = *call.get_mode();
    }
    if (call.get_target_temperature().has_value()) {
      this->target_temperature = *call.get_target_temperature();
    }
    if (call.get_fan_mode().has_value()) {
      this->fan_mode = *call.get_fan_mode();
    }
    if (call.get_swing_mode().has_value()) {
      this->swing_mode = *call.get_swing_mode();
    }

    this->send_state_to_ac_();
    this->publish_state();
  }

  // Сборка 15-байтового пакета TX для отправки в кондиционер [0x0.1.4]
  void send_state_to_ac_() {
    if (this->mode == climate::CLIMATE_MODE_OFF) {
      uint8_t off_packet[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x01, 0x01, 0x00, 0x01, 0x00, 0x0D};
      this->write_array(off_packet, 12);
      return;
    }

    // Если режим AUTO — шлем вашу эталонную 12-байтовую сервисную команду [0x0.1.4, 0x0.1.6]
    if (this->mode == climate::CLIMATE_MODE_AUTO) {
      uint8_t auto_packet[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x04, 0x04, 0x00, 0x01, 0x04, 0x17};
      this->write_array(auto_packet, 12);
      return;
    }

    // Для ручных режимов собираем честный 15-байтовый параметрический пакет [0x0.1.4]
    uint8_t packet[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x08, 0x02, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00};
    
    // Записываем градусы [0x0.1.4]
    packet[13] = (uint8_t)this->target_temperature;

    // Режимы (2 - Cool, 4 - Heat по вашей таблице) [0x0.1.6]
    if (this->mode == climate::CLIMATE_MODE_COOL) packet[6] = 0x02;
    if (this->mode == climate::CLIMATE_MODE_HEAT) packet[6] = 0x04;

    // Скорость вентилятора [0x0.1.6]
    if (this->fan_mode == climate::CLIMATE_FAN_MODE_LOW) packet[7] = 0x02;
    if (this->fan_mode == climate::CLIMATE_FAN_MODE_AUTO) packet[7] = 0x00;
    if (this->fan_mode == climate::CLIMATE_FAN_MODE_MIDDLE) packet[7] = 0x03;
    if (this->fan_mode == climate::CLIMATE_FAN_MODE_HIGH) packet[7] = 0x04;

    // Вертикальный свинг
    if (this->swing_mode == climate::CLIMATE_SWING_VERTICAL) {
      // Для шторок по вашей логике лучше отправить отдельный импульс, 
      // но в рамках Climate-модели взведем флаг в общем пакете, если плата это переваривает.
      // На всякий случай оставляем базовую маску MBO.
    }

    // Расчет CRC с поправкой -1 [0x0.1.4]
    uint8_t crc = 0;
    for (int i = 2; i < 14; i++) crc += packet[i];
    packet[14] = crc - 1;

    this->write_array(packet, 15);
  }

  // Разбор ответов шины RX [0x0.1.7]
  void parse_packet_() {
    // 1. Стандартные 12-байтовые ответы [0x0.1.7]
    if (this->rx_buf_.size() == 12 && this->rx_buf_[0] == 0x55 && this->rx_buf_[1] == 0xAA && this->rx_buf_[2] == 0x03) {
      uint8_t reg = this->rx_buf_[6];
      uint8_t status = this->rx_buf_[10];

      if (reg == 0x01) {
        if (status == 0x00) this->mode = climate::CLIMATE_MODE_OFF;
      }
      else if (reg == 0x6A) {
        this->swing_mode = (status == 0x01) ? climate::CLIMATE_SWING_VERTICAL : climate::CLIMATE_SWING_OFF;
      }
      else if (reg == 0x04) {
        if (status == 0x00) this->mode = climate::CLIMATE_MODE_COOL;
        if (status == 0x01) this->mode = climate::CLIMATE_MODE_HEAT;
        if (status == 0x04) this->mode = climate::CLIMATE_MODE_AUTO;
      }
      else if (reg == 0x05) {
        if (status == 0x00) this->fan_mode = climate::CLIMATE_FAN_MODE_AUTO;
        if (status == 0x02) this->fan_mode = climate::CLIMATE_FAN_MODE_LOW;
        if (status == 0x03) this->fan_mode = climate::CLIMATE_FAN_MODE_MIDDLE;
        if (status == 0x04) this->fan_mode = climate::CLIMATE_FAN_MODE_HIGH;
      }
      this->publish_state();
    }

    // 2. 15-байтовый пакет датчика комнаты [0x0.1.7]
    if (this->rx_buf_.size() == 15 && this->rx_buf_[0] == 0x55 && this->rx_buf_[1] == 0xAA && this->rx_buf_[2] == 0x03 && this->rx_buf_[5] == 0x08) {
      uint8_t room_temp = this->rx_buf_[13]; // 14-й байт — чистый HEX воздуха в комнате [0x0.1.7]
      if (room_temp >= 10 && room_temp <= 40) {
        this->current_temperature = (float)room_temp;
        this->publish_state();
      }
    }
  }
};

}  // namespace neoline_mbo_ac
}  // namespace esphome
