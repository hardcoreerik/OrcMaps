# The OrcMaps component is found through EXTRA_COMPONENT_DIRS pointing at the
# repository root, so its component name is the checkout folder name. Derive
# it rather than hardcoding, so this builds from any worktree.
get_filename_component(_orcmap_root "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
get_filename_component(_orcmap_component "${_orcmap_root}" NAME)

idf_component_register(
    SRCS "main.cpp"
    INCLUDE_DIRS "."
    REQUIRES ${_orcmap_component} hardware-benchmark lovyangfx fatfs sdmmc
             esp_driver_spi esp_driver_gpio vfs esp_timer
)

# Measurement-only build option: run the zoom/pan sweep once at boot instead
# of only the fixed scenarios, so a sweep can be captured over serial.
#   idf.py -DORCMAP_CYD_AUTOSWEEP=1 build
if(ORCMAP_CYD_AUTOSWEEP)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ORCMAP_CYD_AUTOSWEEP=1)
endif()

# Runs the fixed cold/warm scenario per installed pack at boot. Measurement
# only: in a plain build the sequence flickers through half-drawn maps,
# because this board renders straight to the panel.
#   idf.py -DORCMAP_CYD_SCENARIOS=1 build
if(ORCMAP_CYD_SCENARIOS)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ORCMAP_CYD_SCENARIOS=1)
endif()

# Interactive build: one finger drags the map. The frame under the finger is
# drawn with a reduced style while it moves and upgraded to full quality when
# it stops. This build owns the main loop and never runs the benchmark
# scenarios.
#   idf.py -DORCMAP_CYD_INTERACTIVE=1 build
if(ORCMAP_CYD_INTERACTIVE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ORCMAP_CYD_INTERACTIVE=1)
endif()

# Measure how far this board is from a target frame rate: sweeps the display
# SPI write clock and reports bus throughput, CPU-only frame cost and a
# read-back check per clock. Needs no SD card or pack.
#   idf.py -DORCMAP_CYD_FPS_PROBE=1 build
if(ORCMAP_CYD_FPS_PROBE)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ORCMAP_CYD_FPS_PROBE=1)
endif()

# Print every touch sample's raw ADC and mapped screen point on the serial
# log. Use this once to confirm kTouchCs/kTouchIrq and the axis mapping in
# main.cpp are right for the board in hand.
#   idf.py -DORCMAP_CYD_INTERACTIVE=1 -DORCMAP_CYD_TOUCH_DEBUG=1 build
if(ORCMAP_CYD_TOUCH_DEBUG)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE ORCMAP_CYD_TOUCH_DEBUG=1)
endif()
