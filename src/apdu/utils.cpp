#include "keycard-qt/apdu/utils.h"
#include <QDebug>

namespace Keycard {
namespace APDU {

QByteArray Utils::fromHex(const QString& hex)
{
    QString cleaned = hex;
    cleaned.remove(QChar(' '));
    cleaned.remove(QChar(':'));
    cleaned.remove(QChar('-'));
    return QByteArray::fromHex(cleaned.toLatin1());
}

QString Utils::toHex(const QByteArray& data, bool uppercase)
{
    QString hex = QString::fromLatin1(data.toHex());
    return uppercase ? hex.toUpper() : hex;
}

QByteArray Utils::encodeTLV(uint8_t tag, const QByteArray& value)
{
    QByteArray result;
    result.append(static_cast<char>(tag));
    result.append(encodeLength(value.size()));
    result.append(value);
    return result;
}

bool Utils::decodeTLV(const QByteArray& tlv, uint8_t& tag, QByteArray& value)
{
    if (tlv.isEmpty()) {
        return false;
    }
    
    int offset = 0;
    tag = static_cast<uint8_t>(tlv[offset++]);
    
    if (offset >= tlv.size()) {
        return false;
    }
    
    int length = parseLength(tlv.mid(offset), offset);
    if (length < 0 || offset + length > tlv.size()) {
        return false;
    }
    
    value = tlv.mid(offset, length);
    return true;
}

int Utils::parseLength(const QByteArray& data, int& offset)
{
    if (data.isEmpty()) {
        offset = -1;
        return -1;
    }
    
    uint8_t firstByte = static_cast<uint8_t>(data[0]);
    offset = 1;
    
    if (firstByte < 0x80) {
        // Short form: length is in first byte
        return firstByte;
    } else if (firstByte == 0x81) {
        // Long form: 1 byte length
        if (data.size() < 2) {
            offset = -1;
            return -1;
        }
        offset = 2;
        return static_cast<uint8_t>(data[1]);
    } else if (firstByte == 0x82) {
        // Long form: 2 byte length
        if (data.size() < 3) {
            offset = -1;
            return -1;
        }
        offset = 3;
        return (static_cast<uint8_t>(data[1]) << 8) | static_cast<uint8_t>(data[2]);
    }
    
    // Unsupported length encoding
    offset = -1;
    return -1;
}

QByteArray Utils::encodeLength(int length)
{
    QByteArray result;
    
    if (length < 0x80) {
        // Short form
        result.append(static_cast<char>(length));
    } else if (length <= 0xFF) {
        // Long form: 1 byte
        result.append(static_cast<char>(0x81));
        result.append(static_cast<char>(length));
    } else if (length <= 0xFFFF) {
        // Long form: 2 bytes
        result.append(static_cast<char>(0x82));
        result.append(static_cast<char>((length >> 8) & 0xFF));
        result.append(static_cast<char>(length & 0xFF));
    }
    // Longer lengths not commonly needed for keycard
    
    return result;
}

QByteArray Utils::pad(const QByteArray& data, int blockSize)
{
    // ISO/IEC 7816-4 padding: add 0x80 followed by 0x00s
    QByteArray padded = data;
    padded.append(static_cast<char>(0x80));
    
    while (padded.size() % blockSize != 0) {
        padded.append(static_cast<char>(0x00));
    }
    
    return padded;
}

QByteArray Utils::unpad(const QByteArray& paddedData)
{
    if (paddedData.isEmpty()) {
        return paddedData;
    }
    
    // Find the 0x80 byte from the end
    int i = paddedData.size() - 1;
    while (i >= 0 && static_cast<uint8_t>(paddedData[i]) == 0x00) {
        i--;
    }
        
    if (i >= 0 && static_cast<uint8_t>(paddedData[i]) == 0x80) {
        QByteArray result = paddedData.left(i);
        return result;
    }
    
    // No valid padding found, return as-is
    return paddedData;
}

QByteArray Utils::uint32ToBytes(uint32_t value)
{
    QByteArray result;
    result.append(static_cast<char>((value >> 24) & 0xFF));
    result.append(static_cast<char>((value >> 16) & 0xFF));
    result.append(static_cast<char>((value >> 8) & 0xFF));
    result.append(static_cast<char>(value & 0xFF));
    return result;
}

uint32_t Utils::bytesToUint32(const QByteArray& bytes)
{
    if (bytes.size() < 4) {
        return 0;
    }
    
    return (static_cast<uint32_t>(static_cast<uint8_t>(bytes[0])) << 24) |
           (static_cast<uint32_t>(static_cast<uint8_t>(bytes[1])) << 16) |
           (static_cast<uint32_t>(static_cast<uint8_t>(bytes[2])) << 8) |
           static_cast<uint32_t>(static_cast<uint8_t>(bytes[3]));
}

} // namespace APDU
} // namespace Keycard

