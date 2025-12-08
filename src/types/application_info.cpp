#include "keycard-qt/types.h"
#include "keycard-qt/types_parser.h"
#include "keycard-qt/apdu/utils.h"
#include <QDebug>

namespace Keycard {

namespace {

// TLV Tags
constexpr uint8_t TAG_SELECT_RESPONSE_PRE_INITIALIZED = 0x80;
constexpr uint8_t TAG_APPLICATION_INFO_TEMPLATE = 0xA4;
constexpr uint8_t TAG_INSTANCE_UID = 0x8F;
constexpr uint8_t TAG_PUBLIC_KEY = 0x80;
constexpr uint8_t TAG_VERSION = 0x02;
constexpr uint8_t TAG_KEY_UID = 0x8E;
constexpr uint8_t TAG_CAPABILITIES = 0x8D;

/**
 * @brief Find a TLV tag in data
 * @param data Data to search
 * @param tag Tag to find
 * @param value Output value
 * @return true if found
 */
bool findTag(const QByteArray& data, uint8_t tag, QByteArray& value) {
    int i = 0;
    while (i < data.size()) {
        uint8_t currentTag = static_cast<uint8_t>(data[i++]);
        
        if (i >= data.size()) break;
        
        uint8_t length = static_cast<uint8_t>(data[i++]);
        
        // Handle extended length (TODO: implement properly)
        if (length == 0x81 && i < data.size()) {
            length = static_cast<uint8_t>(data[i++]);
        } else if (length == 0x82 && i + 1 < data.size()) {
            length = (static_cast<uint8_t>(data[i]) << 8) | static_cast<uint8_t>(data[i+1]);
            i += 2;
        }
        
        if (i + length > data.size()) break;
        
        if (currentTag == tag) {
            value = data.mid(i, length);
            return true;
        }
        
        i += length;
    }
    
    return false;
}

/**
 * @brief Find Nth occurrence of a tag in nested TLV
 * @param data Data to search
 * @param n Which occurrence (0-based)
 * @param parentTag Parent tag
 * @param childTag Child tag
 * @param value Output value
 * @return true if found
 */
bool findTagN(const QByteArray& data, int n, uint8_t parentTag, uint8_t childTag, QByteArray& value) {
    QByteArray parentData;
    if (!findTag(data, parentTag, parentData)) {
        return false;
    }
    
    int count = 0;
    int i = 0;
    while (i < parentData.size()) {
        uint8_t currentTag = static_cast<uint8_t>(parentData[i++]);
        
        if (i >= parentData.size()) break;
        
        uint8_t length = static_cast<uint8_t>(parentData[i++]);
        
        if (length == 0x81 && i < parentData.size()) {
            length = static_cast<uint8_t>(parentData[i++]);
        } else if (length == 0x82 && i + 1 < parentData.size()) {
            length = (static_cast<uint8_t>(parentData[i]) << 8) | static_cast<uint8_t>(parentData[i+1]);
            i += 2;
        }
        
        if (i + length > parentData.size()) break;
        
        if (currentTag == childTag) {
            if (count == n) {
                value = parentData.mid(i, length);
                return true;
            }
            count++;
        }
        
        i += length;
    }
    
    return false;
}

} // anonymous namespace

ApplicationInfo parseApplicationInfo(const QByteArray& data) {
    ApplicationInfo info;
    info.installed = true;
    
    if (data.isEmpty()) {
        qWarning() << "ApplicationInfo: Empty data";
        return info;
    }
    
    qDebug() << "ApplicationInfo: Parsing data:" << data.toHex();
    
    // Check if pre-initialized (card not set up yet)
    if (static_cast<uint8_t>(data[0]) == TAG_SELECT_RESPONSE_PRE_INITIALIZED) {
        qDebug() << "ApplicationInfo: Pre-initialized card detected";
        info.secureChannelPublicKey = data.mid(2);
        return info;
    }
    
    // Initialized card
    info.initialized = true;
    
    if (static_cast<uint8_t>(data[0]) != TAG_APPLICATION_INFO_TEMPLATE) {
        qWarning() << "ApplicationInfo: Wrong template tag:" << QString::number(data[0], 16);
        return info;
    }
    
    // Extract fields from TLV structure
    QByteArray value;
    
    // Instance UID (0x8F)
    if (findTagN(data, 0, TAG_APPLICATION_INFO_TEMPLATE, TAG_INSTANCE_UID, value)) {
        info.instanceUID = value;
        qDebug() << "ApplicationInfo: Instance UID:" << value.toHex();
    }
    
    // Public key (0x80)
    if (findTagN(data, 0, TAG_APPLICATION_INFO_TEMPLATE, TAG_PUBLIC_KEY, value)) {
        info.secureChannelPublicKey = value;
        qDebug() << "ApplicationInfo: Public key:" << value.toHex();
    }
    
    // Version (first 0x02 tag)
    if (findTagN(data, 0, TAG_APPLICATION_INFO_TEMPLATE, TAG_VERSION, value)) {
        if (value.size() >= 2) {
            info.appVersion = static_cast<uint8_t>(value[0]);
            info.appVersionMinor = static_cast<uint8_t>(value[1]);
            qDebug() << "ApplicationInfo: Version:" << info.appVersion << "." << info.appVersionMinor;
        }
    }
    
    // Available slots (second 0x02 tag)
    if (findTagN(data, 1, TAG_APPLICATION_INFO_TEMPLATE, TAG_VERSION, value)) {
        if (!value.isEmpty()) {
            info.availableSlots = static_cast<uint8_t>(value[0]);
            qDebug() << "ApplicationInfo: Available slots:" << info.availableSlots;
        }
    }
    
    // Key UID (0x8E)
    if (findTagN(data, 0, TAG_APPLICATION_INFO_TEMPLATE, TAG_KEY_UID, value)) {
        info.keyUID = value;
        qDebug() << "ApplicationInfo: Key UID:" << value.toHex();
    }
    
    // Capabilities (0x8D) - if not present, assume all
    // Note: We're not implementing this fully yet

    return info;
}

} // namespace Keycard

