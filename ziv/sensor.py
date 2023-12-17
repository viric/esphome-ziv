import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor, uart
from esphome.const import (
    CONF_ID,
    CONF_IMPORT_ACTIVE_ENERGY,
    CONF_EXPORT_ACTIVE_ENERGY,
    CONF_UPDATE_INTERVAL,
    DEVICE_CLASS_ENERGY,
    STATE_CLASS_TOTAL_INCREASING,
    UNIT_WATT_HOURS,
)
from esphome.core import TimePeriodMilliseconds

CODEOWNERS = ["@viric"]
DEPENDENCIES = ["uart"]

ziv_ns = cg.esphome_ns.namespace("ziv")
ZivComponent = ziv_ns.class_(
    "ZivComponent", uart.UARTDevice, cg.PollingComponent
)


CONFIG_SCHEMA = cv.All(
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ZivComponent),
            cv.Optional(CONF_IMPORT_ACTIVE_ENERGY): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT_HOURS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_ENERGY,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
            cv.Optional(CONF_EXPORT_ACTIVE_ENERGY): sensor.sensor_schema(
                unit_of_measurement=UNIT_WATT_HOURS,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_ENERGY,
                state_class=STATE_CLASS_TOTAL_INCREASING,
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
    .extend(cv.polling_component_schema("never")),
)


def validate_interval_uart(config):
    uart.final_validate_device_schema(
        "ziv", baud_rate=9600, require_rx=True, require_tx=True
    )(config)


FINAL_VALIDATE_SCHEMA = validate_interval_uart

async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if CONF_IMPORT_ACTIVE_ENERGY in config:
        sens = await sensor.new_sensor(config[CONF_IMPORT_ACTIVE_ENERGY])
        cg.add(var.set_import_sensor(sens))

    if CONF_EXPORT_ACTIVE_ENERGY in config:
        sens = await sensor.new_sensor(config[CONF_EXPORT_ACTIVE_ENERGY])
        cg.add(var.set_export_sensor(sens))
