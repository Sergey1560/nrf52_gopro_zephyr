#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gopro_remote)

list(APPEND CMAKE_MODULE_PATH ${ZEPHYR_BASE}/modules/nanopb)
include(nanopb)

zephyr_nanopb_sources(app src/protobuf/cohn.proto)
zephyr_nanopb_sources(app src/protobuf/live_streaming.proto)
zephyr_nanopb_sources(app src/protobuf/media.proto)
zephyr_nanopb_sources(app src/protobuf/network_management.proto)
zephyr_nanopb_sources(app src/protobuf/preset_status.proto)
zephyr_nanopb_sources(app src/protobuf/request_get_preset_status.proto)
zephyr_nanopb_sources(app src/protobuf/response_generic.proto)
zephyr_nanopb_sources(app src/protobuf/set_camera_control_status.proto)
zephyr_nanopb_sources(app src/protobuf/turbo_transfer.proto)

# NORDIC SDK APP START
target_sources(app PRIVATE
  src/main.c
  src/gopro_client.c
  src/gopro_ble_discovery.c
  src/gopro_protobuf.c
  src/gopro_packet.c
  src/canbus.c
  src/buttons.c
  src/leds.c
)
target_include_directories(app PRIVATE
src
# Add user defined include paths
)


# NORDIC SDK APP END
