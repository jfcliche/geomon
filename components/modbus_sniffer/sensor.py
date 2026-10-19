import esphome.codegen as cg
from esphome.components import sensor
import esphome.config_validation as cv
from esphome.const import CONF_ADDRESS

from . import CONF_MODBUS_SNIFFER_ID, ModbusSniffer, modbus_sniffer_ns

DEPENDENCIES = ["modbus_sniffer"]

CONF_SLAVE = "slave"
CONF_REGISTER_TYPE = "register_type"
CONF_VALUE_TYPE = "value_type"
CONF_ACTIVE = "active"

RegisterSensor = modbus_sniffer_ns.class_("RegisterSensor", sensor.Sensor)

RegisterType = modbus_sniffer_ns.enum("RegisterType", is_class=True)
REGISTER_TYPES = {
    "holding": RegisterType.HOLDING,
    "input": RegisterType.INPUT,
}

ValueType = modbus_sniffer_ns.enum("ValueType", is_class=True)
VALUE_TYPES = {
    "U16": ValueType.U16,
    "S16": ValueType.S16,
    "U32": ValueType.U32,
    "S32": ValueType.S32,
    "U32_R": ValueType.U32_R,
    "S32_R": ValueType.S32_R,
    "FLOAT32": ValueType.FLOAT32,
    "FLOAT32_R": ValueType.FLOAT32_R,
}

CONFIG_SCHEMA = sensor.sensor_schema(RegisterSensor).extend(
    {
        cv.GenerateID(CONF_MODBUS_SNIFFER_ID): cv.use_id(ModbusSniffer),
        cv.Required(CONF_SLAVE): cv.int_range(min=1, max=247),
        cv.Optional(CONF_REGISTER_TYPE, default="holding"): cv.enum(
            REGISTER_TYPES, lower=True
        ),
        cv.Required(CONF_ADDRESS): cv.int_range(min=0, max=0xFFFF),
        cv.Optional(CONF_VALUE_TYPE, default="U16"): cv.enum(VALUE_TYPES, upper=True),
        # The other master already publishes sensors without this. Active sensors
        # are read in the quiet gap after that master stops.
        cv.Optional(CONF_ACTIVE, default=False): cv.boolean,
    }
)


async def to_code(config):
    var = await sensor.new_sensor(config)
    cg.add(var.set_slave(config[CONF_SLAVE]))
    cg.add(var.set_register_type(config[CONF_REGISTER_TYPE]))
    cg.add(var.set_address(config[CONF_ADDRESS]))
    cg.add(var.set_value_type(config[CONF_VALUE_TYPE]))
    cg.add(var.set_active(config[CONF_ACTIVE]))

    parent = await cg.get_variable(config[CONF_MODBUS_SNIFFER_ID])
    cg.add(parent.register_sensor(var))
