#include "keycard-qt/tlv_utils.h"
#include <QDebug>

namespace Keycard {
namespace TLV {

quint32 parseLength(const QByteArray& data, int& offset) {
    if (offset >= data.size()) {
        return 0;
    }
    
    uint8_t firstByte = static_cast<uint8_t>(data[offset]);
    offset++;
    
    if ((firstByte & 0x80) == 0) {
        // Short form: length is in the lower 7 bits
        return firstByte;
    }
    
    // Long form: lower 7 bits indicate number of length bytes
    int numLengthBytes = firstByte & 0x7F;
    if (numLengthBytes > 4 || offset + numLengthBytes > data.size()) {
        qWarning() << "TLV::parseLength: Invalid length encoding";
        return 0;
    }
    
    quint32 length = 0;
    for (int i = 0; i < numLengthBytes; i++) {
        length = (length << 8) | static_cast<uint8_t>(data[offset]);
        offset++;
    }
    
    return length;
}

QByteArray findTag(const QByteArray& data, uint8_t targetTag) {
    int offset = 0;
    
    while (offset < data.size()) {
        if (offset >= data.size()) {
            break;
        }
        
        uint8_t tag = static_cast<uint8_t>(data[offset]);
        offset++;
        
        quint32 length = parseLength(data, offset);
        if (length == 0 && offset >= data.size()) {
            break;
        }
        
        // Check if we have enough data
        if (offset + static_cast<int>(length) > data.size()) {
            qWarning() << "TLV::findTag: Length exceeds data size. Tag:"
                      << QString("0x%1").arg(tag, 2, 16, QChar('0'))
                      << "Length:" << length << "Remaining:" << (data.size() - offset);
            break;
        }
        
        // Found the target tag
        if (tag == targetTag) {
            return data.mid(offset, length);
        }
        
        // Skip to next tag
        offset += length;
    }
    
    return QByteArray();
}

QByteArray encodeLength(quint32 length) {
    QByteArray result;
    
    if (length < 128) {
        // Short form (0-127)
        result.append(static_cast<char>(length));
    } else {
        // Long form
        // Determine number of bytes needed
        int numBytes = 0;
        quint32 temp = length;
        while (temp > 0) {
            numBytes++;
            temp >>= 8;
        }
        
        // First byte: 0x80 | numBytes
        result.append(static_cast<char>(0x80 | numBytes));
        
        // Length bytes (big-endian)
        for (int i = numBytes - 1; i >= 0; i--) {
            result.append(static_cast<char>((length >> (i * 8)) & 0xFF));
        }
    }
    
    return result;
}

QByteArray encode(uint8_t tag, const QByteArray& value) {
    QByteArray result;
    result.append(static_cast<char>(tag));
    result.append(encodeLength(value.size()));
    result.append(value);
    return result;
}

} // namespace TLV
} // namespace Keycard
