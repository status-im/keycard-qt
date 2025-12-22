# Qt Test framework and Concurrent for threading tests
find_package(Qt6 REQUIRED COMPONENTS Test Concurrent)

# Enable testing
enable_testing()

# Test helper - create test executable
function(add_keycard_test test_name)
    # Check if there are additional source files (e.g., mocks)
    if(${ARGC} GREATER 1)
        set(EXTRA_SOURCES ${ARGN})
        add_executable(${test_name} ${test_name}.cpp ${EXTRA_SOURCES})
    else()
        add_executable(${test_name} ${test_name}.cpp)
    endif()
    
    target_link_libraries(${test_name}
        PRIVATE
            keycard-qt
            Qt6::Test
    )
    
    # Include test directory for mocks
    target_include_directories(${test_name}
        PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}
    )
    
    # Enable test helpers for bypassing crypto in unit tests
    target_compile_definitions(${test_name} PRIVATE KEYCARD_ENABLE_TEST_HELPERS)
    
    # Add as test
    add_test(NAME ${test_name} COMMAND ${test_name})
    
    # Set working directory
    set_tests_properties(${test_name} PROPERTIES
        WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
endfunction()

# Test files
add_keycard_test(test_apdu_command)
add_keycard_test(test_apdu_response)
add_keycard_test(test_apdu_utils)
add_keycard_test(test_secure_channel)
add_keycard_test(test_types)

# Extended test suites for higher coverage
add_keycard_test(test_command_set_extended mocks/mock_backend.cpp)
add_keycard_test(test_secure_channel_extended)
add_keycard_test(test_types_extended)
add_keycard_test(test_keycard_channel)
add_keycard_test(test_command_set_new mocks/mock_backend.cpp)
add_keycard_test(test_pbkdf2)
add_keycard_test(test_init_pair mocks/mock_backend.cpp)
add_keycard_test(test_globalplatform_crypto)

# Dependency Injection tests with mock backend
add_keycard_test(test_keycard_channel_di mocks/mock_backend.cpp)

# CommunicationManager tests (new thread-safe architecture)
add_keycard_test(test_communication_manager mocks/mock_backend.cpp)
add_keycard_test(test_communication_manager_queue mocks/mock_backend.cpp)
add_keycard_test(test_communication_manager_sync mocks/mock_backend.cpp)

# CardCommand pattern tests
add_keycard_test(test_card_command mocks/mock_backend.cpp)

# Threading tests (requires Qt Concurrent)
add_executable(test_communication_manager_threading test_communication_manager_threading.cpp mocks/mock_backend.cpp)
target_link_libraries(test_communication_manager_threading
    PRIVATE
        keycard-qt
        Qt6::Test
        Qt6::Concurrent
)
target_include_directories(test_communication_manager_threading
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
)
target_compile_definitions(test_communication_manager_threading PRIVATE KEYCARD_ENABLE_TEST_HELPERS)
add_test(NAME test_communication_manager_threading COMMAND test_communication_manager_threading)
set_tests_properties(test_communication_manager_threading PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
)

message(STATUS "Unit tests configured - run with: ctest --verbose")

