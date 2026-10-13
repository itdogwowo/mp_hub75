include(${MICROPY_DIR}/py/py.cmake)

add_library(usermod_mp_hub75 INTERFACE)

target_sources(usermod_mp_hub75 INTERFACE
    ${CMAKE_CURRENT_LIST_DIR}/src/hub75_bridge.cpp
    ${CMAKE_CURRENT_LIST_DIR}/src/hub75_esp.c
)

# Register dependency on esp-hub75 component
# The component is managed by IDF component manager via idf_component.yml
if(DEFINED ESP_HUB75_DIR AND EXISTS "${ESP_HUB75_DIR}")
    message(STATUS "Using user-defined ESP_HUB75_DIR: ${ESP_HUB75_DIR}")
    set(ESP_HUB75_MANAGED_DIR "${ESP_HUB75_DIR}")
else()
    set(ESP_HUB75_MANAGED_DIR "${MICROPY_PORT_DIR}/managed_components/esphome__esp-hub75")
endif()

if(EXISTS "${ESP_HUB75_MANAGED_DIR}")
    list(APPEND MICROPY_INC_USERMOD
        ${ESP_HUB75_MANAGED_DIR}/include
    )
    message(STATUS "Found esp-hub75 at: ${ESP_HUB75_MANAGED_DIR}")
    
    # Link against the component library
    if(TARGET esphome__esp-hub75)
        idf_component_get_property(esp_hub75_lib esphome__esp-hub75 COMPONENT_LIB)
        target_link_libraries(usermod_mp_hub75 INTERFACE ${esp_hub75_lib})
    endif()
    
    # Extract driver version
    if(EXISTS "${ESP_HUB75_MANAGED_DIR}/idf_component.yml")
        file(READ "${ESP_HUB75_MANAGED_DIR}/idf_component.yml" _component_yml_contents)
        string(REGEX MATCH "version: ([0-9]+\\.[0-9]+\\.[0-9]+)" _ ${_component_yml_contents})
        if(CMAKE_MATCH_1)
            set(MP_HUB75_DRIVER_VERSION "${CMAKE_MATCH_1}")
            message(STATUS "Found esp-hub75 version: ${MP_HUB75_DRIVER_VERSION}")
        endif()
    endif()
else()
    message(WARNING "esp-hub75 component not found at ${ESP_HUB75_MANAGED_DIR}")
endif()

# NOTE: MICROPY_ROM_TEXT_COMPRESSION must NOT be defined here. The esp32 port
# forces it ON (ports/esp32/esp32_common.cmake) and passes -D...=1, while a
# usermod's INTERFACE definitions are gathered globally by
# micropy_gather_target_properties() -- so defining it as 0 collides with the
# port on *every* translation unit. That is a hard error under ESP-IDF v6 (its
# default is -Werror), and an inconsistent MP_ERROR_TEXT ABI between TUs even
# when it slips through as a warning on IDF v5. Whether module strings get
# compressed is a port-level decision, not something a usermod can override for
# itself alone.
target_compile_definitions(usermod_mp_hub75 INTERFACE 
    $<$<BOOL:MP_HUB75_DRIVER_VERSION>:MP_HUB75_DRIVER_VERSION=\"${MP_HUB75_DRIVER_VERSION}\">
)

target_link_libraries(usermod INTERFACE usermod_mp_hub75)
micropy_gather_target_properties(usermod_mp_hub75)
