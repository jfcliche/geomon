import esphome.codegen as cg
# Aliased because importing the sibling sensor.py platform rebinds `sensor` on this package.
from esphome.components import sensor as sensor_, uart
import esphome.config_validation as cv
from esphome.const import (
    CONF_ID,
    CONF_UART_ID,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_TOTAL_INCREASING,
)
from esphome import pins

CODEOWNERS = ["@jfcliche"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["sensor"]
MULTI_CONF = True

CONF_MODBUS_SNIFFER_ID = "modbus_sniffer_id"
CONF_LOG_FRAMES = "log_frames"
CONF_IDLE_TIMEOUT = "idle_timeout"
CONF_FRAMES = "frames"
CONF_DISCARDED_BYTES = "discarded_bytes"
CONF_UNMATCHED_RESPONSES = "unmatched_responses"
CONF_EXCEPTIONS = "exceptions"
CONF_FLOW_CONTROL_PIN = "flow_control_pin"
CONF_ACTIVE_DELAY = "active_delay"
CONF_ACTIVE_WINDOW = "active_window"


def _optional_txen_pin(value):
    """0 (or none) means the driver-enable pin is not connected."""
    if value in (0, "0", "none", "None", "NONE"):
        return 0
    return pins.internal_gpio_output_pin_schema(value)


modbus_sniffer_ns = cg.esphome_ns.namespace("modbus_sniffer")
ModbusSniffer = modbus_sniffer_ns.class_(
    "ModbusSniffer", cg.PollingComponent, uart.UARTDevice
)

# config key -> (C++ setter, icon)
COUNTERS = {
    CONF_FRAMES: ("set_frames_sensor", "mdi:swap-horizontal"),
    CONF_DISCARDED_BYTES: ("set_discarded_bytes_sensor", "mdi:delete-sweep"),
    CONF_UNMATCHED_RESPONSES: ("set_unmatched_responses_sensor", "mdi:help-circle"),
    CONF_EXCEPTIONS: ("set_exceptions_sensor", "mdi:alert-circle"),
}


def _counter_schema(icon):
    return sensor_.sensor_schema(
        icon=icon,
        accuracy_decimals=0,
        state_class=STATE_CLASS_TOTAL_INCREASING,
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    )


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(ModbusSniffer),
            cv.Optional(CONF_FLOW_CONTROL_PIN): _optional_txen_pin,
            cv.Optional(CONF_ACTIVE_DELAY, default="5s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_ACTIVE_WINDOW, default="30s"): cv.positive_time_period_milliseconds,
            cv.Optional(CONF_LOG_FRAMES, default=False): cv.boolean,
            cv.Optional(
                CONF_IDLE_TIMEOUT, default="50ms"
            ): cv.positive_time_period_milliseconds,
            **{
                cv.Optional(key): _counter_schema(icon)
                for key, (_, icon) in COUNTERS.items()
            },
        }
    )
    .extend(cv.polling_component_schema("30s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    # A real pin selects ESP-IDF RS485 half-duplex: DE rises only while bytes
    # are shifted out. 0 leaves the UART in normal mode, so the driver stays off.
    txen = config.get(CONF_FLOW_CONTROL_PIN)
    if txen not in (None, 0):
        uart_var = await cg.get_variable(config[CONF_UART_ID])
        pin = await cg.gpio_pin_expression(txen)
        cg.add(uart_var.set_flow_control_pin(pin))
        cg.add(var.set_driver_enable(True))

    cg.add(var.set_log_frames(config[CONF_LOG_FRAMES]))
    cg.add(var.set_idle_timeout(config[CONF_IDLE_TIMEOUT]))
    cg.add(var.set_active_delay(config[CONF_ACTIVE_DELAY]))
    cg.add(var.set_active_window(config[CONF_ACTIVE_WINDOW]))

    for key, (setter, _) in COUNTERS.items():
        if key in config:
            sens = await sensor_.new_sensor(config[key])
            cg.add(getattr(var, setter)(sens))
