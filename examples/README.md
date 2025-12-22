# Keycard-Qt Examples

Examples demonstrating the keycard-qt library, from basic card detection to advanced operations.

## Quick Start

**Build all examples:**
```bash
mkdir -p build && cd build
cmake .. -DBUILD_EXAMPLES=ON
cmake --build . --target all
```

## Available Examples

### 1. simple_detection.cpp
**Level**: Beginner  
**Status**: ✅ Working

Basic example showing low-level card detection using KeycardChannel.

**Run:**
```bash
./examples/simple_detection
```

**What it demonstrates:**
- KeycardChannel initialization
- Card detection signals (targetDetected, targetLost)
- Platform-independent backend (PC/SC on desktop, NFC on mobile)

**Use case**: Understanding the low-level channel API

---

### 2. communication_manager_example.cpp ⭐ RECOMMENDED
**Level**: Beginner  
**Status**: ✅ Working

**This is the recommended starting point for new applications.**

Shows how to use `CommunicationManager`, the main thread-safe API for keycard-qt.

**Run:**
```bash
./examples/communication_manager_example
```

**What it demonstrates:**
- Setting up CommunicationManager with CommandSet
- **Async API**: `enqueueCommand()` for non-blocking operations
- **Sync API**: `executeCommandSync()` for blocking operations
- Handling card lifecycle events (cardInitialized, cardLost)
- Command completion signals
- State management

**Use case**: 
- UI applications (use async API)
- Worker threads (use sync API)
- Any production application

---

### 3. advanced_operations_example.cpp
**Level**: Advanced  
**Status**: ✅ Working

Comprehensive example showing real-world keycard operations.

**Run:**
```bash
./examples/advanced_operations_example
```

**What it demonstrates:**
- Card initialization with PIN/PUK/pairing password
- PIN verification
- Key generation (mnemonic)
- Transaction signing
- Batch operations (multiple commands without closing channel)
- Error handling and retry logic

**Use case**: Production applications with full keycard functionality

---

## Architecture Overview

```
┌─────────────────────────────────────┐
│    Your Application                 │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│  CommunicationManager               │  ← Main API (thread-safe)
│  • enqueueCommand() - async         │
│  • executeCommandSync() - blocking  │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│  CommandSet                         │
│  • Card operations                  │
│  • Secure channel management        │
└──────────────┬──────────────────────┘
               │
               ▼
┌─────────────────────────────────────┐
│  KeycardChannel                     │
│  • Platform-specific backend        │
│  • PC/SC (desktop) or NFC (mobile)  │
└─────────────────────────────────────┘
```

## API Design

### CommunicationManager (Recommended)

**Thread-safe, queue-based architecture:**

```cpp
// Create and initialize
auto channel = std::make_shared<KeycardChannel>();
auto cmdSet = std::make_shared<CommandSet>(channel, nullptr, nullptr);
auto commMgr = std::make_shared<CommunicationManager>();
commMgr->init(cmdSet);

// Start detection
commMgr->startDetection();

// Async API (for UI thread)
auto cmd = std::make_unique<SelectCommand>();
QUuid token = commMgr->enqueueCommand(std::move(cmd));
// Result via commandCompleted signal

// Sync API (for worker threads)
auto cmd = std::make_unique<SelectCommand>();
CommandResult result = commMgr->executeCommandSync(std::move(cmd), 5000);
```

### Dependency Injection

CommandSet uses dependency injection for flexibility:

```cpp
auto channel = std::make_shared<KeycardChannel>();
auto storage = std::make_shared<FilePairingStorage>("/path/to/pairings");
auto passwordProvider = [](const QString& cardUID) {
    return "KeycardDefaultPairing";
};

auto cmdSet = std::make_shared<CommandSet>(
    channel,
    storage,          // or nullptr
    passwordProvider  // or nullptr
);
```

## For More Complex Integration

For a full application example with:
- JSON-RPC interface
- Session management  
- Signal handling
- C API wrapper

See: **status-keycard-qt** library (wraps keycard-qt with higher-level API)

## Testing

All examples are tested. Unit tests using these patterns can be found in:
- `tests/test_communication_manager*.cpp`
- `tests/test_command_set*.cpp`
