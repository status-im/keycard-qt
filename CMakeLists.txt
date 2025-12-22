cmake_minimum_required(VERSION 3.16)
project(keycard-qt VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)
# Qt modules
# Core is always required
# Nfc is required for iOS, Android, and unified backend (Desktop uses PC/SC by default)
find_package(Qt6 REQUIRED COMPONENTS Core)

if(IOS OR ANDROID)
    message(STATUS "Backend: Unified Qt NFC (Android/iOS)")
    find_package(Qt6 REQUIRED COMPONENTS Nfc)
endif()

# OpenSSL for secp256k1 ECDH
# For Android and iOS, we use manually provided paths instead of find_package
if((ANDROID OR IOS) AND OPENSSL_CRYPTO_LIBRARY AND OPENSSL_BUILD_INCLUDE_DIR AND OPENSSL_SOURCE_INCLUDE_DIR)
    set(OpenSSL_FOUND TRUE)
else()
    find_package(OpenSSL)
    if(OpenSSL_FOUND)
        message(STATUS "OpenSSL found: ${OPENSSL_VERSION}")
    else()
        message(WARNING "OpenSSL not found. Building without secp256k1 ECDH support.")
    endif()
endif()

# Library sources
set(KEYCARD_QT_SOURCES
    # APDU layer
    src/apdu/command.cpp
    src/apdu/response.cpp
    src/apdu/utils.cpp
    
    # Channel (main factory)
    src/channel/keycard_channel.cpp
    
    # Crypto
    src/crypto/secure_channel.cpp
    
    # GlobalPlatform
    src/globalplatform/gp_crypto.cpp
    src/globalplatform/scp02_session.cpp
    src/globalplatform/scp02_wrapper.cpp
    src/globalplatform/gp_command_set.cpp
    
    # Types
    src/types/application_info.cpp
    src/types/application_status.cpp
    src/types/pairing_info.cpp
    src/types/exported_key.cpp
    src/types/secrets.cpp
    
    # Command Set
    src/command_set.cpp
    
    # Communication Manager (queue-based architecture)
    src/i_communication_manager.cpp
    src/card_command.cpp
    src/communication_manager.cpp
    src/tlv_utils.cpp
    src/metadata_utils.cpp
)

# Public headers
set(KEYCARD_QT_HEADERS
    include/keycard-qt/channel_interface.h
    include/keycard-qt/keycard_channel.h
    include/keycard-qt/command_set.h
    include/keycard-qt/secure_channel.h
    include/keycard-qt/types.h
    include/keycard-qt/apdu/command.h
    include/keycard-qt/apdu/response.h
    include/keycard-qt/apdu/utils.h
    # GlobalPlatform
    include/keycard-qt/globalplatform/gp_constants.h
    include/keycard-qt/globalplatform/gp_crypto.h
    include/keycard-qt/globalplatform/scp02_session.h
    include/keycard-qt/globalplatform/scp02_wrapper.h
    include/keycard-qt/globalplatform/gp_command_set.h
    # Backend interface
    include/keycard-qt/backends/keycard_channel_backend.h
    # Communication Manager (queue-based architecture)
    include/keycard-qt/i_communication_manager.h
    include/keycard-qt/card_command.h
    include/keycard-qt/communication_manager.h
    include/keycard-qt/tlv_utils.h
    include/keycard-qt/metadata_utils.h
)


if(IOS OR ANDROID)
    list(APPEND KEYCARD_QT_SOURCES
        src/channel/backends/keycard_channel_unified_qt_nfc.cpp
    )
    list(APPEND KEYCARD_QT_HEADERS
        include/keycard-qt/backends/keycard_channel_unified_qt_nfc.h
    )
else()
    list(APPEND KEYCARD_QT_SOURCES
        src/channel/backends/keycard_channel_pcsc.cpp
    )
    list(APPEND KEYCARD_QT_HEADERS
        include/keycard-qt/backends/keycard_channel_pcsc.h
    )
endif()

# Create library (shared or static based on BUILD_SHARED_LIBS)
add_library(keycard-qt
    ${KEYCARD_QT_SOURCES}
    ${KEYCARD_QT_HEADERS}
)

# Suppress OpenSSL 3.0 deprecation warnings (we use legacy APIs intentionally)
if(OpenSSL_FOUND)
    target_compile_definitions(keycard-qt PRIVATE OPENSSL_SUPPRESS_DEPRECATED)
endif()

