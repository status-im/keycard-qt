/**
 * Simple example showing keycard detection using Qt NFC unified backend
 * 
 * This works on:
 * - Desktop: PC/SC card readers (Linux, macOS, Windows)
 * - Mobile: NFC (Android, iOS)
 */

#include <QCoreApplication>
#include <QDebug>
#include "keycard-qt/keycard_channel.h"
#include "keycard-qt/apdu/command.h"
#include "keycard-qt/apdu/response.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    
    qDebug() << "=== Keycard Detection Example ===";
    qDebug() << "Using Qt NFC unified backend";
    qDebug() << "Supports: PC/SC (desktop) and NFC (mobile)";
    qDebug() << "";
    
    // Create channel
    Keycard::KeycardChannel channel;
    
    // Connect signals
    QObject::connect(&channel, &Keycard::KeycardChannel::targetDetected,
        [](const QString& uid) {
        qDebug() << "✅ Card detected! UID:" << uid;
        qDebug() << "Card is ready for APDU communication";
    });
    
    QObject::connect(&channel, &Keycard::KeycardChannel::targetLost,
        []() {
        qDebug() << "❌ Card removed";
    });
    
    QObject::connect(&channel, &Keycard::KeycardChannel::error,
        [](const QString& msg) {
        qWarning() << "⚠️  Error:" << msg;
    });
    
    // Start detection
    qDebug() << "Starting card detection...";
    qDebug() << "Please insert/tap your keycard...";
    qDebug() << "";
    channel.startDetection();
    
    return app.exec();
}

