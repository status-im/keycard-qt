# keycard-qt

[![Tests](https://github.com/status-im/keycard-qt/actions/workflows/test.yml/badge.svg)](https://github.com/status-im/keycard-qt/actions/workflows/test.yml)

Cross-platform C++/Qt library for Keycard APDU API - a 1:1 replacement for [keycard-go](https://github.com/status-im/keycard-go).

## Features

- **Thread-safe API**: `CommunicationManager` provides queue-based, thread-safe card operations
- **Cross-platform**: Linux, macOS, Windows (PC/SC), Android, iOS (NFC)
- **Unified backend**: Single codebase with automatic backend selection (PC/SC for desktop, Qt NFC for mobile)
- **Complete APDU support**: All Keycard commands implemented
- **Secure channel**: ECDH key exchange + AES-CBC encryption
- **Async & Sync**: Both signal-based and blocking APIs available
- **Backend architecture**: Clean separation with pluggable backends for testing and extension

## Supported Platforms

| Platform | Backend | Status |
|----------|---------|--------|
| Linux    | PC/SC | Developed - Pending Testing |
| macOS    | PC/SC | Developed - Pending Testing |
| Windows  | PC/SC | Developed - Pending Testing |
| Android  | Unified Qt NFC | Developed - Pending Testing |
| iOS      | Unified Qt NFC | Developed - Pending Testing |

## Requirements

### Build Dependencies

- CMake 3.16+
- Qt 6.9.2 or later
  - QtCore
  - QtNfc
- C++17 compatible compiler
- OpenSSL

### Runtime Dependencies

- Qt 6.9.2 runtime
- PC/SC daemon (desktop platforms)
  - Linux: pcscd
  - macOS: built-in
  - Windows: built-in
- NFC hardware (mobile platforms)

## Building

```bash
mkdir build && cd build
cmake ..
cmake --build .
cmake --install . --prefix /usr/local
```

### Build Options

- `BUILD_TESTING=ON|OFF` - Build unit tests (default: ON)
- `BUILD_EXAMPLES=ON|OFF` - Build example applications (default: OFF)

### Android Build

For Android builds, the library uses the **Unified Qt NFC backend** which is built as part of the native library:

```bash
mkdir build && cd build
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-21
make -j10
```

This builds the native library `libkeycard-qt.so` with Qt NFC support for Android.

## Quick Start

### Using CommunicationManager (Recommended - Thread-Safe)

```cpp
#include <keycard-qt/communication_manager.h>
#include <keycard-qt/command_set.h>
#include <keycard-qt/keycard_channel.h>
#include <keycard-qt/pairing_storage.h>

using namespace Keycard;

// 1. Create communication stack
auto channel = std::make_shared<KeycardChannel>();
auto pairingStorage = std::make_shared<FilePairingStorage>();
auto passwordProvider = [](const QString& cardUID) {
    return "KeycardDefaultPairing";
};
auto cmdSet = std::make_shared<CommandSet>(channel, pairingStorage, passwordProvider);

// 2. Create CommunicationManager (thread-safe API)
auto commManager = std::make_shared<CommunicationManager>();
commManager->init(cmdSet);

// 3. Connect signals
connect(commManager.get(), &CommunicationManager::cardInitialized,
        [](CardInitializationResult result) {
    if (result.success) {
        qDebug() << "Card ready! UID:" << result.uid;
        qDebug() << "Version:" << result.appInfo.version;
    }
});

// 4. Start detection
commManager->startDetection();

// 5. Execute commands synchronously (thread-safe)
auto selectCmd = std::make_unique<SelectCommand>();
CommandResult result = commManager->executeCommandSync(std::move(selectCmd), 5000);

// 6. Or asynchronously
auto verifyCmd = std::make_unique<VerifyPINCommand>("123456");
QUuid token = commManager->enqueueCommand(std::move(verifyCmd));
connect(commManager.get(), &CommunicationManager::commandCompleted,
        [token](QUuid completedToken, CommandResult result) {
    if (completedToken == token && result.success) {
        qDebug() << "PIN verified!";
    }
});
```

### Direct API (For Simple Use Cases)

```cpp
#include <keycard-qt/keycard_channel.h>
#include <keycard-qt/command_set.h>

// Create channel (works on all platforms!)
auto channel = std::make_shared<KeycardChannel>();
auto cmdSet = std::make_shared<CommandSet>(channel, nullptr, nullptr);

// Connect signals
connect(channel.get(), &KeycardChannel::targetDetected, 
        [cmdSet](const QString& uid) {
    qDebug() << "Keycard detected:" << uid;
    
    // Select and use
    auto appInfo = cmdSet->select();
    cmdSet->verifyPIN("123456");
});

channel->startDetection();
```

## Architecture

```
┌─────────────────────────────────────┐
│ CommunicationManager                │
│ • Thread-safe queue-based API       │
│ • Async (signal) + Sync (blocking)  │
│ • Single communication thread       │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│ CommandSet                          │
│ • High-level keycard operations     │
│ • Pairing & secure channel mgmt     │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│ SecureChannel                       │
│ • ECDH + AES-CBC encryption         │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│ KeycardChannel                      │
│ • Platform-adaptive API             │
│ • Automatic backend selection       │
└──────────────┬──────────────────────┘
               │
        ┌──────┴──────┐
        │             │
┌───────▼──────┐ ┌────▼────────────────┐
│ PC/SC        │ │ Unified Qt NFC      │
│ Backend      │ │ Backend             │
│              │ │                     │
│ • Windows    │ │ • iOS               │
│ • macOS      │ │ • Android           │
│ • Linux      │ │                     │
└──────────────┘ └─────────────────────┘
```

## Documentation

- [API Reference](docs/API.md) - Complete API documentation
- [Examples Guide](EXAMPLES_GUIDE.md) - Detailed guide for all examples
- [Qt NFC Integration](https://doc.qt.io/qt-6/qtnfc-pcsc.html) - Qt NFC documentation

## Testing

The project includes comprehensive unit tests using Qt Test framework:

```bash
cd build
ctest --output-on-failure
```


## Credits

- Based on [keycard-go](https://github.com/status-im/keycard-go)
- Part of the [Status](https://status.im) ecosystem
- Uses [Qt](https://www.qt.io/) framework
- Cryptography via [OpenSSL](https://github.com/openssl/openssl)

## Related Projects

- [status-keycard-qt](../status-keycard-qt/) - High-level Session API
- [keycard-go](https://github.com/status-im/keycard-go) - Original Go implementation
- [Keycard](https://keycard.tech/) - Hardware keycard specification

