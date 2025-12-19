#include "keycard-qt/metadata_utils.h"
#include <QDebug>
#include <algorithm>

namespace Keycard {
namespace MetadataEncoding {

// Wallet root path constant (matching status-keycard-go)
static const QString PATH_WALLET_ROOT = "m/44'/60'/0'/0";

void writeLEB128(QByteArray& buf, uint32_t value) {
    do {
        uint8_t byte = value & 0x7F;  // Take lower 7 bits
        value >>= 7;
        if (value != 0) {
            byte |= 0x80;  // Set continuation bit if more bytes follow
        }
        buf.append(static_cast<char>(byte));
    } while (value != 0);
}

uint32_t readLEB128(const QByteArray& data, int& offset) {
    uint32_t result = 0;
    int shift = 0;
    
    while (offset < data.size()) {
        uint8_t byte = static_cast<uint8_t>(data[offset]);
        offset++;
        
        result |= static_cast<uint32_t>(byte & 0x7F) << shift;
        
        if ((byte & 0x80) == 0) {
            // Last byte (no continuation bit)
            break;
        }
        
        shift += 7;
        
        if (shift >= 32) {
            qWarning() << "Metadata::readLEB128: Value too large";
            return 0;
        }
    }
    
    return result;
}

QByteArray encode(const QString& name, const QStringList& paths, QString& errorMsg) {
    qDebug() << "Metadata::encode: name:" << name << "paths:" << paths.size();
    
    // Validate name length
    QByteArray nameBytes = name.toUtf8();
    if (nameBytes.size() > 20) {
        errorMsg = "Card name exceeds 20 characters";
        return QByteArray();
    }
    
    // Parse paths to extract last component (matching Go implementation)
    // All paths must start with PATH_WALLET_ROOT
    QVector<uint32_t> pathComponents;
    for (const QString& path : paths) {
        if (!path.startsWith(PATH_WALLET_ROOT)) {
            errorMsg = QString("Path '%1' does not start with wallet root path '%2'")
                      .arg(path, PATH_WALLET_ROOT);
            return QByteArray();
        }
        
        // Extract last component (after last '/')
        QStringList parts = path.split('/');
        if (parts.isEmpty()) {
            errorMsg = QString("Invalid path format: %1").arg(path);
            return QByteArray();
        }
        
        bool ok;
        uint32_t component = parts.last().toUInt(&ok);
        if (!ok) {
            errorMsg = QString("Invalid path component: %1").arg(parts.last());
            return QByteArray();
        }
        
        pathComponents.append(component);
    }
    
    // Sort path components (Go keeps them ordered)
    std::sort(pathComponents.begin(), pathComponents.end());
    
    // Build metadata in Go's custom binary format (matching types/metadata.go Serialize())
    // Format: [version+namelen][name][start/count pairs in LEB128]
    // - Byte 0: 0x20 | namelen (version=1 in top 3 bits, name length in bottom 5 bits)
    // - Bytes 1..namelen: card name (UTF-8)
    // - Remaining: LEB128-encoded start/count pairs for consecutive wallet paths
    QByteArray metadata;
    
    uint8_t header = 0x20 | static_cast<uint8_t>(nameBytes.size());  // Version 1, name length
    metadata.append(static_cast<char>(header));
    metadata.append(nameBytes);
    
    // Encode wallet paths as start/count pairs (consecutive paths are grouped)
    // This matches Go's Serialize() logic
    if (!pathComponents.isEmpty()) {
        uint32_t start = pathComponents[0];
        uint32_t count = 0;
        
        for (int i = 1; i < pathComponents.size(); ++i) {
            if (pathComponents[i] == start + count + 1) {
                // Consecutive path, extend range
                count++;
            } else {
                // Non-consecutive, write current range and start new one
                writeLEB128(metadata, start);
                writeLEB128(metadata, count);
                start = pathComponents[i];
                count = 0;
            }
        }
        
        // Write final range
        writeLEB128(metadata, start);
        writeLEB128(metadata, count);
    }
    
    qDebug() << "Metadata::encode: Encoded metadata size:" << metadata.size() << "bytes";
    qDebug() << "Metadata::encode: Metadata hex:" << metadata.toHex();
    
    return metadata;
}

} // namespace MetadataEncoding
} // namespace Keycard


