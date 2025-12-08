# Examples for keycard-qt

find_package(Qt6 REQUIRED COMPONENTS Core)

# Helper function to create examples
function(add_keycard_example example_name)
    add_executable(${example_name} ${example_name}.cpp)
    
    target_link_libraries(${example_name}
        PRIVATE
            keycard-qt
            Qt6::Core
    )
    
    # Set RPATH for finding libraries at build time
    if(APPLE)
        set_target_properties(${example_name} PROPERTIES
            BUILD_WITH_INSTALL_RPATH FALSE
            BUILD_RPATH "${CMAKE_BINARY_DIR};${CMAKE_PREFIX_PATH}/lib"
            INSTALL_RPATH "@executable_path/../lib"
        )
    elseif(UNIX)
        set_target_properties(${example_name} PROPERTIES
            BUILD_RPATH "${CMAKE_BINARY_DIR}"
            INSTALL_RPATH "$ORIGIN/../lib"
        )
    endif()
endfunction()

# Simple detection example - demonstrates basic card detection
add_keycard_example(simple_detection)

message(STATUS "Example configured: simple_detection")

