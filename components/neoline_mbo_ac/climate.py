import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate, uart, switch, select
from esphome.const import CONF_ID

DEPENDENCIES = ["uart"]

neoline_mbo_ac_ns = cg.esphome_ns.namespace("neoline_mbo_ac")
NeolineMBOACClimate = neoline_mbo_ac_ns.class_(
    "NeolineMBOACClimate", climate.Climate, cg.Component, uart.UARTDevice
)

# Пропишем прокси-классы для вывода тумблеров в yaml
NeolineMBOACSwitch = neoline_mbo_ac_ns.class_("NeolineMBOACSwitch", switch.Switch)

CONFIG_SCHEMA = climate.climate_schema(NeolineMBOACClimate).extend(
    uart.UART_DEVICE_SCHEMA
)

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await climate.register_climate(var, config)
    await uart.register_uart_device(var, config)
