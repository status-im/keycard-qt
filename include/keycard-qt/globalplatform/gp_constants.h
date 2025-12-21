#pragma once

#include <cstdint>
#include <QByteArray>

namespace Keycard {
namespace GlobalPlatform {

// GlobalPlatform CLA bytes
constexpr uint8_t CLA_ISO7816 = 0x00;
constexpr uint8_t CLA_GP = 0x80;
constexpr uint8_t CLA_MAC = 0x84;

// GlobalPlatform Instructions
constexpr uint8_t INS_SELECT = 0xA4;
constexpr uint8_t INS_INITIALIZE_UPDATE = 0x50;
constexpr uint8_t INS_EXTERNAL_AUTHENTICATE = 0x82;
constexpr uint8_t INS_DELETE = 0xE4;
constexpr uint8_t INS_INSTALL = 0xE6;
constexpr uint8_t INS_LOAD = 0xE8;
constexpr uint8_t INS_GET_STATUS = 0xF2;
constexpr uint8_t INS_GET_RESPONSE = 0xC0;

// P1 Parameters
constexpr uint8_t P1_EXTERNAL_AUTH_CMAC = 0x01;
constexpr uint8_t P1_INSTALL_FOR_LOAD = 0x02;
constexpr uint8_t P1_INSTALL_FOR_INSTALL = 0x04;
constexpr uint8_t P1_INSTALL_FOR_MAKE_SELECTABLE = 0x08;
constexpr uint8_t P1_LOAD_MORE_BLOCKS = 0x00;
constexpr uint8_t P1_LOAD_LAST_BLOCK = 0x80;
constexpr uint8_t P1_GET_STATUS_ISD = 0x80;
constexpr uint8_t P1_GET_STATUS_APPS = 0x40;

// P2 Parameters
constexpr uint8_t P2_GET_STATUS_TLV = 0x02;
constexpr uint8_t P2_DELETE_OBJECT = 0x00;
constexpr uint8_t P2_DELETE_OBJECT_AND_RELATED = 0x80;

// Status Words
constexpr uint16_t SW_OK = 0x9000;
constexpr uint16_t SW_FILE_NOT_FOUND = 0x6A82;
constexpr uint16_t SW_REFERENCED_DATA_NOT_FOUND = 0x6A88;
constexpr uint16_t SW_SECURITY_CONDITION_NOT_SATISFIED = 0x6982;
constexpr uint16_t SW_AUTH_METHOD_BLOCKED = 0x6983;
constexpr uint8_t SW1_RESPONSE_DATA_INCOMPLETE = 0x61;

// TLV Tags
constexpr uint8_t TAG_DELETE_AID = 0x4F;
constexpr uint8_t TAG_LOAD_FILE_DATA_BLOCK = 0xC4;
constexpr uint8_t TAG_GET_STATUS_AID = 0x4F;

// Keycard AIDs (from keycard-go identifiers)
// Package AID: A0 00 00 08 04 00 01 (7 bytes)
inline QByteArray PACKAGE_AID() {
    return QByteArray::fromHex("A0000008040001");  // FIXED: 7 bytes (was 8)
}

// Keycard Applet AID: A0 00 00 08 04 00 01 01 (8 bytes)
// A000000804000101
inline QByteArray KEYCARD_AID() {
    return QByteArray::fromHex("A000000804000101");  // Fixed: was 9 bytes, now correct 8 bytes
}

// Default instance index
constexpr int DEFAULT_INSTANCE_INDEX = 1;

// Keycard Instance AID: A0 00 00 08 04 00 01 01 [index] (9 bytes)
inline QByteArray KEYCARD_INSTANCE_AID(int instance = DEFAULT_INSTANCE_INDEX) {
    QByteArray aid = QByteArray::fromHex("A000000804000101");  // Fixed: 8 bytes base
    aid.append(static_cast<char>(instance));  // + 1 byte instance = 9 bytes total
    return aid;
}

// ISD (Issuer Security Domain / Card Manager) AID
// Standard GlobalPlatform ISD AID used by most cards
inline QByteArray ISD_AID() {
    return QByteArray::fromHex("A000000151000000");  // 8 bytes - standard GP Card Manager
}

// Default keys (from keycard-go identifiers)
// Keycard development key (used for development builds)
inline QByteArray KEYCARD_DEFAULT_KEY() {
    return QByteArray::fromHex("c212e073ff8b4bbfaff4de8ab655221f");
}

// GlobalPlatform default key (standard test key)
inline QByteArray GLOBALPLATFORM_DEFAULT_KEY() {
    return QByteArray::fromHex("404142434445464748494a4b4c4d4e4f");
}

} // namespace GlobalPlatform
} // namespace Keycard

