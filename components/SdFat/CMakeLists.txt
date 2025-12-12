file(GLOB_RECURSE SDFAT_SRCS
    "src/*.cpp"
)

idf_component_register(
    SRCS ${SDFAT_SRCS}
    INCLUDE_DIRS "src"
    REQUIRES arduino-esp32
)
