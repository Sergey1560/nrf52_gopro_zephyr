#
# Copyright (c) 2018 Nordic Semiconductor
#
# SPDX-License-Identifier: LicenseRef-Nordic-5-Clause
#
cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gopro_remote)

# NORDIC SDK APP START
target_sources(app PRIVATE
  src/main.c
  src/gopro_client.c
)
target_include_directories(app PRIVATE
src
# Add user defined include paths
)


# NORDIC SDK APP END
