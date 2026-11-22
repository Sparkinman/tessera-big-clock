"""Big clock: the ESPHome side. smart_display.register_plugin() reads the id, version, tile memory and the screen's
texts from the manifest and translations/ beside this folder. The fonts named in plugin.yaml are handed to the C++ so
the tile can pick the largest that fits."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import font, smart_display
from esphome.const import CONF_ID

# The core component every Tessera screen has; the plugin API lives in it.
DEPENDENCIES = ["smart_display", "font"]

CONF_FONTS = "fonts"
CONF_DATE_FONT = "date_font"
CONF_SAVER_DATE_FONT = "saver_date_font"

big_clock_ns = cg.esphome_ns.namespace("big_clock")
BigClock = big_clock_ns.class_("BigClock", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(BigClock),
        cv.Optional(CONF_FONTS, default=[]): cv.ensure_list(cv.use_id(font.Font)),
        cv.Optional(CONF_DATE_FONT): cv.use_id(font.Font),
        cv.Optional(CONF_SAVER_DATE_FONT): cv.use_id(font.Font),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    for font_id in config[CONF_FONTS]:
        cg.add(var.add_font(await cg.get_variable(font_id)))
    if CONF_DATE_FONT in config:
        cg.add(var.set_date_font(await cg.get_variable(config[CONF_DATE_FONT])))
    if CONF_SAVER_DATE_FONT in config:
        cg.add(var.set_saver_date_font(await cg.get_variable(config[CONF_SAVER_DATE_FONT])))
    await smart_display.register_plugin(var, __file__)
