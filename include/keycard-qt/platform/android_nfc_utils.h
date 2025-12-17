// Copyright (C) 2025 Status Research & Development GmbH
// SPDX-License-Identifier: MIT

#pragma once

#ifdef Q_OS_ANDROID

#include <QJniObject>
#include <QDebug>

namespace Keycard {
namespace Platform {

/**
 * @brief Enable extended NFC timeout on Android for long operations
 * 
 * Call this before operations that might take longer than the default
 * Android NFC timeout (~2-3 seconds), such as GlobalPlatform operations.
 * 
 * This temporarily enables Android Foreground Dispatch which intercepts
 * NFC tags and sets IsoDep timeout to 10 seconds.
 * 
 * @note Must be paired with disableExtendedNfcTimeout() after operation
 */
inline void enableExtendedNfcTimeout()
{
    try {
        QJniObject::callStaticMethod<void>(
            "app/status/mobile/StatusQtActivity",
            "enableExtendedNfcTimeout",
            "(Z)V",
            true);
        qDebug() << "Android: Extended NFC timeout enabled (10s)";
    } catch (...) {
        qWarning() << "Android: Failed to enable extended NFC timeout";
    }
}

/**
 * @brief Disable extended NFC timeout on Android
 * 
 * Call this after long operations complete to restore normal NFC handling.
 * This disables Foreground Dispatch and allows Qt NFC to work normally.
 */
inline void disableExtendedNfcTimeout()
{
    try {
        QJniObject::callStaticMethod<void>(
            "app/status/mobile/StatusQtActivity",
            "enableExtendedNfcTimeout",
            "(Z)V",
            false);
        qDebug() << "Android: Extended NFC timeout disabled";
    } catch (...) {
        qWarning() << "Android: Failed to disable extended NFC timeout";
    }
}

/**
 * @brief RAII helper for automatic extended NFC timeout management
 * 
 * Usage:
 * {
 *     AndroidNfcTimeoutGuard guard;
 *     // Perform long operation here
 *     // Timeout will be automatically restored when guard goes out of scope
 * }
 */
class AndroidNfcTimeoutGuard {
public:
    AndroidNfcTimeoutGuard() {
        enableExtendedNfcTimeout();
    }
    
    ~AndroidNfcTimeoutGuard() {
        disableExtendedNfcTimeout();
    }
    
    // Non-copyable, non-movable
    AndroidNfcTimeoutGuard(const AndroidNfcTimeoutGuard&) = delete;
    AndroidNfcTimeoutGuard& operator=(const AndroidNfcTimeoutGuard&) = delete;
};

} // namespace Platform
} // namespace Keycard

#else // !Q_OS_ANDROID

// No-op implementations for non-Android platforms
namespace Keycard {
namespace Platform {

inline void enableExtendedNfcTimeout() {}
inline void disableExtendedNfcTimeout() {}

class AndroidNfcTimeoutGuard {
public:
    AndroidNfcTimeoutGuard() {}
    ~AndroidNfcTimeoutGuard() {}
};

} // namespace Platform
} // namespace Keycard

#endif // Q_OS_ANDROID