# Include directories
target_include_directories(keycard-qt
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

# Link libraries
target_link_libraries(keycard-qt
    PUBLIC
        Qt6::Core
)

# Qt NFC for iOS, Android, and unified backend (desktop)
if(IOS OR ANDROID)
    target_link_libraries(keycard-qt PUBLIC Qt6::Nfc)
endif()

# Link OpenSSL (if found)
if(OpenSSL_FOUND)
    # For Android and iOS with manually provided paths, link directly to the library file
    if((ANDROID OR IOS) AND OPENSSL_CRYPTO_LIBRARY)
        message(STATUS "Linking against OpenSSL crypto library: ${OPENSSL_CRYPTO_LIBRARY}")
        target_link_libraries(keycard-qt PRIVATE ${OPENSSL_CRYPTO_LIBRARY})
        
        # Add include directories
        # For OpenSSL 3.x on Android/iOS, we can use source headers directly
        # The generated build headers are not strictly necessary if using modern OpenSSL
        set(OPENSSL_INCLUDES_AVAILABLE FALSE)
        
        # Check if source headers exist (these are the main API headers)
        if(EXISTS "${OPENSSL_SOURCE_INCLUDE_DIR}")
            message(STATUS "Adding OpenSSL source include dir: ${OPENSSL_SOURCE_INCLUDE_DIR}")
            target_include_directories(keycard-qt PRIVATE ${OPENSSL_SOURCE_INCLUDE_DIR})
            set(OPENSSL_INCLUDES_AVAILABLE TRUE)
            
            # Also add build dir if it exists (has generated configuration.h)
            if(EXISTS "${OPENSSL_BUILD_INCLUDE_DIR}")
                message(STATUS "Adding OpenSSL build include dir: ${OPENSSL_BUILD_INCLUDE_DIR}")
                target_include_directories(keycard-qt PRIVATE ${OPENSSL_BUILD_INCLUDE_DIR})
            else()
                message(STATUS "OpenSSL build includes not found (using source headers only)")
            endif()
        else()
            message(WARNING "OpenSSL source include dir not found: ${OPENSSL_SOURCE_INCLUDE_DIR}")
            message(WARNING "OpenSSL headers not available. Building WITHOUT OpenSSL support.")
            message(WARNING "Crypto features will use fallback implementations.")
        endif()
        
        # Define KEYCARD_QT_HAS_OPENSSL if we have headers AND library
        if(OPENSSL_INCLUDES_AVAILABLE AND EXISTS "${OPENSSL_CRYPTO_LIBRARY}")
            target_compile_definitions(keycard-qt PRIVATE KEYCARD_QT_HAS_OPENSSL)
            message(STATUS "OpenSSL support: ENABLED (library + headers available)")
        else()
            message(STATUS "OpenSSL support: DISABLED (missing library or headers)")
        endif()
    else()
        # For other platforms, use the imported target
        target_link_libraries(keycard-qt PRIVATE OpenSSL::Crypto)
        target_compile_definitions(keycard-qt PRIVATE KEYCARD_QT_HAS_OPENSSL)
    endif()
endif()

# Link PC/SC framework on Apple platforms (desktop only, not iOS)
# Required for both direct PC/SC backend and Qt NFC's PC/SC backend
if(APPLE AND NOT IOS)
    find_library(PCSC_LIBRARY PCSC)
    if(PCSC_LIBRARY)
        target_link_libraries(keycard-qt PRIVATE ${PCSC_LIBRARY})
        message(STATUS "Found PCSC framework: ${PCSC_LIBRARY}")
    else()
        message(WARNING "PCSC framework not found")
    endif()
endif()

# Link CoreNFC framework on iOS (required for NFC support)
if(IOS)
    find_library(CORENFC_LIBRARY CoreNFC)
    if(CORENFC_LIBRARY)
        target_link_libraries(keycard-qt PRIVATE ${CORENFC_LIBRARY})
        message(STATUS "Found CoreNFC framework: ${CORENFC_LIBRARY}")
    else()
        message(WARNING "CoreNFC framework not found - NFC will not work on iOS")
    endif()
endif()

# Link PC/SC library on Linux (not Android)
if(UNIX AND NOT APPLE AND NOT ANDROID)
    # Use pkg-config to get PC/SC include paths and libraries
    find_package(PkgConfig QUIET)
    if(PKG_CONFIG_FOUND)
        pkg_check_modules(PCSC libpcsclite)
        if(PCSC_FOUND)
            target_include_directories(keycard-qt PRIVATE ${PCSC_INCLUDE_DIRS})
            target_link_libraries(keycard-qt PRIVATE ${PCSC_LIBRARIES})
            message(STATUS "Found PC/SC via pkg-config: ${PCSC_VERSION}")
        else()
            message(WARNING "PC/SC not found via pkg-config")
        endif()
    else()
        # Fallback to find_library if pkg-config not available
        find_library(PCSCLITE_LIBRARY pcsclite)
        if(PCSCLITE_LIBRARY)
            target_link_libraries(keycard-qt PRIVATE ${PCSCLITE_LIBRARY})
            message(STATUS "Found PC/SC library: ${PCSCLITE_LIBRARY}")
        else()
            message(WARNING "PC/SC library not found")
        endif()
    endif()
endif()

# Link winscard on Windows
if(WIN32)
    target_link_libraries(keycard-qt PRIVATE winscard)
endif()

# Compiler warnings
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(keycard-qt PRIVATE
        -Wall -Wextra -Wpedantic
        -Wno-unused-parameter
    )
elseif(MSVC)
    target_compile_options(keycard-qt PRIVATE /W4)
endif()

# Export compile commands for IDE integration
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Properties
set_target_properties(keycard-qt PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION 0
    PUBLIC_HEADER "${KEYCARD_QT_HEADERS}"
)

# Installation
include(GNUInstallDirs)

install(TARGETS keycard-qt
    EXPORT keycard-qt-targets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    PUBLIC_HEADER DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/keycard-qt
)

# Install subdirectory headers
install(DIRECTORY include/keycard-qt/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/keycard-qt
    FILES_MATCHING PATTERN "*.h"
)

# Export targets
install(EXPORT keycard-qt-targets
    FILE keycard-qt-targets.cmake
    NAMESPACE keycard-qt::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/keycard-qt
)

# Package configuration
include(CMakePackageConfigHelpers)

configure_package_config_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/cmake/keycard-qt-config.cmake.in
    ${CMAKE_CURRENT_BINARY_DIR}/keycard-qt-config.cmake
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/keycard-qt
)

write_basic_package_version_file(
    ${CMAKE_CURRENT_BINARY_DIR}/keycard-qt-config-version.cmake
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY AnyNewerVersion
)

install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/keycard-qt-config.cmake
    ${CMAKE_CURRENT_BINARY_DIR}/keycard-qt-config-version.cmake
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/keycard-qt
)

# Testing
option(BUILD_TESTING "Build tests" ON)
if(BUILD_TESTING)
    enable_testing()
    add_subdirectory(tests)
endif()

# Examples
option(BUILD_EXAMPLES "Build examples" OFF)
if(BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

