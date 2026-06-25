# The portable UI lives in ../../emulator/src (core/ + apps/); we compile it
# straight from there so the emulator and device share one source of truth.
file(GLOB CORE_SRCS ${CMAKE_CURRENT_LIST_DIR}/../../emulator/src/core/*.cpp)
file(GLOB APP_SRCS  ${CMAKE_CURRENT_LIST_DIR}/../../emulator/src/apps/*.cpp)

idf_component_register(
    SRCS "main.cpp" ${CORE_SRCS} ${APP_SRCS}
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../emulator/src"
    REQUIRES driver esp_timer
)

# The shared core/apps printf-format node IDs with %x; uint32_t is `unsigned` on
# the host but `long unsigned` on xtensa, so IDF's -Werror=format trips. Harmless
# (both 32-bit) — relax these to warnings here rather than diverge the source.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-error=format -Wno-error=format-truncation -Wno-error=format-nonliteral)
