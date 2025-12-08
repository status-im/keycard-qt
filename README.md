# keycard-qt

[![Tests](https://github.com/status-im/keycard-qt/actions/workflows/test.yml/badge.svg)](https://github.com/status-im/keycard-qt/actions/workflows/test.yml)

Cross-platform C++/Qt library for Keycard APDU API - a 1:1 replacement for [keycard-go](https://github.com/status-im/keycard-go).

## Features

- **Cross-platform**: Linux, macOS, Windows (PC/SC), Android, iOS (NFC)
- **Unified API**: Single codebase with automatic backend selection (PC/SC for desktop, Qt NFC for mobile)
- **Complete APDU support**: All Keycard commands implemented
- **Secure channel**: ECDH key exchange + AES-CBC encryption
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

```cpp
#include <keycard-qt/keycard_channel.h>
#include <keycard-qt/command_set.h>

// Create channel (works on all platforms!)
auto channel = new KeycardChannel();

// Start detection
channel->startDetection();

// Connect signals
connect(channel, &KeycardChannel::targetDetected, [](const QString& uid) {
    qDebug() << "Keycard detected:" << uid;
});

// Use command set
auto cmdSet = new CommandSet(channel);

// Select keycard applet
auto appInfo = cmdSet->select();
qDebug() << "Instance UID:" << appInfo.instanceUID;

// Pair with keycard
auto pairingInfo = cmdSet->pair("KeycardDefaultPairing");

// Open secure channel
cmdSet->openSecureChannel(pairingInfo.index, pairingInfo.key);

// Verify PIN
cmdSet->verifyPIN("123456");

// Export keys
auto keys = cmdSet->exportKeyExtended(true, false, 
                                      P2ExportKeyPrivateAndPublic, 
                                      "m/44'/60'/0'/0/0");
```

## Architecture

```
┌─────────────────────────────────────┐
│ CommandSet                          │
│ • High-level keycard operations     │
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

- [API Reference](docs/API.md)
- [Qt NFC Integration](https://doc.qt.io/qt-6/qtnfc-pcsc.html)

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

