#include "keycard-qt/types.h"
#include "keycard-qt/types_parser.h"
#include <QDebug>

namespace Keycard {

ApplicationStatus parseApplicationStatus(const QByteArray& data)
{
    ApplicationStatus status;
    
    if (data.isEmpty()) {
        qWarning() << "ApplicationStatus: Empty data (secure channel not open?)";
        return status;
    }
    
    qDebug() << "ApplicationStatus: Parsing data:" << data.toHex();
    
    // Check if this is key path status (just derivation path bytes)
    if (data.size() > 0 && static_cast<uint8_t>(data[0]) != 0xA3) {
        // This is a key path status, not application status
        status.currentPath = data;
        qDebug() << "ApplicationStatus: Key path:" << data.toHex();
        return status;
    }
    
    // Parse TLV structure (tag 0xA3 = ApplicationStatusTemplate)
    if (data.size() < 2 || static_cast<uint8_t>(data[0]) != 0xA3) {
        qWarning() << "ApplicationStatus: Invalid template tag:" << QString::number(data[0], 16);
        return status;
    }
    
    // Get length
    int offset = 1;
    int length = static_cast<uint8_t>(data[offset++]);
    if (length == 0x81 && offset < data.size()) {
        length = static_cast<uint8_t>(data[offset++]);
    }
    
    // Parse nested TLV
    int i = offset;
    int pinTagCount = 0;
    
    while (i < data.size() && i < offset + length) {
        uint8_t tag = static_cast<uint8_t>(data[i++]);
        if (i >= data.size()) break;
        
        uint8_t tagLen = static_cast<uint8_t>(data[i++]);
        if (i + tagLen > data.size()) break;
        
        if (tag == 0x02 && tagLen == 1) {
            // Tag 0x02 appears twice: first is PIN, second is PUK
            if (pinTagCount == 0) {
                status.pinRetryCount = static_cast<uint8_t>(data[i]);
                qDebug() << "ApplicationStatus: PIN retry count:" << status.pinRetryCount;
            } else if (pinTagCount == 1) {
                status.pukRetryCount = static_cast<uint8_t>(data[i]);
                qDebug() << "ApplicationStatus: PUK retry count:" << status.pukRetryCount;
            }
            pinTagCount++;
        } else if (tag == 0x01 && tagLen == 1) {
            // Tag 0x01 = key initialized (0xFF = true)
            status.keyInitialized = (static_cast<uint8_t>(data[i]) == 0xFF);
            qDebug() << "ApplicationStatus: Key initialized:" << status.keyInitialized;
        }
        
        i += tagLen;
    }
    
    status.valid = true;
    return status;
}

} // namespace Keycard
