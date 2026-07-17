# Boombox — ESP-IDF project entry point (standard template).
# The ESP-IDF version is pinned in .espidf-version; use scripts/build.sh so the
# pinned environment is sourced automatically.
cmake_minimum_required(VERSION 3.16)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(boombox)
