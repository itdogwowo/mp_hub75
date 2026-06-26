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

# Module strings are not suitable for compression
target_compile_definitions(usermod_mp_hub75 INTERFACE 
    MICROPY_ROM_TEXT_COMPRESSION=0
    $<$<BOOL:MP_HUB75_DRIVER_VERSION>:MP_HUB75_DRIVER_VERSION=\"${MP_HUB75_DRIVER_VERSION}\">
)

target_link_libraries(usermod INTERFACE usermod_mp_hub75)
micropy_gather_target_properties(usermod_mp_hub75)
