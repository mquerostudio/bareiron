# bareiron as an ESP-IDF component. The host firmware (for example
# KodeCraft) provides the network, calls bareiron_run from a task and
# passes its own BareironPlatform.

if(NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/src/registries.c")
  message(FATAL_ERROR
    "bareiron: src/registries.c is missing. Generate it with "
    "./extract_registries.sh (Java 21 + the 1.21.8 server.jar, see README).")
endif()

idf_component_register(
  SRCS
    src/blockindex.c
    src/crafting.c
    src/dispatch.c
    src/globals.c
    src/net.c
    src/packets.c
    src/procedures.c
    src/registries.c
    src/serialize.c
    src/server.c
    src/status.c
    src/structures.c
    src/tools.c
    src/varnum.c
    src/worldgen.c
    third_party/miniz/miniz.c
  INCLUDE_DIRS include third_party/miniz
  REQUIRES lwip esp_timer
)

target_compile_definitions(${COMPONENT_LIB} PRIVATE
  MINIZ_NO_STDIO MINIZ_NO_TIME MINIZ_NO_ARCHIVE_APIS MINIZ_NO_ZLIB_COMPATIBLE_NAMES)

# miniz is third-party code; keep its warnings from failing -Werror builds
set_source_files_properties(third_party/miniz/miniz.c PROPERTIES COMPILE_OPTIONS "-Wno-type-limits")
