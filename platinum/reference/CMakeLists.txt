cmake_minimum_required(VERSION 3.10)
project(PlatinumReference C)
add_application(PlatinumReference TYPE APPL CREATOR CMpr FILES reference.c reference.r)
target_link_libraries(PlatinumReference AppearanceLib IconServicesLib)
