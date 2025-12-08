# Keycard-Qt Examples

## Available Example

### simple_detection.cpp ✅
**Status**: Working

Basic example showing card detection using the unified Qt backend (PC/SC on desktop, NFC on mobile).

**Build:**
```bash
mkdir -p build && cd build
cmake .. -DBUILD_EXAMPLES=ON
cmake --build . --target simple_detection
```

**Run:**
```bash
./examples/simple_detection
```

**What it does:**
- Initializes the keycard channel with default platform backend
- Starts card detection
- Shows signals when cards are inserted/removed
- Demonstrates basic channel usage

## Complete API Example

For a comprehensive example showing the full API including:
- JSON-RPC calls
- Signal handling
- Session management
- Pairing and secure channel

See: **status-keycard-qt/examples/simple_usage.cpp**

This example uses the higher-level status-keycard-qt library which provides a simpler C API wrapping keycard-qt.

## API Changes

The old examples were removed because `CommandSet` now uses dependency injection:

**Current API:**
```cpp
auto channel = std::make_shared<Keycard::KeycardChannel>();
auto storage = std::make_shared<FilePairingStorage>();
auto passwordProvider = [](const QString& cardUID) { 
    return "KeycardDefaultPairing"; 
};
auto cmdSet = std::make_shared<Keycard::CommandSet>(
    channel, storage, passwordProvider
);
```

For new examples using `CommandSet`, refer to the test files in `tests/` directory.
