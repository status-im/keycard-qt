# keycard-qt API Reference

Complete API documentation for keycard-qt library.

## Table of Contents

- [Overview](#overview)
- [Architecture](#architecture)
- [Core Classes](#core-classes)
  - [CommunicationManager](#communicationmanager) ⭐
  - [CommandSet](#commandset)
  - [KeycardChannel](#keycardchannel)
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

1. **CommunicationManager** - Thread-safe, queue-based API for all card operations (✨ **Recommended**)
2. **CommandSet** - High-level keycard operations (pairing, signing, key management)
3. **SecureChannel** - ECDH key exchange + AES-CBC encryption
4. **KeycardChannel** - Platform-adaptive communication layer
5. **Backend** - Platform-specific implementations (PC/SC for desktop, Qt NFC for mobile)

All classes are in the `Keycard` namespace.

### Which API Should I Use?

- **Use `CommunicationManager`** (recommended) for:
  - Production applications
  - Multi-threaded environments
  - When you need both async and sync APIs
  - Stable, reliable card communication

- **Use `CommandSet` directly** for:
  - Simple single-threaded tools
  - Testing and debugging
  - Quick prototypes
  - Maximum control over the communication flow

---

## Architecture

```
┌─────────────────────────────────────┐
│ CommunicationManager                │
│ • Thread-safe queue-based API       │
│ • Async (signal) + Sync (blocking)  │
│ • Single communication thread       │
│ • Command queue serialization       │
└──────────────┬──────────────────────┘
               │
┌──────────────▼──────────────────────┐
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

### CommunicationManager

**Header:** `keycard-qt/communication_manager.h`

Thread-safe, queue-based API for managing card communication. This is the **recommended** entry point for production applications. All card operations are serialized through a dedicated communication thread, preventing race conditions and ensuring reliable operation.

#### Architecture

`CommunicationManager` uses a queue-based architecture:
- Single dedicated communication thread
- Command queue for serialized execution
- Both async (signal-based) and sync (blocking) APIs
- Automatic card detection and initialization
- Thread-safe from any thread

#### Constructor

```cpp
explicit CommunicationManager(QObject* parent = nullptr);
```

#### Initialization

```cpp
// Initialize with CommandSet
void init(std::shared_ptr<CommandSet> commandSet);

// Start card detection
void startDetection();

// Stop card detection (but keep communication thread alive)
void stopDetection();

// Stop everything (cleanup)
void stop();
```

#### Asynchronous API (Signal-Based)

```cpp
// Enqueue a command for async execution
QUuid enqueueCommand(std::unique_ptr<CardCommand> cmd);

// Signal: Command completed
void commandCompleted(QUuid token, CommandResult result);
```

**Example:**
```cpp
auto cmd = std::make_unique<SelectCommand>();
QUuid token = commManager->enqueueCommand(std::move(cmd));

connect(commManager.get(), &CommunicationManager::commandCompleted,
        [token](QUuid completedToken, CommandResult result) {
    if (completedToken == token) {
        if (result.success) {
            qDebug() << "Command succeeded!";
        } else {
            qWarning() << "Command failed:" << result.error;
        }
    }
});
```

#### Synchronous API (Blocking)

```cpp
// Execute command synchronously (blocks until complete)
CommandResult executeCommandSync(std::unique_ptr<CardCommand> cmd, 
                                  int timeoutMs = -1);
```

**Example:**
```cpp
auto cmd = std::make_unique<VerifyPINCommand>("123456");
CommandResult result = commManager->executeCommandSync(std::move(cmd), 5000);

if (result.success) {
    qDebug() << "PIN verified!";
} else {
    qWarning() << "Error:" << result.error;
}
```

#### Card Lifecycle Signals

```cpp
// Card detected and initialized
void cardInitialized(CardInitializationResult result);

// Card removed
void cardLost();

// State changed
void stateChanged(CommunicationManager::State newState);
```

#### States

```cpp
enum class State {
    Idle,                  // Not running
    DetectingCard,         // Waiting for card
    InitializingCard,      // Initializing card
    CardReady,             // Card ready for operations
    ExecutingCommand,      // Executing command
    Error                  // Error state
};
```

#### Data Types

**`CardInitializationResult`:**
```cpp
struct CardInitializationResult {
    bool success;                      // Initialization succeeded?
    QString error;                     // Error message (if failed)
    QString uid;                       // Card UID
    ApplicationInfo appInfo;           // Application info
    ApplicationStatus appStatus;       // Application status
};
```

**`CommandResult`:**
```cpp
struct CommandResult {
    bool success;           // Command succeeded?
    QString error;          // Error message (if failed)
    QVariant data;          // Command-specific result data
    QByteArray rawResponse; // Raw APDU response
};
```

#### Available Commands

All commands inherit from `CardCommand` base class. Available commands:

**Basic Commands:**
- `SelectCommand` - Select Keycard applet
- `VerifyPINCommand` - Verify PIN
- `ChangePINCommand` - Change PIN
- `ChangePUKCommand` - Change PUK
- `UnblockPINCommand` - Unblock PIN with PUK

**Key Management:**
- `InitCommand` - Initialize new card
- `GenerateKeyCommand` - Generate key pair
- `LoadSeedCommand` - Load BIP39 seed
- `DeriveKeyCommand` - Derive key at path
- `RemoveKeyCommand` - Remove key
- `ExportKeyCommand` - Export public key
- `GenerateMnemonicCommand` - Generate BIP39 mnemonic

**Signing:**
- `SignCommand` - Sign with current key
- `SignWithPathCommand` - Sign with key at path
- `SetPinlessPathCommand` - Set path for pinless signing

**Other:**
- `PairCommand` - Pair with card
- `UnpairCommand` - Unpair slot
- `GetStatusCommand` - Get application status
- `FactoryResetCommand` - Factory reset (⚠️ ERASES ALL DATA)
- `StoreDataCommand` - Store data on card
- `GetDataCommand` - Get data from card

#### Complete Example

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

// 2. Create CommunicationManager
auto commManager = std::make_shared<CommunicationManager>();
commManager->init(cmdSet);

// 3. Connect signals
connect(commManager.get(), &CommunicationManager::cardInitialized,
        [commManager](CardInitializationResult result) {
    if (result.success) {
        qDebug() << "Card ready! UID:" << result.uid;
        
        // Execute operations
        auto cmd = std::make_unique<VerifyPINCommand>("123456");
        CommandResult result = commManager->executeCommandSync(std::move(cmd));
        
        if (result.success) {
            qDebug() << "PIN verified!";
        }
    }
});

connect(commManager.get(), &CommunicationManager::cardLost, []() {
    qDebug() << "Card removed";
});

// 4. Start detection
commManager->startDetection();
```

#### Thread Safety Notes

- `CommunicationManager` is **fully thread-safe**
- Can call `enqueueCommand()` and `executeCommandSync()` from any thread
- Signals are emitted from the communication thread (use `Qt::QueuedConnection` for cross-thread slots)
- All card I/O happens on a single dedicated thread (prevents race conditions)

#### Lifecycle Management

```cpp
// Typical lifecycle:
commManager->init(cmdSet);           // 1. Initialize
commManager->startDetection();       // 2. Start detection
// ... use card ...
commManager->stopDetection();        // 3. Stop detection (keep thread alive)
// ... can restart detection later ...
commManager->stop();                 // 4. Full cleanup (destructor calls this too)
```

---

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

### Using CommunicationManager (Recommended - Thread-Safe)

`CommunicationManager` is **fully thread-safe** and handles all threading complexity for you:

```cpp
// ✅ Recommended: Thread-safe from any thread
auto commManager = std::make_shared<CommunicationManager>();
commManager->init(cmdSet);
commManager->startDetection();

// Can call from any thread!
auto cmd = std::make_unique<VerifyPINCommand>("123456");
CommandResult result = commManager->executeCommandSync(std::move(cmd));

// Or async from any thread
QUuid token = commManager->enqueueCommand(std::move(anotherCmd));
```

**Benefits:**
- No manual thread management needed
- All card I/O serialized on dedicated thread
- No race conditions
- Both async and sync APIs available
- Safe to call from multiple threads simultaneously

### Direct API Threading (Advanced)

If using `CommandSet` and `KeycardChannel` directly (not recommended for production):

#### Main Thread Usage

All Qt signal/slot operations must happen on the main thread:

```cpp
// ⚠️ Direct API: Use from main thread
auto channel = new KeycardChannel(this);
connect(channel, &KeycardChannel::targetDetected, this, &MyClass::onCardDetected);
channel->startDetection();
```

#### Worker Thread Usage

For blocking operations, use worker threads:

```cpp
// ⚠️ Direct API: Blocking operations in worker thread
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

### Thread Safety Summary

| Class | Thread Safety | Notes |
|-------|---------------|-------|
| **CommunicationManager** | ✅ **Fully thread-safe** | Recommended for production use |
| **CommandSet** | ⚠️ Single-threaded | Use from one thread only |
| **SecureChannel** | ✅ Thread-safe | Uses internal mutex |
| **KeycardChannel** | ⚠️ Main thread only | Qt signal/slot constraints |

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

For complete, runnable examples, see the [Examples Guide](../EXAMPLES_GUIDE.md).

### Using CommunicationManager (Recommended)

#### Basic Card Detection and Operations

```cpp
#include <keycard-qt/communication_manager.h>
#include <keycard-qt/command_set.h>
#include <keycard-qt/keycard_channel.h>
#include <keycard-qt/pairing_storage.h>

using namespace Keycard;

// Setup
auto channel = std::make_shared<KeycardChannel>();
auto pairingStorage = std::make_shared<FilePairingStorage>();
auto passwordProvider = [](const QString& uid) { return "KeycardDefaultPairing"; };
auto cmdSet = std::make_shared<CommandSet>(channel, pairingStorage, passwordProvider);

auto commManager = std::make_shared<CommunicationManager>();
commManager->init(cmdSet);

// Connect signals
connect(commManager.get(), &CommunicationManager::cardInitialized,
        [](CardInitializationResult result) {
    if (result.success) {
        qDebug() << "Card ready! UID:" << result.uid;
    }
});

connect(commManager.get(), &CommunicationManager::cardLost, []() {
    qDebug() << "Card removed";
});

// Start detection
commManager->startDetection();
```

#### Synchronous Operations

```cpp
// Verify PIN (blocks until complete, but thread-safe!)
auto verifyCmd = std::make_unique<VerifyPINCommand>("123456");
CommandResult result = commManager->executeCommandSync(std::move(verifyCmd), 5000);

if (result.success) {
    qDebug() << "PIN verified!";
    
    // Sign a transaction
    QByteArray hash = /* 32-byte hash */;
    auto signCmd = std::make_unique<SignCommand>(hash, "m/44'/60'/0'/0/0");
    CommandResult signResult = commManager->executeCommandSync(std::move(signCmd), 30000);
    
    if (signResult.success) {
        QByteArray signature = signResult.data.toMap()["signature"].toByteArray();
        qDebug() << "Signature:" << signature.toHex();
    }
}
```

#### Asynchronous Operations

```cpp
// Enqueue multiple commands
auto cmd1 = std::make_unique<SelectCommand>();
auto cmd2 = std::make_unique<VerifyPINCommand>("123456");

QUuid token1 = commManager->enqueueCommand(std::move(cmd1));
QUuid token2 = commManager->enqueueCommand(std::move(cmd2));

// Handle completion
connect(commManager.get(), &CommunicationManager::commandCompleted,
        [token1, token2](QUuid token, CommandResult result) {
    if (token == token1) {
        qDebug() << "Select completed:" << result.success;
    } else if (token == token2) {
        qDebug() << "Verify PIN completed:" << result.success;
    }
});
```

### Using Direct API (Advanced)

#### Basic Card Detection

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

#### Complete Key Generation and Signing Flow

```cpp
// Wait for card
connect(commManager.get(), &CommunicationManager::cardInitialized,
        [commManager](CardInitializationResult result) {
    if (!result.success) {
        qWarning() << "Card init failed:" << result.error;
        return;
    }
    
    if (!result.appInfo.initialized) {
        // Initialize new card
        auto initCmd = std::make_unique<InitCommand>("123456", "123456789012", "KeycardDefaultPairing");
        CommandResult initResult = commManager->executeCommandSync(std::move(initCmd), 60000);
        
        if (!initResult.success) {
            qWarning() << "Init failed:" << initResult.error;
            return;
        }
        
        qDebug() << "Card initialized!";
    }
    
    // Verify PIN
    auto verifyCmd = std::make_unique<VerifyPINCommand>("123456");
    CommandResult verifyResult = commManager->executeCommandSync(std::move(verifyCmd));
    
    if (!verifyResult.success) {
        qWarning() << "PIN verification failed:" << verifyResult.error;
        return;
    }
    
    // Generate key
    auto genKeyCmd = std::make_unique<GenerateKeyCommand>();
    CommandResult keyResult = commManager->executeCommandSync(std::move(genKeyCmd));
    
    if (keyResult.success) {
        QByteArray keyUID = keyResult.data.toMap()["keyUID"].toByteArray();
        qDebug() << "Key UID:" << keyUID.toHex();
        
        // Sign transaction
        QByteArray txHash = /* 32-byte hash */;
        auto signCmd = std::make_unique<SignCommand>(txHash, "m/44'/60'/0'/0/0");
        CommandResult signResult = commManager->executeCommandSync(std::move(signCmd));
        
        if (signResult.success) {
            QByteArray signature = signResult.data.toMap()["signature"].toByteArray();
            qDebug() << "Signature:" << signature.toHex();
        }
    }
});
```

### Using Direct API (Advanced)

#### Complete Pairing Flow

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

#### Key Generation and Signing

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

### Architecture

1. **Use CommunicationManager for production:**
   ```cpp
   // ✅ Recommended: Thread-safe, robust
   auto commManager = std::make_shared<CommunicationManager>();
   commManager->init(cmdSet);
   ```

2. **Direct API only for simple cases:**
   ```cpp
   // ⚠️ Only for: prototypes, tests, or simple single-threaded tools
   auto cmdSet = std::make_shared<CommandSet>(channel, storage, provider);
   ```

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
- [Examples Guide](../EXAMPLES_GUIDE.md) - Detailed examples with CommunicationManager
- [Qt NFC Documentation](https://doc.qt.io/qt-6/qtnfc-index.html)
- [Keycard Specification](https://keycard.tech/)

---

*Generated from keycard-qt source code. For the latest version, see [GitHub](https://github.com/status-im/keycard-qt).*

