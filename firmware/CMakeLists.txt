# Cardputer Deck — device firmware (the "brain").
# ESP-IDF project. Reuses the portable core/apps from ../emulator/src directly
# (single source of truth) and adds device seam implementations under main/.
cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(cardputer_deck)
