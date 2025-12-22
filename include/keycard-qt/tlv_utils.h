#pragma once

#include <QByteArray>
#include <cstdint>

namespace Keycard {
namespace TLV {

/**
 * @brief Parse BER-TLV length field
 * @param data Raw data containing TLV structure
 * @param offset Current offset in data, will be updated to point after length field
 * @return Parsed length value, or 0 on error
 */
quint32 parseLength(const QByteArray& data, int& offset);

/**
 * @brief Find a specific tag in TLV data
 * @param data Raw TLV data
 * @param targetTag Tag to search for (e.g., 0x80, 0xA1)
 * @return Data associated with the tag, or empty QByteArray if not found
 */
QByteArray findTag(const QByteArray& data, uint8_t targetTag);

/**
 * @brief Encode a single TLV entry
 * @param tag Tag byte
 * @param value Value data
 * @return Encoded TLV structure (tag + length + value)
 */
QByteArray encode(uint8_t tag, const QByteArray& value);

/**
 * @brief Encode TLV length field (BER-TLV format)
 * @param length Length value to encode
 * @return Encoded length bytes
 */
QByteArray encodeLength(quint32 length);

} // namespace TLV
} // namespace Keycard




