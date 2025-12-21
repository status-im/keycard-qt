#include "keycard-qt/globalplatform/scp02_wrapper.h"
#include "keycard-qt/globalplatform/gp_crypto.h"
#include <QDebug>
#include <QDataStream>

namespace Keycard {
namespace GlobalPlatform {

SCP02Wrapper::SCP02Wrapper(const QByteArray& encKey, const QByteArray& macKey)
    : m_encKey(encKey)
    , m_macKey(macKey)
    , m_icv(Crypto::NULL_BYTES_8())  // Initialize with null bytes
{
    // NOTE: We store both keys but only macKey is actually used for ICV encryption
    // (to match Go/Java implementation). encKey is kept for potential future use.
    if (encKey.size() != 16) {
        qWarning() << "SCP02Wrapper: ENC key must be 16 bytes, got" << encKey.size();
    }
    if (macKey.size() != 16) {
        qWarning() << "SCP02Wrapper: MAC key must be 16 bytes, got" << macKey.size();
    }
}

void SCP02Wrapper::reset()
{
    m_icv = Crypto::NULL_BYTES_8();
}

APDU::Command SCP02Wrapper::wrap(const APDU::Command& cmd)
{
    // Build data for MAC calculation: CLA||INS||P1||P2||Lc||Data
    // Note: CLA has 0x04 bit set, Lc INCLUDES the 8-byte MAC (per GlobalPlatform SCP02 spec)
    
    uint8_t wrappedCla = cmd.cla() | 0x04;  // Set bit 2 to indicate MAC follows
    uint8_t wrappedLc = static_cast<uint8_t>(cmd.data().size() + 8);  // Original data + MAC
    
    // Build MAC input buffer
    QByteArray macData;
    macData.reserve(5 + cmd.data().size());
    
    macData.append(static_cast<char>(wrappedCla));
    macData.append(static_cast<char>(cmd.ins()));
    macData.append(static_cast<char>(cmd.p1()));
    macData.append(static_cast<char>(cmd.p2()));
    macData.append(static_cast<char>(wrappedLc));  // Wrapped Lc (including MAC)
    macData.append(cmd.data());
    
    qDebug() << "SCP02Wrapper: MAC input:" << macData.toHex();
    qDebug() << "SCP02Wrapper: Current ICV:" << m_icv.toHex();
    
    // Determine ICV for MAC calculation
    QByteArray icvForMac;
    if (m_icv == Crypto::NULL_BYTES_8()) {
        // First command: use null bytes
        icvForMac = m_icv;
    } else {
        // Subsequent commands: encrypt previous MAC with single DES-CBC using MAC key
        icvForMac = Crypto::encryptICV(m_macKey, m_icv);
    }
    
    qDebug() << "SCP02Wrapper: ICV for MAC:" << icvForMac.toHex();
    
    // Calculate MAC using retail MAC (single DES + 3DES)
    QByteArray mac = Crypto::macFull3DES(m_macKey, macData, icvForMac);
    
    if (mac.size() != 8) {
        qWarning() << "SCP02Wrapper: Invalid MAC size:" << mac.size();
        mac = QByteArray(8, 0x00);
    }
    
    qDebug() << "SCP02Wrapper: Calculated MAC:" << mac.toHex();
    
    // Update ICV for next command (ICV = last MAC)
    m_icv = mac;
    
    // Build wrapped command data: original data + MAC
    QByteArray wrappedData = cmd.data() + mac;
    
    // Create wrapped command
    APDU::Command wrappedCmd(wrappedCla, cmd.ins(), cmd.p1(), cmd.p2());
    wrappedCmd.setData(wrappedData);
    
    // Copy Le if present
    if (cmd.hasLe()) {
        wrappedCmd.setLe(cmd.le());
    }
    
    qDebug() << "SCP02Wrapper: Wrapped command:" << wrappedCmd.serialize().toHex();
    
    return wrappedCmd;
}

} // namespace GlobalPlatform
} // namespace Keycard

