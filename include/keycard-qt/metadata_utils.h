#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <cstdint>

namespace Keycard {
namespace MetadataEncoding {

/**
 * @brief Encode metadata in keycard format
 * 
 * Format matches Go's types/metadata.go Serialize():
 * - Byte 0: 0x20 | namelen (version=1 in top 3 bits, name length in bottom 5 bits)
 * - Bytes 1..namelen: card name (UTF-8)
 * - Remaining: LEB128-encoded start/count pairs for consecutive wallet paths
 * 
 * @param name Card name (max 20 characters)
 * @param paths Wallet paths (must start with "m/44'/60'/0'/0/")
 * @param errorMsg Output: error message if encoding fails
 * @return Encoded metadata, or empty QByteArray on error
 */
QByteArray encode(const QString& name, const QStringList& paths, QString& errorMsg);

/**
 * @brief Write unsigned integer in LEB128 format
 * @param buffer Output buffer
 * @param value Value to encode
 */
void writeLEB128(QByteArray& buffer, uint32_t value);

/**
 * @brief Read unsigned integer from LEB128 format
 * @param data Input data
 * @param offset Current offset, will be updated to point after LEB128 value
 * @return Decoded value
 */
uint32_t readLEB128(const QByteArray& data, int& offset);

} // namespace MetadataEncoding
} // namespace Keycard


