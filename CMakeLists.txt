# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
find_package(Python REQUIRED COMPONENTS Interpreter)

project(http_server)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON CACHE BOOL "Generate compile_commands.json" FORCE)

# Get git commit SHA for build info
find_program(GIT_EXECUTABLE git)

if(GIT_EXECUTABLE)
  execute_process(
    COMMAND ${GIT_EXECUTABLE} rev-parse --short HEAD
    WORKING_DIRECTORY ${CMAKE_SOURCE_DIR}
    OUTPUT_VARIABLE GIT_COMMIT_SHA
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
  )
endif()

# Fallback if git is not available or not in a git repo
if(NOT GIT_COMMIT_SHA)
  set(GIT_COMMIT_SHA "unknown" CACHE STRING "Git commit SHA for build info" FORCE)
endif()

# Add git commit SHA as a compile definition
target_compile_definitions(app PRIVATE OPENJBOD_GIT_COMMIT="${GIT_COMMIT_SHA}")

message(STATUS "OpenJBOD Git commit SHA: ${GIT_COMMIT_SHA}")


option(INCLUDE_HTML_CONTENT "Include the HTML content" ON)

target_sources(app PRIVATE
  src/main.c
  src/eeprom_mac.c
  src/sr_latch.c
  src/status_led.c
  src/temperature.c
  src/emc2301.c
  src/fan_control.c
  src/settings.c
  src/device_info.c
  src/http/auth.c
  src/http/session.c
  src/http/api_token.c
  src/http/headers.c
  src/http/server_control.c
  src/http/static_assets.c
  src/http/router.c
  src/http/routes/routes_core.c
  src/http/routes/routes_power.c
  src/http/routes/routes_environment.c
  src/http/routes/routes_settings.c
  src/http/routes/routes_users.c
  src/http/routes/routes_certificates.c
  src/http/routes/routes_reset.c
  src/http/routes/routes_auth.c
  src/http/routes/routes_tokens.c
)

# Network firmware upgrade (DFU) - only when built under MCUboot (sysbuild).
target_sources_ifdef(CONFIG_BOOTLOADER_MCUBOOT app PRIVATE src/firmware_update.c)

# Development/debug UART shell commands (only useful when the shell is enabled).
target_sources_ifdef(CONFIG_SHELL app PRIVATE src/shell_cmds.c)

# USB CDC-ACM console: our bring-up code plus the canonical USBD init helper from
# the Zephyr tree (its header explicitly invites reuse as a template).
target_sources_ifdef(CONFIG_OPENJBOD_USB_CONSOLE app PRIVATE
  src/usb_console.c
  ${ZEPHYR_BASE}/samples/subsys/usb/common/sample_usbd_init.c
)
if(CONFIG_OPENJBOD_USB_CONSOLE)
  target_include_directories(app PRIVATE ${ZEPHYR_BASE}/samples/subsys/usb/common)
endif()

target_include_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)

set(gen_dir ${ZEPHYR_BINARY_DIR}/include/generated/)

target_link_libraries(app PRIVATE zephyr_interface zephyr)

zephyr_linker_sources(SECTIONS sections-rom.ld)
zephyr_linker_section_ifdef(CONFIG_NET_SAMPLE_HTTPS_SERVICE NAME
				http_resource_desc_test_https_service
				KVMA RAM_REGION GROUP RODATA_REGION)
zephyr_linker_section_ifdef(CONFIG_NET_SAMPLE_HTTP_SERVICE NAME
				http_resource_desc_test_http_service
				KVMA RAM_REGION GROUP RODATA_REGION)

set(WEBSRC ${CMAKE_CURRENT_SOURCE_DIR}/src/static_web_resources)
set(WEB_INLINE_DIR ${CMAKE_BINARY_DIR}/web_inlined)

# Every page now inlines its CSS/JS at build time (below), so the standalone asset
# routes were removed - no separate <name>.js.gz.inc / style.css.gz.inc is embedded.
# The source files are still read directly by the inliner.

# HTML pages: inline /style.css, the page's own JS, and common.js so each page is a
# SINGLE HTTPS request (one TLS handshake) instead of 3-4. Over HTTPS the handshake
# is the whole cost and handshakes serialise on the one crypto core, so collapsing a
# page to one request is the biggest load-time win. The inlined page is then gzipped
# into the same <page>.html.gz.inc that static_assets.c already embeds.
file(GLOB WEB_ASSET_DEPS ${WEBSRC}/*.js ${WEBSRC}/*.css)
foreach(page
  index
  about
  network
  power
  http
  environment
  console
  users
  tokens
  firmware
  reset
  login
    )
  add_custom_command(
    OUTPUT ${WEB_INLINE_DIR}/${page}.html
    COMMAND ${CMAKE_COMMAND} -E make_directory ${WEB_INLINE_DIR}
    COMMAND ${Python_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/scripts/inline_web_assets.py
            ${WEBSRC}/${page}.html ${WEBSRC} ${WEB_INLINE_DIR}/${page}.html
    DEPENDS ${WEBSRC}/${page}.html ${WEB_ASSET_DEPS}
            ${CMAKE_CURRENT_SOURCE_DIR}/scripts/inline_web_assets.py
    COMMENT "Inlining web assets into ${page}.html"
    VERBATIM
  )
  generate_inc_file_for_target(
    app
    ${WEB_INLINE_DIR}/${page}.html
    ${gen_dir}/${page}.html.gz.inc
    --gzip
  )
endforeach()

set(cert_source_dir ${CMAKE_CURRENT_SOURCE_DIR}/src/certs)
set(cert_build_dir ${CMAKE_BINARY_DIR}/certs)
set(cert_outputs
  ${cert_build_dir}/server_cert.der
  ${cert_build_dir}/server_privkey.der
)

add_custom_target(embedded_certs_force ALL
  COMMAND ${CMAKE_COMMAND} -E make_directory ${cert_build_dir}
  COMMAND ${CMAKE_COMMAND} -E rm -f ${cert_build_dir}/server_cert.der
  COMMAND ${CMAKE_COMMAND} -E rm -f ${cert_build_dir}/server_privkey.der
  COMMAND ${CMAKE_COMMAND} -E rm -f ${cert_build_dir}/server_cert.pem
  COMMAND ${CMAKE_COMMAND} -E rm -f ${cert_build_dir}/server_privkey.pem
  COMMAND ${CMAKE_COMMAND} -E rm -f ${cert_build_dir}/server_cert.ext
  COMMENT "Forcing TLS certificate regeneration"
  VERBATIM
)

add_custom_command(
  OUTPUT ${cert_outputs}
  COMMAND ${CMAKE_COMMAND} -E make_directory ${cert_build_dir}
  COMMAND ${cert_source_dir}/gen_cert.sh
  WORKING_DIRECTORY ${cert_build_dir}
  DEPENDS ${cert_source_dir}/gen_cert.sh embedded_certs_force
  COMMENT "Generating embedded TLS certificates"
  VERBATIM
)

add_custom_target(embedded_certs ALL DEPENDS ${cert_outputs})
add_dependencies(embedded_certs embedded_certs_force)

foreach(inc_file
  server_cert.der
  server_privkey.der
    )
  generate_inc_file_for_target(
    app
    ${cert_build_dir}/${inc_file}
    ${gen_dir}/${inc_file}.inc
    )
endforeach()

add_dependencies(app embedded_certs)
