file(GLOB_RECURSE GXEPD2_SRCS
    "src/*.cpp"
)

idf_component_register(
    SRCS ${GXEPD2_SRCS}
    INCLUDE_DIRS "src"
    REQUIRES arduino-esp32 Adafruit-GFX
)
