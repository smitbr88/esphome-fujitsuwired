import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import climate
from esphome.components.logger import HARDWARE_UART_TO_SERIAL
from esphome.const import (
    CONF_ID,
    CONF_UPDATE_INTERVAL,
    CONF_MODE,
    CONF_FAN_MODE,
)
from esphome.core import CORE, coroutine

AUTO_LOAD = ["climate"]

CONF_SUPPORTS = "supports"
DEFAULT_CLIMATE_MODES = ['HEAT_COOL', 'COOL', 'HEAT', 'DRY', 'FAN_ONLY']
DEFAULT_FAN_MODES = ['AUTO', 'DIFFUSE', 'LOW', 'MEDIUM', 'HIGH']

FujitsuHeatPump = cg.global_ns.class_("FujitsuHeatPump", climate.Climate, cg.PollingComponent)


CONFIG_SCHEMA = climate.CLIMATE_SCHEMA.extend(
    {
        cv.GenerateID(): cv.declare_id(FujitsuHeatPump),

        # If polling interval is greater than 9 seconds, the HeatPump library
        # reconnects, but doesn't then follow up with our data request.
        cv.Optional(CONF_UPDATE_INTERVAL, default="500ms"): cv.All(
            cv.update_interval,
            cv.Range(max=cv.TimePeriod(milliseconds=9000))
        ),

        # Optionally override the supported ClimateTraits.
        cv.Optional(CONF_SUPPORTS, default={}): cv.Schema(
            {
                cv.Optional(CONF_MODE, default=DEFAULT_CLIMATE_MODES):
                    cv.ensure_list(climate.validate_climate_mode),
                cv.Optional(CONF_FAN_MODE, default=DEFAULT_FAN_MODES):
                    cv.ensure_list(climate.validate_climate_fan_mode),
            }
        ),
    }
).extend(cv.COMPONENT_SCHEMA)


@coroutine
def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID], cg.RawExpression(f'&{serial}'))

    supports = config[CONF_SUPPORTS]
    traits = var.config_traits()

    for mode in supports[CONF_MODE]:
        if mode == 'OFF':
            continue
        cg.add(traits.add_supported_mode(climate.CLIMATE_MODES[mode]))

    for mode in supports[CONF_FAN_MODE]:
        cg.add(traits.add_supported_fan_mode(climate.CLIMATE_FAN_MODES[mode]))

    yield cg.register_component(var, config)
    yield climate.register_climate(var, config)
    cg.add_library("https://github.com/unreality/FujiHeatPump", None)