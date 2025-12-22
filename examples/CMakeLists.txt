# Examples for keycard-qt

find_package(Qt6 REQUIRED COMPONENTS Core Concurrent)

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

# CommunicationManager example - demonstrates the main API (recommended)
add_executable(communication_manager_example communication_manager_example.cpp)
target_link_libraries(communication_manager_example
    PRIVATE
        keycard-qt
        Qt6::Core
        Qt6::Concurrent
)
set_target_properties(communication_manager_example PROPERTIES AUTOMOC ON)
if(APPLE)
    set_target_properties(communication_manager_example PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        BUILD_RPATH "${CMAKE_BINARY_DIR};${CMAKE_PREFIX_PATH}/lib"
        INSTALL_RPATH "@executable_path/../lib"
    )
elseif(UNIX)
    set_target_properties(communication_manager_example PROPERTIES
        BUILD_RPATH "${CMAKE_BINARY_DIR}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
endif()

# Advanced operations example - demonstrates real-world usage
add_executable(advanced_operations_example advanced_operations_example.cpp)
target_link_libraries(advanced_operations_example
    PRIVATE
        keycard-qt
        Qt6::Core
)
set_target_properties(advanced_operations_example PROPERTIES AUTOMOC ON)
if(APPLE)
    set_target_properties(advanced_operations_example PROPERTIES
        BUILD_WITH_INSTALL_RPATH FALSE
        BUILD_RPATH "${CMAKE_BINARY_DIR};${CMAKE_PREFIX_PATH}/lib"
        INSTALL_RPATH "@executable_path/../lib"
    )
elseif(UNIX)
    set_target_properties(advanced_operations_example PROPERTIES
        BUILD_RPATH "${CMAKE_BINARY_DIR}"
        INSTALL_RPATH "$ORIGIN/../lib"
    )
endif()

message(STATUS "Examples configured:")
message(STATUS "  - simple_detection")
message(STATUS "  - communication_manager_example (recommended)")
message(STATUS "  - advanced_operations_example")

