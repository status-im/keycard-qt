#include "keycard-qt/types.h"
#include "keycard-qt/types_parser.h"
#include "keycard-qt/apdu/utils.h"
#include <QCryptographicHash>
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
constexpr uint8_t TAG_STATUS = 0x8C;       // applet 4.x
constexpr uint8_t TAG_CERTIFICATE = 0x8A;  // applet 4.x

constexpr uint8_t STATUS_INITIALIZED = 0x10;
constexpr uint8_t STATUS_PIN_RETRIES_MASK = 0x0F;
constexpr int IDENTITY_PUBLIC_KEY_SIZE = 33;
constexpr int INSTANCE_UID_SIZE = 16;

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

        int length = static_cast<uint8_t>(data[i++]);

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
        
        int length = static_cast<uint8_t>(parentData[i++]);

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

/**
 * @brief Let the application info template span the whole SELECT response
 *
 * Applet 4.x before 4.0.1 (status-keycard #133) writes the template length one byte short, so its
 * last TLV, the identity certificate, seems to run past the template and is dropped. A SELECT
 * response holds nothing but this template, so a declared length that stops short of the data is
 * widened to it. Correct lengths are left as they are.
 */
QByteArray withTemplateSpanningResponse(const QByteArray& response) {
    if (response.size() < 2 || static_cast<uint8_t>(response[0]) != TAG_APPLICATION_INFO_TEMPLATE) {
        return response;
    }
    int headerSize = 2;
    int declared = static_cast<uint8_t>(response[1]);
    if (declared == 0x81) {
        if (response.size() < 3) {
            return response;
        }
        declared = static_cast<uint8_t>(response[2]);
        headerSize = 3;
    } else if (declared > 0x80) {
        return response;
    }

    const int actual = response.size() - headerSize;
    if (declared >= actual || actual > 0xFF) {
        return response;
    }
    qWarning() << "ApplicationInfo: Template length" << declared << "shorter than the" << actual
               << "bytes received (applet before 4.0.1), using the whole response";
    QByteArray fixed(1, static_cast<char>(TAG_APPLICATION_INFO_TEMPLATE));
    if (actual > 0x7F) {
        fixed.append(static_cast<char>(0x81));
    }
    fixed.append(static_cast<char>(actual));
    fixed.append(response.mid(headerSize));
    return fixed;
}

} // anonymous namespace

ApplicationInfo parseApplicationInfo(const QByteArray& response) {
    const QByteArray data = withTemplateSpanningResponse(response);
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
        if (!findTag(data, TAG_SELECT_RESPONSE_PRE_INITIALIZED, info.secureChannelPublicKey)) {
            info.secureChannelPublicKey = data.mid(2);
        }
        return info;
    }

    // Applet 3.x answers with the template only once initialized; applet 4.x always does
    // and reports initialization in the status tag (handled below).
    info.initialized = true;

    if (static_cast<uint8_t>(data[0]) != TAG_APPLICATION_INFO_TEMPLATE) {
        qWarning() << "ApplicationInfo: Wrong template tag:" << QString::number(data[0], 16);
        return info;
    }

    // Extract fields from TLV structure
    QByteArray value;

    // Applet 4.x: SecureChannelV2, identity certificate instead of instance UID / SC key / slots
    if (findTagN(data, 0, TAG_APPLICATION_INFO_TEMPLATE, TAG_VERSION, value)
        && value.size() >= 2 && static_cast<uint8_t>(value[0]) >= 4) {
        info.appVersion = static_cast<uint8_t>(value[0]);
        info.appVersionMinor = static_cast<uint8_t>(value[1]);
        info.secureChannelVersion = 2;
        info.availableSlots = 0;

        if (findTagN(data, 0, TAG_APPLICATION_INFO_TEMPLATE, TAG_STATUS, value) && !value.isEmpty()) {
            info.appStatus = static_cast<uint8_t>(value[0]);
        }
        info.initialized = (info.appStatus & STATUS_INITIALIZED) != 0;
        if (info.initialized) {
            info.pinRetries = info.appStatus & STATUS_PIN_RETRIES_MASK;
        }

        if (findTagN(data, 0, TAG_APPLICATION_INFO_TEMPLATE, TAG_KEY_UID, value)) {
            info.keyUID = value;
        }
        if (findTagN(data, 0, TAG_APPLICATION_INFO_TEMPLATE, TAG_CAPABILITIES, value) && !value.isEmpty()) {
            info.capabilities = static_cast<uint8_t>(value[0]);
        }
        if (findTagN(data, 0, TAG_APPLICATION_INFO_TEMPLATE, TAG_CERTIFICATE, value)
            && value.size() > IDENTITY_PUBLIC_KEY_SIZE) {
            info.certificate = value;
            info.identityPublicKey = value.left(IDENTITY_PUBLIC_KEY_SIZE);
            // No instance UID on 4.x: derive a stable 16-byte one from the identity key, so callers
            // keyed on the instance UID keep working and it survives factory reset.
            info.instanceUID = QCryptographicHash::hash(info.identityPublicKey, QCryptographicHash::Sha256)
                                   .left(INSTANCE_UID_SIZE);
        }

        qDebug() << "ApplicationInfo: Applet" << info.appVersion << "." << info.appVersionMinor
                 << "(SecureChannelV2) initialized:" << info.initialized
                 << "instance UID:" << info.instanceUID.toHex();
        return info;
    }

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
    
    // Capabilities (0x8D) - if not present, assume all capabilities are supported
    if (findTagN(data, 0, TAG_APPLICATION_INFO_TEMPLATE, TAG_CAPABILITIES, value)) {
        if (!value.isEmpty()) {
            info.capabilities = static_cast<uint8_t>(value[0]);
            qDebug() << "ApplicationInfo: Capabilities:" << QString::number(info.capabilities, 16);
        } else {
            info.capabilities = static_cast<uint8_t>(Capability::All);
        }
    } else {
        // No capabilities tag present - assume all capabilities (for older cards)
        info.capabilities = static_cast<uint8_t>(Capability::All);
        qDebug() << "ApplicationInfo: No capabilities tag, assuming all capabilities";
    }

    return info;
}

} // namespace Keycard

