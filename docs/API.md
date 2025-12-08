# keycard-qt API Reference

Complete API documentation for keycard-qt library.

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Core Classes](#core-classes)
  - [KeycardChannel](#keycardchannel)
  - [CommandSet](#commandset)
  - [SecureChannel](#securechannel)
- [Backend System](#backend-system)
  - [KeycardChannelBackend](#keycardchannelbackend)
  - [Platform Backends](#platform-backends)
- [APDU Layer](#apdu-layer)
  - [APDU::Command](#apducommand)
  - [APDU::Response](#apduresponse)
- [Data Types](#data-types)
  - [ApplicationInfo](#applicationinfo)
  - [ApplicationStatus](#applicationstatus)
  - [PairingInfo](#pairinginfo)
  - [Secrets](#secrets)
  - [ExportedKey](#exportedkey)
  - [Signature](#signature)
- [Error Handling](#error-handling)
- [Threading Model](#threading-model)
- [Platform-Specific Considerations](#platform-specific-considerations)
- [Examples](#examples)

---

## Overview

keycard-qt provides a complete C++/Qt implementation of the Keycard APDU API. The library uses a layered architecture:

1. **CommandSet** - High-level keycard operations (pairing, signing, key management)
2. **SecureChannel** - ECDH key exchange + AES-CBC encryption
3. **KeycardChannel** - Platform-adaptive communication layer
4. **Backend** - Platform-specific implementations (PC/SC for desktop, Qt NFC for mobile)

All classes are in the `Keycard` namespace.

---

## Architecture

```
┌─────────────────────────────────────┐
│ CommandSet                          │
│ • High-level keycard operations     │
│ • Automatic pairing management      │
│ • PIN/PUK handling                  │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│ SecureChannel                       │
│ • ECDH + AES-CBC encryption         │
│ • CMAC authentication               │
│ • Session key derivation            │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
│ KeycardChannel                      │
│ • Platform-adaptive API             │
│ • Automatic backend selection       │
│ • Card detection/connection         │
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

---

## Core Classes

### KeycardChannel

**Header:** `keycard-qt/keycard_channel.h`

Platform-adaptive communication channel for Keycard. Automatically selects the appropriate backend based on the target platform.

#### Constructor

```cpp
// Create with default platform backend
explicit KeycardChannel(QObject* parent = nullptr);

// Create with custom backend (for testing/DI)
explicit KeycardChannel(KeycardChannelBackend* backend, QObject* parent = nullptr);
```

#### Methods

##### Card Detection

```cpp
// Start detecting cards/tags
void startDetection();

// Stop detecting cards/tags
void stopDetection();

// Force immediate re-scan
void forceScan();

// Disconnect from current card
void disconnect();
```

##### Communication

```cpp
// Transmit APDU command (blocking)
QByteArray transmit(const QByteArray& apdu);

// Check if connected to a card
bool isConnected() const;
```

##### Status & Information

```cpp
// Get current card UID
QString targetUid() const;

// Get backend name (e.g., "PC/SC", "Qt NFC")
QString backendName() const;

// Get backend instance (for platform-specific features)
KeycardChannelBackend* backend() const;
```

##### State Management

```cpp
// Set channel lifecycle state
void setState(ChannelState state);

// Get lifecycle state
ChannelState state() const;

// Get operational state
ChannelOperationalState channelState() const;
```

##### iOS-Specific

```cpp
// Request card at app startup (shows NFC drawer on iOS)
bool requestCardAtStartup();
```

#### Signals

```cpp
// Reader availability changed (PC/SC only)
void readerAvailabilityChanged(bool available);

// Card detected and ready
void targetDetected(const QString& uid);

// Card removed or connection lost
void targetLost();

// Error occurred
void error(const QString& message);

// Operational state changed
void channelStateChanged(ChannelOperationalState state);
```

#### Example

```cpp
auto channel = std::make_shared<KeycardChannel>();

// Connect signals
connect(channel.get(), &KeycardChannel::targetDetected, 
        [](const QString& uid) {
    qDebug() << "Card detected:" << uid;
});

connect(channel.get(), &KeycardChannel::targetLost, 
        []() {
    qDebug() << "Card removed";
});

// Start detection
channel->startDetection();
```

---

### CommandSet

**Header:** `keycard-qt/command_set.h`

High-level interface for all Keycard operations. Handles secure channel management, automatic pairing, and response parsing.

#### Constructor

```cpp
explicit CommandSet(
    std::shared_ptr<KeycardChannel> channel,
    std::shared_ptr<IPairingStorage> pairingStorage,
    PairingPasswordProvider passwordProvider,
    QObject* parent = nullptr
);
```

**Parameters:**
- `channel` - Communication channel (required)
- `pairingStorage` - Optional pairing storage for persistence
- `passwordProvider` - Optional callback to provide pairing password
- `parent` - QObject parent

#### Connection & Pairing

```cpp
// Select the Keycard applet
ApplicationInfo select(bool force = false);

// Pair with card
PairingInfo pair(const QString& pairingPassword);

// Open secure channel
bool openSecureChannel(const PairingInfo& pairingInfo);

// Mutual authentication
bool mutualAuthenticate();

// Ensure pairing is available (auto-pairs if needed)
bool ensurePairing();

// Ensure secure channel is ready
bool ensureSecureChannel();
```

#### Initialization & Setup

```cpp
// Initialize a new keycard
bool init(const Secrets& secrets);

// Unpair a pairing slot
bool unpair(uint8_t index);

// Factory reset (⚠️ ERASES ALL DATA)
bool factoryReset();
```

#### Status & Information

```cpp
// Get application status
ApplicationStatus getStatus(uint8_t info = APDU::P1GetStatusApplication);

// Get cached application status
ApplicationStatus cachedApplicationStatus() const;

// Check if status is cached
bool hasCachedStatus() const;

// Get application info (from last select())
ApplicationInfo applicationInfo() const;

// Get pairing info
PairingInfo pairingInfo() const;
```

#### Authentication

```cpp
// Verify PIN (⚠️ 3 wrong attempts will BLOCK the PIN!)
bool verifyPIN(const QString& pin);

// Get remaining PIN attempts
int remainingPINAttempts() const;

// Change PIN
bool changePIN(const QString& newPIN);

// Change PUK
bool changePUK(const QString& newPUK);

// Unblock PIN using PUK (⚠️ 5 wrong PUK attempts will permanently block the card!)
bool unblockPIN(const QString& puk, const QString& newPIN);

// Change pairing password
bool changePairingSecret(const QString& newPassword);
```

#### Key Management

```cpp
// Generate new key pair on card
QByteArray generateKey();

// Generate BIP39 mnemonic
QVector<int> generateMnemonic(int checksumSize = 4);

// Load seed to card
QByteArray loadSeed(const QByteArray& seed);

// Remove key from card
bool removeKey();

// Derive key at BIP32 path
bool deriveKey(const QString& path);
```

#### Signing

```cpp
// Sign with current key
QByteArray sign(const QByteArray& data);

// Sign with key at specific path
QByteArray signWithPath(const QByteArray& data, 
                         const QString& path, 
                         bool makeCurrent = false);

// Sign with full TLV response
QByteArray signWithPathFullResponse(const QByteArray& data,
                                     const QString& path,
                                     bool makeCurrent = false);

// Sign without PIN (if pinless path set)
QByteArray signPinless(const QByteArray& data);

// Set path for pinless signing
bool setPinlessPath(const QString& path);
```

#### Key Export

```cpp
// Export public key (or private+public)
QByteArray exportKey(bool derive = false,
                      bool makeCurrent = false,
                      const QString& path = QString(),
                      uint8_t exportType = APDU::P2ExportKeyPublicOnly);

// Export extended key (includes chain code)
QByteArray exportKeyExtended(bool derive = false,
                              bool makeCurrent = false,
                              const QString& path = QString(),
                              uint8_t exportType = APDU::P2ExportKeyExtendedPublic);
```

#### Data Storage

```cpp
// Store data on card
bool storeData(uint8_t type, const QByteArray& data);

// Get data from card
QByteArray getData(uint8_t type);
```

**Data types:**
- `0x00` - Public data
- `0x01` - NDEF data
- `0x02` - Cash data

#### Utilities

```cpp
// Identify the card
QByteArray identify(const QByteArray& challenge = QByteArray());

// Wait for card presence
bool waitForCard(int timeoutMs = -1);

// Set default wait timeout
void setDefaultWaitTimeout(int timeoutMs);

// Get last error message
QString lastError() const;
```

#### iOS Session Management

```cpp
// Reset secure channel state (iOS: when NFC drawer closes)
void resetSecureChannel();

// Re-establish secure channel after session loss
bool reestablishSecureChannel();

// Clear authentication cache
void clearAuthenticationCache();

// Handle card swap
void handleCardSwap();
```

#### Example

```cpp
// Create command set with auto-pairing
auto channel = std::make_shared<KeycardChannel>();
auto storage = std::make_shared<MyPairingStorage>();
auto passwordProvider = [](const QString& uid) {
    return "KeycardDefaultPairing";
};

auto cmdSet = new CommandSet(channel, storage, passwordProvider);

// Select applet
auto appInfo = cmdSet->select();
qDebug() << "Keycard version:" << appInfo.appVersion;

// Pairing and secure channel handled automatically
// Just call the operation you need
cmdSet->verifyPIN("123456");

// Sign a transaction
QByteArray hash = /* 32-byte hash */;
QByteArray signature = cmdSet->signWithPath(hash, "m/44'/60'/0'/0/0");
```

---

### SecureChannel

**Header:** `keycard-qt/secure_channel.h`

Implements secure communication with Keycard using ECDH key exchange and AES-CBC encryption.

#### Constructor

```cpp
explicit SecureChannel(IChannel* channel);
```

#### Methods

```cpp
// Generate ephemeral ECDH key pair
bool generateSecret(const QByteArray& cardPublicKey);

// Initialize session keys
void init(const QByteArray& iv, 
          const QByteArray& encKey, 
          const QByteArray& macKey);

// Reset secure channel state
void reset();

// Get raw public key
QByteArray rawPublicKey() const;

// Get shared secret
QByteArray secret() const;

// Send encrypted command
APDU::Response send(const APDU::Command& command);

// Encrypt/decrypt data
QByteArray encrypt(const QByteArray& plaintext);
QByteArray decrypt(const QByteArray& ciphertext);
QByteArray oneShotEncrypt(const QByteArray& data);

// Check if secure channel is open
bool isOpen() const;
```

#### Example

```cpp
auto secureChannel = QSharedPointer<SecureChannel>::create(channel);

// Open secure channel (typically done via CommandSet)
secureChannel->generateSecret(cardPublicKey);
secureChannel->init(iv, encKey, macKey);

// Send secure command
APDU::Command cmd(0x80, 0x20, 0x00, 0x00);
cmd.setData(pinData);
APDU::Response response = secureChannel->send(cmd);
```

---

## Backend System

### KeycardChannelBackend

**Header:** `keycard-qt/backends/keycard_channel_backend.h`

Abstract interface for platform-specific communication backends.

#### Pure Virtual Methods

```cpp
// Card detection
virtual void startDetection() = 0;
virtual void stopDetection() = 0;
virtual void forceScan() = 0;

// Communication
virtual QByteArray transmit(const QByteArray& apdu) = 0;
virtual bool isConnected() const = 0;
virtual void disconnect() = 0;

// State management
virtual void setState(ChannelState state) = 0;
virtual ChannelState state() const = 0;

// Information
virtual QString backendName() const = 0;
```

#### Virtual Methods

```cpp
// Optional: operational state
virtual ChannelOperationalState channelState() const;

// Optional: iOS startup card request
virtual bool requestCardAtStartup();
```

#### Signals

```cpp
void readerAvailabilityChanged(bool available);
void targetDetected(const QString& uid);
void cardRemoved();
void error(const QString& message);
void channelStateChanged(ChannelOperationalState state);
```

### Platform Backends

#### KeycardChannelPcsc

**Platform:** Windows, macOS, Linux  
**Header:** `keycard-qt/backends/keycard_channel_pcsc.h`

Desktop backend using PC/SC (Personal Computer/Smart Card) protocol.

**Features:**
- Automatic smart card reader detection
- Continuous polling for card presence
- T=0/T=1 protocol support
- Multi-reader support

#### KeycardChannelUnifiedQtNfc

**Platform:** iOS, Android  
**Header:** `keycard-qt/backends/keycard_channel_unified_qt_nfc.h`

Mobile backend using Qt NFC framework.

**Features:**
- iOS: CoreNFC integration with system NFC drawer
- Android: Qt NFC with IsoDep support
- Automatic platform detection
- Session management

---

## APDU Layer

### APDU::Command

**Header:** `keycard-qt/apdu/command.h`

Represents an APDU command.

#### Structure

```
[CLA | INS | P1 | P2 | Lc | Data | Le]
```

#### Constructor

```cpp
Command(uint8_t cla, uint8_t ins, uint8_t p1 = 0, uint8_t p2 = 0);
```

#### Methods

```cpp
void setData(const QByteArray& data);
void setLe(uint8_t le);
QByteArray serialize() const;

// Getters
uint8_t cla() const;
uint8_t ins() const;
uint8_t p1() const;
uint8_t p2() const;
QByteArray data() const;
bool hasLe() const;
uint8_t le() const;
```

#### Example

```cpp
// Create SELECT command
APDU::Command cmd(0x00, 0xA4, 0x04, 0x00);
cmd.setData(QByteArray::fromHex("A0000008040001"));
cmd.setLe(0);

QByteArray apdu = cmd.serialize();
```

---

### APDU::Response

**Header:** `keycard-qt/apdu/response.h`

Represents an APDU response.

#### Structure

```
[Data | SW1 | SW2]
```

#### Constructor

```cpp
explicit Response(const QByteArray& rawResponse);
```

#### Methods

```cpp
// Data access
QByteArray data() const;
uint16_t sw() const;

// Status checking
bool isOK() const;                    // SW = 0x9000
bool isSecurityError() const;
bool isBlocked() const;
bool isWrongPIN() const;
bool isWrongPUK() const;

// Error information
int remainingAttempts() const;        // For wrong PIN/PUK
QString errorMessage() const;
```

#### Status Words

Common status words (see `types.h` for complete list):

| Status Word | Constant | Meaning |
|-------------|----------|---------|
| `0x9000` | `SW_OK` | Success |
| `0x63Cx` | - | Wrong PIN/PUK (x = remaining attempts) |
| `0x6982` | `SW_SECURITY_CONDITION_NOT_SATISFIED` | Security condition not satisfied |
| `0x6983` | `SW_AUTHENTICATION_METHOD_BLOCKED` | PIN/PUK blocked |
| `0x6984` | `SW_DATA_INVALID` | Data invalid |
| `0x6985` | `SW_CONDITIONS_NOT_SATISFIED` | Conditions not satisfied |
| `0x6A80` | `SW_WRONG_DATA` | Wrong data |
| `0x6A82` | `SW_FILE_NOT_FOUND` | Applet not found |
| `0x6A84` | `SW_NO_AVAILABLE_PAIRING_SLOTS` | No pairing slots available |

#### Example

```cpp
APDU::Response response(rawData);

if (response.isOK()) {
    QByteArray data = response.data();
    // Process data
} else if (response.isWrongPIN()) {
    int remaining = response.remainingAttempts();
    qWarning() << "Wrong PIN! Remaining attempts:" << remaining;
} else {
    qCritical() << "Error:" << response.errorMessage();
}
```

---

## Data Types

### ApplicationInfo

**Header:** `keycard-qt/types.h`

Information returned by SELECT command.

```cpp
struct ApplicationInfo {
    QByteArray instanceUID;           // Unique card instance ID
    QByteArray secureChannelPublicKey; // Card's public key for ECDH
    uint8_t appVersion;               // Application version
    uint8_t appVersionMinor;          // Application minor version
    uint8_t availableSlots;           // Available pairing slots
    bool installed;                   // Keycard applet installed?
    bool initialized;                 // Keycard initialized?
    QByteArray keyUID;                // Key UID if keys loaded
};
```

---

### ApplicationStatus

**Header:** `keycard-qt/types.h`

Current application status.

```cpp
struct ApplicationStatus {
    uint8_t pinRetryCount;   // Remaining PIN attempts
    uint8_t pukRetryCount;   // Remaining PUK attempts
    bool keyInitialized;     // Keys loaded?
    QByteArray currentPath;  // Current derivation path
    bool valid;              // Status is valid?
};
```

---

### PairingInfo

**Header:** `keycard-qt/types.h`

Pairing information for secure channel.

```cpp
struct PairingInfo {
    QByteArray key;    // Pairing key (32 bytes)
    int index;         // Pairing slot index (0-4)
    
    bool isValid() const;
};
```

---

### Secrets

**Header:** `keycard-qt/types.h`

Secrets for initializing a new keycard.

```cpp
struct Secrets {
    QString pin;              // PIN (6 digits)
    QString puk;              // PUK (12 digits)
    QString pairingPassword;  // Pairing password (5-25 chars)
};
```

---

### ExportedKey

**Header:** `keycard-qt/types.h`

Exported key information.

```cpp
struct ExportedKey {
    QByteArray publicKey;   // Public key (if exported)
    QByteArray privateKey;  // Private key (if exported)
    QByteArray chainCode;   // BIP32 chain code
};
```

---

### Signature

**Header:** `keycard-qt/types.h`

ECDSA signature components.

```cpp
struct Signature {
    QByteArray r;        // R component
    QByteArray s;        // S component
    uint8_t v;           // Recovery ID
    QByteArray publicKey; // Public key that signed
};
```

---

## Error Handling

### Exception-Based Errors

The library uses exceptions for critical errors:

```cpp
try {
    QByteArray response = channel->transmit(apdu);
} catch (const std::runtime_error& e) {
    qCritical() << "Communication error:" << e.what();
}
```

### Return Value Errors

Most CommandSet methods return `bool` or empty data on failure:

```cpp
if (!cmdSet->verifyPIN(pin)) {
    int remaining = cmdSet->remainingPINAttempts();
    qWarning() << "Wrong PIN! Remaining:" << remaining;
}

QString error = cmdSet->lastError();
if (!error.isEmpty()) {
    qCritical() << "Error:" << error;
}
```

### Signal-Based Errors

Channel errors are emitted as signals:

```cpp
connect(channel, &KeycardChannel::error, 
        [](const QString& message) {
    qCritical() << "Channel error:" << message;
});
```

### APDU Response Errors

Check response status words:

```cpp
APDU::Response response = /* ... */;

if (!response.isOK()) {
    if (response.isWrongPIN()) {
        // Handle wrong PIN
    } else if (response.isBlocked()) {
        // Handle blocked card
    } else {
        qCritical() << response.errorMessage();
    }
}
```

---

## Threading Model

### Main Thread Usage

All Qt signal/slot operations must happen on the main thread:

```cpp
// ✅ Correct: Use from main thread
auto channel = new KeycardChannel(this);
connect(channel, &KeycardChannel::targetDetected, this, &MyClass::onCardDetected);
channel->startDetection();
```

### Worker Thread Usage

For blocking operations, use worker threads:

```cpp
// ✅ Correct: Blocking operations in worker thread
QThread* workerThread = new QThread;
auto cmdSet = new CommandSet(channel, storage, provider);

cmdSet->moveToThread(workerThread);

connect(workerThread, &QThread::started, [cmdSet]() {
    // Blocking operation safe in worker thread
    cmdSet->verifyPIN("123456");
    auto signature = cmdSet->sign(hash);
});

workerThread->start();
```

### Thread Safety

- **SecureChannel**: Thread-safe (uses internal mutex)
- **KeycardChannel**: Should be used from main thread
- **CommandSet**: Should be used from single thread (worker or main)

---

## Platform-Specific Considerations

### iOS

#### NFC Session Management

iOS requires explicit NFC session management:

```cpp
// Show NFC drawer at app startup
if (!channel->requestCardAtStartup()) {
    qWarning() << "Failed to start NFC session";
}

// Handle NFC drawer closing
connect(channel, &KeycardChannel::targetLost, [cmdSet]() {
    cmdSet->resetSecureChannel();
});
```

#### Entitlements

Add NFC capability to `Info.plist`:

```xml
<key>NFCReaderUsageDescription</key>
<string>Read Keycard for secure operations</string>
<key>com.apple.developer.nfc.readersession.formats</key>
<array>
    <string>TAG</string>
</array>
```

#### Session Reestablishment

```cpp
// After NFC drawer reopens, reestablish secure channel
if (!cmdSet->reestablishSecureChannel()) {
    qWarning() << "Failed to reestablish secure channel";
}
```

### Android

#### Permissions

Add NFC permission to `AndroidManifest.xml`:

```xml
<uses-permission android:name="android.permission.NFC" />
<uses-feature android:name="android.hardware.nfc" android:required="true" />
```

#### Build Configuration

Use CMake toolchain for Android:

```bash
cmake .. \
    -DCMAKE_TOOLCHAIN_FILE=$ANDROID_NDK/build/cmake/android.toolchain.cmake \
    -DANDROID_ABI=arm64-v8a \
    -DANDROID_PLATFORM=android-21
```

### Desktop (PC/SC)

#### Runtime Dependencies

Ensure PC/SC daemon is running:

- **Linux**: `sudo systemctl start pcscd`
- **macOS**: Built-in (no setup needed)
- **Windows**: Built-in (no setup needed)

#### Reader Detection

```cpp
connect(channel, &KeycardChannel::readerAvailabilityChanged,
        [](bool available) {
    if (!available) {
        qWarning() << "No smart card readers detected!";
    }
});
```

---

## Examples

### Basic Card Detection

```cpp
#include <keycard-qt/keycard_channel.h>

auto channel = new KeycardChannel(this);

connect(channel, &KeycardChannel::targetDetected, 
        [](const QString& uid) {
    qDebug() << "Card detected:" << uid;
});

connect(channel, &KeycardChannel::targetLost, 
        []() {
    qDebug() << "Card removed";
});

channel->startDetection();
```

### Complete Pairing Flow

```cpp
#include <keycard-qt/keycard_channel.h>
#include <keycard-qt/command_set.h>

// Create channel
auto channel = std::make_shared<KeycardChannel>();

// Create command set (no auto-pairing)
auto cmdSet = new CommandSet(channel, nullptr, nullptr);

// Wait for card
channel->startDetection();
// ... wait for targetDetected signal ...

// Select applet
ApplicationInfo appInfo = cmdSet->select();
qDebug() << "Keycard version:" << appInfo.appVersion;

// Pair with card
PairingInfo pairingInfo = cmdSet->pair("KeycardDefaultPairing");
qDebug() << "Pairing index:" << pairingInfo.index;

// Open secure channel
if (cmdSet->openSecureChannel(pairingInfo)) {
    qDebug() << "Secure channel opened!";
}

// Verify PIN
if (cmdSet->verifyPIN("123456")) {
    qDebug() << "PIN verified!";
} else {
    int remaining = cmdSet->remainingPINAttempts();
    qWarning() << "Wrong PIN! Remaining:" << remaining;
}
```

### Key Generation and Signing

```cpp
// Initialize new keycard
Secrets secrets("123456", "123456789012", "KeycardDefaultPairing");
if (cmdSet->init(secrets)) {
    qDebug() << "Card initialized!";
}

// Generate key
QByteArray keyUID = cmdSet->generateKey();
qDebug() << "Key UID:" << keyUID.toHex();

// Sign transaction
QByteArray txHash = QByteArray::fromHex("1234..."); // 32 bytes
QByteArray signature = cmdSet->signWithPath(txHash, "m/44'/60'/0'/0/0");
qDebug() << "Signature:" << signature.toHex();
```

### BIP39 Seed Import

```cpp
// Load BIP39 seed
QByteArray seed = /* 64-byte BIP39 seed */;
QByteArray keyUID = cmdSet->loadSeed(seed);

// Derive key at path
cmdSet->deriveKey("m/44'/60'/0'/0/0");

// Export public key
QByteArray pubKey = cmdSet->exportKeyExtended(
    false,  // don't derive (already at path)
    false,  // don't make current
    QString(),
    APDU::P2ExportKeyExtendedPublic
);
```

### Auto-Pairing with Storage

```cpp
// Implement pairing storage
class MyPairingStorage : public IPairingStorage {
public:
    bool savePairing(const QString& instanceUID, 
                     const PairingInfo& pairing) override {
        // Save to database/file
        m_storage[instanceUID] = pairing;
        return true;
    }
    
    PairingInfo loadPairing(const QString& instanceUID) override {
        return m_storage.value(instanceUID);
    }
    
    bool removePairing(const QString& instanceUID) override {
        return m_storage.remove(instanceUID) > 0;
    }

private:
    QMap<QString, PairingInfo> m_storage;
};

// Create command set with auto-pairing
auto storage = std::make_shared<MyPairingStorage>();
auto passwordProvider = [](const QString& uid) {
    return "KeycardDefaultPairing";
};

auto cmdSet = new CommandSet(channel, storage, passwordProvider);

// Just use it - pairing handled automatically!
cmdSet->verifyPIN("123456");
auto signature = cmdSet->sign(hash);
```

### Custom Backend (Testing)

```cpp
// Create mock backend for testing
class MockBackend : public KeycardChannelBackend {
    // Implement interface for testing
};

// Use mock backend
auto mockBackend = new MockBackend();
auto channel = new KeycardChannel(mockBackend);

// Test without real hardware
// ...
```

---

## Constants Reference

### APDU Instructions

```cpp
namespace Keycard::APDU {
    constexpr uint8_t INS_SELECT = 0xA4;
    constexpr uint8_t INS_INIT = 0xFE;
    constexpr uint8_t INS_PAIR = 0x12;
    constexpr uint8_t INS_UNPAIR = 0x13;
    constexpr uint8_t INS_OPEN_SECURE_CHANNEL = 0x10;
    constexpr uint8_t INS_VERIFY_PIN = 0x20;
    constexpr uint8_t INS_SIGN = 0xC0;
    constexpr uint8_t INS_GENERATE_KEY = 0xD4;
    constexpr uint8_t INS_DERIVE_KEY = 0xD1;
    constexpr uint8_t INS_EXPORT_KEY = 0xC2;
    // ... see types.h for complete list
}
```

### Export Key Types

```cpp
constexpr uint8_t P2ExportKeyPrivateAndPublic = 0x00;  // Both keys
constexpr uint8_t P2ExportKeyPublicOnly = 0x01;        // Public only
constexpr uint8_t P2ExportKeyExtendedPublic = 0x02;    // Public + chain code
```

### Data Storage Types

```cpp
constexpr uint8_t P1StoreDataPublic = 0x00;  // Public data
constexpr uint8_t P1StoreDataNDEF = 0x01;    // NDEF data
constexpr uint8_t P1StoreDataCash = 0x02;    // Cash data
```

---

## Best Practices

### Security

1. **Always check remaining attempts before verifyPIN:**
   ```cpp
   ApplicationStatus status = cmdSet->getStatus();
   if (status.pinRetryCount < 2) {
       qWarning() << "Only" << status.pinRetryCount << "attempts left!";
       // Ask user to confirm
   }
   ```

2. **Clear sensitive data after use:**
   ```cpp
   cmdSet->clearAuthenticationCache();
   ```

3. **Handle card swap:**
   ```cpp
   connect(channel, &KeycardChannel::targetDetected, [cmdSet](const QString& uid) {
       if (uid != expectedUID) {
           cmdSet->handleCardSwap();
       }
   });
   ```

### Error Handling

1. **Always check return values:**
   ```cpp
   if (!cmdSet->verifyPIN(pin)) {
       QString error = cmdSet->lastError();
       // Handle error
   }
   ```

2. **Use try-catch for transmit:**
   ```cpp
   try {
       response = channel->transmit(apdu);
   } catch (const std::exception& e) {
       // Handle communication error
   }
   ```

### Platform Adaptation

1. **Check backend capabilities:**
   ```cpp
   QString backend = channel->backendName();
   if (backend == "Qt NFC (iOS)") {
       // Use iOS-specific features
   }
   ```

2. **Handle platform-specific timeouts:**
   ```cpp
   #ifdef Q_OS_IOS
       cmdSet->setDefaultWaitTimeout(30000);  // 30s for iOS
   #else
       cmdSet->setDefaultWaitTimeout(60000);  // 60s for desktop
   #endif
   ```

---

## See Also

- [Main README](../README.md) - Project overview and build instructions
- [iOS NFC Quick Start](IOS_QUICK_START.md) - iOS-specific NFC setup
- [Porting Guide](PORTING_GUIDE.md) - Porting from keycard-go
- [Qt NFC Documentation](https://doc.qt.io/qt-6/qtnfc-index.html)
- [Keycard Specification](https://keycard.tech/)

---

*Generated from keycard-qt source code. For the latest version, see [GitHub](https://github.com/status-im/keycard-qt).*

