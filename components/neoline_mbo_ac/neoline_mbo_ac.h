#pragma once

#include "esphome/core/component.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/switch/switch.h"

namespace esphome {
namespace neoline_mbo_ac {

class NeolineMBOACClimate : public climate::Climate, public Component, public uart::UARTDevice {
 public:
  // Дополнительный контроль дисплея для вывода в HA через YAML указатель
  switch_::Switch *display_switch{nullptr};

  void setup() override {
    this->target_temperature = 22.0;
    this->mode = climate::CLIMATE_MODE_COOL;
    this->fan_mode = climate::CLIMATE_FAN_AUTO;
    this->swing_mode = climate::CLIMATE_SWING_OFF;
    this->current_temperature = NAN;
  }

  void loop() override {
    // Чтение байт из UART в буфер
    while (this->available() > 0) {
      uint8_t b;
      if (this->read_byte(&b)) {
        this->rx_buf_.push_back(b);
      }
    }

    // Алгоритм сдвига буфера для защиты от склеивания пакетов
    while (this->rx_buf_.size() >= 7) {
      if (this->rx_buf_[0] != 0x55 || this->rx_buf_[1] != 0xAA) {
        this->rx_buf_.erase(this->rx_buf_.begin());
        continue;
      }

      uint8_t data_len = this->rx_buf_[5];
      size_t full_packet_size = 6 + 1 + data_len;

      if (this->rx_buf_.size() < full_packet_size) {
        break; // Пакет долетел не полностью, ждем
      }

      std::vector<uint8_t> packet(this->rx_buf_.begin(), this->rx_buf_.begin() + full_packet_size);
      this->parse_packet_(packet);

      this->rx_buf_.erase(this->rx_buf_.begin(), this->rx_buf_.begin() + full_packet_size);
    }

    // Автоматический пинг каждые 1.5 секунды
    if (millis() - this->last_ping_time_ > 1500) {
      uint8_t ping_packet[] = {0x55, 0xAA, 0x00, 0x08, 0x00, 0x00, 0x07};
      this->write_array(ping_packet, 7);
      this->last_ping_time_ = millis();
    }
  }

  // Метод управления дисплеем (вызывается из YAML)
  void set_display(bool enable) {
    uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x68, 0x01, 0x00, 0x01, (uint8_t)(enable ? 0x01 : 0x00), 0x00};
    p[11] = (enable ? 0x75 : 0x74);
    this->write_array(p, 12);
  }

