# Quick Start: Queue-Based Architecture

## 🎯 What Was Done

Implemented a **queue-based architecture** to eliminate the race condition in keycard communication.

**Status:** ✅ **Core infrastructure complete** (Phase 2)

---

## 📁 What Files Were Created

```
keycard-qt-status/
├── include/keycard-qt/
│   ├── card_command.h              ← Command Pattern interface
│   └── communication_manager.h     ← Queue dispatcher
├── src/
│   ├── card_command.cpp            ← Command implementations
│   └── communication_manager.cpp   ← Manager implementation
└── Documentation:
    ├── QUEUE_ARCHITECTURE_README.md    ← Start here!
    ├── ARCHITECTURE_CHANGES.md         ← Deep dive
    ├── INTEGRATION_GUIDE.md            ← How to integrate
    ├── IMPLEMENTATION_STATUS.md        ← Status tracking
    └── QUICK_START.md                  ← This file
```

---

## 🚀 How to Use (Quick Example)

### 1. Create and Start Manager

```cpp
#include "keycard-qt/communication_manager.h"

// Create
auto manager = new Keycard::CommunicationManager(this);

// Connect signals
connect(manager, &Keycard::CommunicationManager::cardInitialized,
        this, &MyClass::onCardInit);

// Start
manager->start(channel, pairingStorage, passwordProvider);
```

### 2. Execute Commands (Async)

```cpp
// Create command
auto cmd = std::make_unique<Keycard::VerifyPINCommand>("123456");

// Enqueue
QUuid token = manager->enqueueCommand(std::move(cmd));

// Wait for result signal
connect(manager, &Keycard::CommunicationManager::commandCompleted,
        [](QUuid token, Keycard::CommandResult result) {
    if (result.success) {
        qDebug() << "Success:" << result.data;
    } else {
        qDebug() << "Error:" << result.error;
    }
});
```

### 3. Execute Commands (Sync)

```cpp
// Create and execute
auto cmd = std::make_unique<Keycard::GetStatusCommand>();
Keycard::CommandResult result = manager->executeCommandSync(std::move(cmd));

if (result.success) {
    QVariantMap status = result.data.toMap();
    int pinRetries = status["pinRetryCount"].toInt();
}
```

---

## 🏗️ Architecture Overview

### The Problem (Before)
```
Card Detected → SessionManager & CommandSet both try to initialize
                     ↓
                  RACE CONDITION!
                     ↓
              Random failures
```

### The Solution (After)
```
Card Detected
      ↓
CommunicationManager
      ↓
[Initialize Atomically]
      ↓
[Queue Commands]
      ↓
[Execute Serially]
      ↓
No races, predictable!
```

---

## 📚 Where to Read Next

1. **Want overview?** → Read `QUEUE_ARCHITECTURE_README.md`
2. **Want details?** → Read `ARCHITECTURE_CHANGES.md`  
3. **Want to integrate?** → Read `INTEGRATION_GUIDE.md`
4. **Want status?** → Read `IMPLEMENTATION_STATUS.md`

---

## ✅ What's Complete

- [x] CardCommand interface (Command Pattern)
- [x] CommunicationManager (queue dispatcher)
- [x] Communication thread (dedicated for card I/O)
- [x] Atomic initialization sequence
- [x] Both async and sync APIs
- [x] Build system updated
- [x] Comprehensive documentation

**Phase 2:** ✅ **COMPLETE**

---

## 🔄 What's Next

### Phase 3: Integration (Pending)
- [ ] Modify SessionManager to use CommunicationManager
- [ ] Add feature flag to switch architectures
- [ ] Keep backward compatibility

### Phase 4: Testing (Pending)  
- [ ] Unit tests
- [ ] Integration tests
- [ ] Stress tests

### Phase 5: Rollout (Pending)
- [ ] Canary testing
- [ ] Gradual rollout
- [ ] Monitoring

---

## 🔨 How to Build

```bash
cd keycard-qt-status
mkdir build && cd build
cmake ..
make
```

Should compile without errors!

---

## 💡 Key Concepts

### CardCommand
- Encapsulates one operation
- Implements `execute(CommandSet*)`
- Returns CommandResult

### CommunicationManager  
- Manages command queue
- Runs dedicated communication thread
- Handles card initialization
- Provides async & sync APIs

### State Machine
```
Idle → Initializing → Ready → Processing → Ready
  ↑                                           ↓
  └──────────────────────────────────────────┘
```

---

## 📊 Impact

### Before
- ❌ Race conditions
- ❌ Unpredictable failures
- ❌ Corrupted state

### After
- ✅ No races (atomic init)
- ✅ Predictable (serial execution)
- ✅ Clean state management

---

## 🎓 Learn By Example

See usage examples in:
- `ARCHITECTURE_CHANGES.md` - Multiple examples
- `INTEGRATION_GUIDE.md` - SessionManager integration
- Source code comments - Inline examples

---

## 🤝 How to Contribute

### Adding New Commands

1. **Declare in `card_command.h`:**
```cpp
class MyCommand : public CardCommand {
public:
    MyCommand(params...);
    CommandResult execute(CommandSet*) override;
    QString name() const override { return "MY_COMMAND"; }
};
```

2. **Implement in `card_command.cpp`:**
```cpp
CommandResult MyCommand::execute(CommandSet* cmdSet) {
    // Your logic here
    bool ok = cmdSet->someOperation();
    return ok ? CommandResult::fromSuccess(data)
              : CommandResult::fromError(error);
}
```

3. **Use it:**
```cpp
auto cmd = std::make_unique<MyCommand>(args);
manager->enqueueCommand(std::move(cmd));
```

---

## 🐛 Troubleshooting

### Build Errors
- Check CMakeLists.txt includes new files
- Verify Qt6 Core is available
- Check include paths

### Runtime Issues
- Enable logging: `QLoggingCategory::setFilterRules("keycard.*=true")`
- Check state transitions in logs
- Verify card detection events

### Integration Issues
- See `INTEGRATION_GUIDE.md` for step-by-step
- Use feature flag for gradual rollout
- Keep old code path for fallback

---

## 📞 Need Help?

1. Check documentation (4 comprehensive files)
2. Review source code comments
3. Compare with Go implementation
4. Ask the team!

---

## 🎉 Summary

**What:** Queue-based architecture for keycard communication  
**Why:** Eliminate race condition between SessionManager and CommandSet  
**How:** Single communication thread + command queue + atomic init  
**Status:** Core infrastructure complete, ready for integration  
**Next:** Integrate with SessionManager and test thoroughly

**Result:** No more races, predictable behavior, maintainable code!

---

**Last Updated:** December 16, 2024  
**Phase:** 2 Complete, 3 Pending  
**Status:** ✅ Ready for Integration
