#pragma once

#include <QByteArray>
#include <QString>
#include <cstdint>

namespace Keycard {
namespace APDU {

/**
 * @brief Utility functions for APDU handling
 */
class Utils {
public:
    /**
     * @brief Convert hex string to byte array
     * @param hex Hex string (e.g. "AABBCCDD")
     * @return Byte array
     */
    static QByteArray fromHex(const QString& hex);
    
    /**
     * @brief Convert byte array to hex string
     * @param data Byte array
     * @param uppercase Use uppercase letters (default: true)
     * @return Hex string
     */
    static QString toHex(const QByteArray& data, bool uppercase = true);
    
    /**
     * @brief Encode TLV (Tag-Length-Value)
     * @param tag Tag byte
     * @param value Value bytes
     * @return Encoded TLV
     */
    static QByteArray encodeTLV(uint8_t tag, const QByteArray& value);
    
    /**
     * @brief Decode TLV
     * @param tlv TLV bytes
     * @param tag Output: tag byte
     * @param value Output: value bytes
     * @return true if successfully decoded
     */
    static bool decodeTLV(const QByteArray& tlv, uint8_t& tag, QByteArray& value);
    
    /**
     * @brief Parse ASN.1 length
     * @param data Data starting at length field
     * @param offset Output: offset after length field
     * @return Length value
     */
    static int parseLength(const QByteArray& data, int& offset);
    
    /**
     * @brief Encode ASN.1 length
     * @param length Length to encode
     * @return Encoded length bytes
     */
    static QByteArray encodeLength(int length);
    
    /**
     * @brief Pad data to block size
     * @param data Data to pad
     * @param blockSize Block size (typically 16 for AES)
     * @return Padded data
     */
    static QByteArray pad(const QByteArray& data, int blockSize);
    
    /**
     * @brief Unpad data
     * @param paddedData Padded data
     * @return Unpadded data
     */
    static QByteArray unpad(const QByteArray& paddedData);
    
    /**
     * @brief Convert uint32 to big-endian bytes
     * @param value Value to convert
     * @return 4 bytes in big-endian order
     */
    static QByteArray uint32ToBytes(uint32_t value);
    
    /**
     * @brief Convert big-endian bytes to uint32
     * @param bytes 4 bytes in big-endian order
     * @return uint32 value
     */
    static uint32_t bytesToUint32(const QByteArray& bytes);
};

} // namespace APDU
} // namespace Keycard