 protected:
  std::vector<uint8_t> rx_buf_;
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
      climate::CLIMATE_FAN_MIDDLE, climate::CLIMATE_FAN_HIGH,
      climate::CLIMATE_FAN_DIFFUSE // Используем DIFFUSE как маркер ТУРБО-режима
    });
    
    traits.set_supported_swing_modes({
      climate::CLIMATE_SWING_OFF, 
      climate::CLIMATE_SWING_VERTICAL,
      climate::CLIMATE_SWING_HORIZONTAL,
      climate::CLIMATE_SWING_BOTH
    });
    
    traits.set_supports_current_temperature(true);
    return traits;
  }

  void control(const climate::ClimateCall &call) override {
    // 1. ПРОВЕРКА ПИТАНИЯ: Если кондиционер выключен, а пользователь меняет режим/температуру/вентилятор
    bool mode_changing_to_on = call.get_mode().has_value() && (*call.get_mode() != climate::CLIMATE_MODE_OFF);
    bool adjustments_while_off = (this->mode == climate::CLIMATE_MODE_OFF) && 
                                 (call.get_target_temperature().has_value() || call.get_fan_mode().has_value() || call.get_swing_mode().has_value());

    if ((this->mode == climate::CLIMATE_MODE_OFF && mode_changing_to_on) || adjustments_while_off) {
      // Отправляем пакет "Включить" строго по логу
      uint8_t power_on[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x01, 0x01, 0x00, 0x01, 0x01, 0x0E};
      this->write_array(power_on, 12);
      this->mode = climate::CLIMATE_MODE_AUTO; // Временный флаг, что уже не OFF
      delay(50); // Пауза для MCU кондиционера перед приемом следующих команд
    }

    // 2. ОБРАБОТКА РЕЖИМОВ
    if (call.get_mode().has_value()) {
      auto new_mode = *call.get_mode();
      this->mode = new_mode;

      if (new_mode == climate::CLIMATE_MODE_OFF) {
        uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x01, 0x01, 0x00, 0x01, 0x00, 0x0D};
        this->write_array(p, 12);
      }
      else if (new_mode == climate::CLIMATE_MODE_COOL)     { uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x04, 0x04, 0x00, 0x01, 0x00, 0x13}; this->write_array(p, 12); }
      else if (new_mode == climate::CLIMATE_MODE_HEAT)     { uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x04, 0x04, 0x00, 0x01, 0x01, 0x14}; this->write_array(p, 12); }
      else if (new_mode == climate::CLIMATE_MODE_AUTO)     { uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x04, 0x04, 0x00, 0x01, 0x04, 0x17}; this->write_array(p, 12); }
      else if (new_mode == climate::CLIMATE_MODE_DRY)      { uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x04, 0x04, 0x00, 0x01, 0x02, 0x15}; this->write_array(p, 12); }
      else if (new_mode == climate::CLIMATE_MODE_FAN_ONLY) { uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x04, 0x04, 0x00, 0x01, 0x03, 0x16}; this->write_array(p, 12); }
    }

    // 3. ОБРАБОТКА ВЕНТИЛЯТОРА
    if (call.get_fan_mode().has_value() && this->mode != climate::CLIMATE_MODE_OFF) {
      this->fan_mode = *call.get_fan_mode();
      if (this->fan_mode == climate::CLIMATE_FAN_AUTO)       { uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x05, 0x04, 0x00, 0x01, 0x00, 0x14}; this->write_array(p, 12); }
      else if (this->fan_mode == climate::CLIMATE_FAN_LOW)   { uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x05, 0x04, 0x00, 0x01, 0x02, 0x16}; this->write_array(p, 12); }
      else if (this->fan_mode == climate::CLIMATE_FAN_MIDDLE){ uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x05, 0x04, 0x00, 0x01, 0x03, 0x17}; this->write_array(p, 12); }
      else if (this->fan_mode == climate::CLIMATE_FAN_HIGH)  { uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x05, 0x04, 0x00, 0x01, 0x04, 0x18}; this->write_array(p, 12); }
      else if (this->fan_mode == climate::CLIMATE_FAN_DIFFUSE){ uint8_t p[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x05, 0x04, 0x00, 0x01, 0x01, 0x15}; this->write_array(p, 12); } // ТУРБО режим
    }

    // 4. ОБРАБОТКА ШТОРЫ (С поддержкой горизонтальных и совмещенных режимов)
    if (call.get_swing_mode().has_value() && this->mode != climate::CLIMATE_MODE_OFF) {
      this->swing_mode = *call.get_swing_mode();
      
      bool v_enable = (this->swing_mode == climate::CLIMATE_SWING_VERTICAL || this->swing_mode == climate::CLIMATE_SWING_BOTH);
      bool h_enable = (this->swing_mode == climate::CLIMATE_SWING_HORIZONTAL || this->swing_mode == climate::CLIMATE_SWING_BOTH);

      // Вертикальные шторки (Регистр 0x6A)
      uint8_t p_v[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x6A, 0x01, 0x00, 0x01, (uint8_t)(v_enable ? 0x01 : 0x00), 0x00};
      p_v[11] = (v_enable ? 0x77 : 0x76);
      this->write_array(p_v, 12);
      delay(20);

      // Горизонтальные шторки (Регистр 0x6B)
      uint8_t p_h[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x05, 0x6B, 0x01, 0x00, 0x01, (uint8_t)(h_enable ? 0x01 : 0x00), 0x00};
      p_h[11] = (h_enable ? 0x78 : 0x77);
      this->write_array(p_h, 12);
    }

    // 5. УСТАНОВКА ТЕМПЕРАТУРЫ (Твоя подтвержденная анализатором CRC математика)
    if (call.get_target_temperature().has_value() && this->mode != climate::CLIMATE_MODE_OFF) {
      this->target_temperature = *call.get_target_temperature();
      int target = (int)this->target_temperature;
      uint8_t packet[] = {0x55, 0xAA, 0x00, 0x06, 0x00, 0x08, 0x02, 0x02, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x00};
      packet[13] = (uint8_t)target;
      
      uint8_t crc = 0; 
      for (int i = 2; i < 14; i++) crc += packet[i];
      packet[14] = crc - 1; // Твой проверенный CRC хак
      
      this->write_array(packet, 15);
    }
    this->publish_state();
  }

  void parse_packet_(const std::vector<uint8_t> &packet) {
    if (packet[0] != 0x55 || packet[1] != 0xAA || packet[2] != 0x03) return;

    // 1. Парсинг 12-байтовых статусных ответов (RX)
    if (packet.size() == 12) {
      uint8_t reg = packet[6];       
      uint8_t status = packet[10];   

      if (reg == 0x01) {
        if (status == 0x00) this->mode = climate::CLIMATE_MODE_OFF;
      }
      else if (reg == 0x6A || reg == 0x6B) {
        // Логика обратной связи для объединенного swing_mode
        bool current_v = (this->swing_mode == climate::CLIMATE_SWING_VERTICAL || this->swing_mode == climate::CLIMATE_SWING_BOTH);
        bool current_h = (this->swing_mode == climate::CLIMATE_SWING_HORIZONTAL || this->swing_mode == climate::CLIMATE_SWING_BOTH);

        if (reg == 0x6A) current_v = (status == 0x01);
        if (reg == 0x6B) current_h = (status == 0x01);

        if (current_v && current_h) this->swing_mode = climate::CLIMATE_SWING_BOTH;
        else if (current_v)         this->swing_mode = climate::CLIMATE_SWING_VERTICAL;
        else if (current_h)         this->swing_mode = climate::CLIMATE_SWING_HORIZONTAL;
        else                        this->swing_mode = climate::CLIMATE_SWING_OFF;
      }
      else if (reg == 0x68 && this->display_switch != nullptr) {
        this->display_switch->publish_state(status == 0x01); // Обратная связь дисплея
      }
      else if (reg == 0x04) {
        if (this->mode != climate::CLIMATE_MODE_OFF) {
          if (status == 0x00)      this->mode = climate::CLIMATE_MODE_COOL;
          else if (status == 0x01) this->mode = climate::CLIMATE_MODE_HEAT;
          else if (status == 0x04) this->mode = climate::CLIMATE_MODE_AUTO;
          else if (status == 0x02) this->mode = climate::CLIMATE_MODE_DRY;
          else if (status == 0x03) this->mode = climate::CLIMATE_MODE_FAN_ONLY;
        }
      }
      else if (reg == 0x05) {
        if (status == 0x00)      this->fan_mode = climate::CLIMATE_FAN_AUTO;
        else if (status == 0x01) this->fan_mode = climate::CLIMATE_FAN_DIFFUSE; // Обратная связь для ТУРБО
        else if (status == 0x02) this->fan_mode = climate::CLIMATE_FAN_LOW;
        else if (status == 0x03) this->fan_mode = climate::CLIMATE_FAN_MIDDLE;
        else if (status == 0x04) this->fan_mode = climate::CLIMATE_FAN_HIGH;
      }
      this->publish_state();
    }

    // 2. Парсинг 15-байтовых длинных пакетов температур (RX)
    if (packet.size() == 15 && packet[5] == 0x08) {
      uint8_t sub_reg = packet[6];  
      uint8_t val = packet[13];     

      if (sub_reg == 0x03 && val >= 10 && val <= 40) {
        this->current_temperature = (float)val;
      }
      else if (sub_reg == 0x02 && val >= 16 && val <= 30) {
        this->target_temperature = (float)val;
      }
      this->publish_state();
    }
  }
};

}  // namespace neoline_mbo_ac
}  // namespace esphome
