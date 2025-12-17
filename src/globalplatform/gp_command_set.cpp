#include "keycard-qt/globalplatform/gp_command_set.h"
#include "keycard-qt/globalplatform/gp_constants.h"
#include "keycard-qt/globalplatform/gp_crypto.h"
#include <QDebug>
#include <QRandomGenerator>

namespace Keycard {
namespace GlobalPlatform {

GlobalPlatformCommandSet::GlobalPlatformCommandSet(IChannel* channel)
    : m_channel(channel)
{
    if (!m_channel) {
        qWarning() << "GPCommandSet: Null channel provided";
    }
}

bool GlobalPlatformCommandSet::select(const QByteArray& aid)
{
    qDebug() << "GPCommandSet::select()" << (aid.isEmpty() ? "ISD" : aid.toHex());
    
    // Build SELECT command
    APDU::Command cmd(CLA_ISO7816, INS_SELECT, 0x04, 0x00);
    if (!aid.isEmpty()) {
        cmd.setData(aid);
    }
    cmd.setLe(0);  // Expect response
    
    APDU::Response resp = send(cmd);
    
    if (!checkOK(resp, {SW_FILE_NOT_FOUND})) {
        m_lastError = QString("SELECT failed: SW=%1").arg(resp.sw(), 4, 16, QChar('0'));
        return false;
    }
    
    qDebug() << "GPCommandSet: SELECT successful";
    return true;
}

bool GlobalPlatformCommandSet::openSecureChannel()
{
    qDebug() << "GPCommandSet::openSecureChannel()";
    
    // Generate 8-byte host challenge
    QByteArray hostChallenge(8, 0);
    QRandomGenerator::global()->fillRange(
        reinterpret_cast<quint32*>(hostChallenge.data()),
        2);  // 8 bytes = 2 x 4-byte integers
    
    qDebug() << "GPCommandSet: Host challenge:" << hostChallenge.toHex();
    
    // Send INITIALIZE UPDATE
    APDU::Response initResp = initializeUpdate(hostChallenge);
    if (!checkOK(initResp)) {
        m_lastError = QString("INITIALIZE UPDATE failed: SW=%1").arg(initResp.sw(), 4, 16, QChar('0'));
        return false;
    }
    
    // Try multiple key sets (matching Go implementation)
    const QVector<QPair<QString, QByteArray>> keySets = {
        {"Keycard development", KEYCARD_DEFAULT_KEY()},
        {"GlobalPlatform default", GLOBALPLATFORM_DEFAULT_KEY()}
    };
    
    for (const auto& keySet : keySets) {
        qDebug() << "GPCommandSet: Trying" << keySet.first << "keys";
        
        // Create base keys (same key for both ENC and MAC in basic SCP02)
        SCP02Keys baseKeys(keySet.second, keySet.second);
        
        // Create session from INITIALIZE UPDATE response
        QString sessionError;
        m_session = SCP02Session::create(baseKeys, initResp.data(), hostChallenge, &sessionError);
        
        if (!m_session) {
            qDebug() << "GPCommandSet: Failed with" << keySet.first << "keys:" << sessionError;
            continue;  // Try next key set
        }
        
        // Session created successfully - now authenticate
        qDebug() << "GPCommandSet: Session established with" << keySet.first << "keys";
        
        // Create wrapper for secure commands (needed for EXTERNAL AUTHENTICATE)
        m_wrapper = std::make_unique<SCP02Wrapper>(m_session->sessionKeys().macKey());
        
        // Calculate host cryptogram
        QByteArray hostCryptogram = calculateHostCryptogram(*m_session);
        
        // Send EXTERNAL AUTHENTICATE (first wrapped command)
        APDU::Response authResp = externalAuthenticate(hostCryptogram);
        if (!checkOK(authResp)) {
            qDebug() << "GPCommandSet: EXTERNAL AUTHENTICATE failed with SW="
                     << QString("0x%1").arg(authResp.sw(), 4, 16, QChar('0'));
            qDebug() << "GPCommandSet: Response data:" << authResp.data().toHex();
            m_session.reset();
            m_wrapper.reset();
            continue;  // Try next key set
        }
        
        // Authentication successful!
        qDebug() << "GPCommandSet: Secure channel opened successfully with" << keySet.first << "keys";
        
        return true;
    }
    
    // All key sets failed
    m_lastError = "Failed to open secure channel with any known key set";
    return false;
}

bool GlobalPlatformCommandSet::deleteObject(const QByteArray& aid, bool deleteRelated)
{
    if (!m_session || !m_wrapper) {
        m_lastError = "Secure channel not open";
        return false;
    }
    
    qDebug() << "GPCommandSet::deleteObject()" << aid.toHex() << "deleteRelated:" << deleteRelated;
    
    // Build DELETE command
    // Data format: TAG_DELETE_AID || length || AID
    QByteArray data;
    data.append(static_cast<char>(TAG_DELETE_AID));
    data.append(static_cast<char>(aid.size()));
    data.append(aid);
    
    uint8_t p2 = deleteRelated ? P2_DELETE_OBJECT_AND_RELATED : P2_DELETE_OBJECT;
    
    APDU::Command cmd(CLA_GP, INS_DELETE, 0x00, p2);
    cmd.setData(data);
    
    APDU::Response resp = sendSecure(cmd);
    
    // Allow "referenced data not found" (object already deleted)
    if (!checkOK(resp, {SW_REFERENCED_DATA_NOT_FOUND})) {
        m_lastError = QString("DELETE failed: SW=%1").arg(resp.sw(), 4, 16, QChar('0'));
        return false;
    }
    
    qDebug() << "GPCommandSet: DELETE successful";
    return true;
}

bool GlobalPlatformCommandSet::installKeycardApplet()
{
    if (!m_session || !m_wrapper) {
        m_lastError = "Secure channel not open";
        return false;
    }
    
    qDebug() << "GPCommandSet::installKeycardApplet()";
    
    // Build INSTALL [for install and make selectable] command
    // This assumes the Keycard package is already loaded
    
    QByteArray packageAID = PACKAGE_AID();
    QByteArray appletAID = KEYCARD_AID();
    QByteArray instanceAID = KEYCARD_INSTANCE_AID();
    
    // Build installation data
    QByteArray data;
    
    // Package AID
    data.append(static_cast<char>(packageAID.size()));
    data.append(packageAID);
    
    // Applet AID
    data.append(static_cast<char>(appletAID.size()));
    data.append(appletAID);
    
    // Instance AID
    data.append(static_cast<char>(instanceAID.size()));
    data.append(instanceAID);
    
    // Privileges (0x00 = no special privileges)
    QByteArray privileges = QByteArray(1, 0x00);
    data.append(static_cast<char>(privileges.size()));
    data.append(privileges);
    
    // Install parameters (empty for Keycard)
    QByteArray params;
    QByteArray fullParams;
    fullParams.append(static_cast<char>(0xC9));  // Tag
    fullParams.append(static_cast<char>(params.size()));
    fullParams.append(params);
    
    data.append(static_cast<char>(fullParams.size()));
    data.append(fullParams);
    
    // Token (empty)
    data.append(static_cast<char>(0x00));
    
    uint8_t p1 = P1_INSTALL_FOR_INSTALL | P1_INSTALL_FOR_MAKE_SELECTABLE;
    
    APDU::Command cmd(CLA_GP, INS_INSTALL, p1, 0x00);
    cmd.setData(data);
    
    APDU::Response resp = sendSecure(cmd);
    
    if (!checkOK(resp)) {
        m_lastError = QString("INSTALL failed: SW=%1").arg(resp.sw(), 4, 16, QChar('0'));
        return false;
    }
    
    qDebug() << "GPCommandSet: INSTALL successful";
    return true;
}

APDU::Response GlobalPlatformCommandSet::initializeUpdate(const QByteArray& hostChallenge)
{
    qDebug() << "GPCommandSet::initializeUpdate()";
    
    APDU::Command cmd(CLA_GP, INS_INITIALIZE_UPDATE, 0x00, 0x00);
    cmd.setData(hostChallenge);
    cmd.setLe(0);  // Expect response
    
    return send(cmd);
}

APDU::Response GlobalPlatformCommandSet::externalAuthenticate(const QByteArray& hostCryptogram)
{
    qDebug() << "GPCommandSet::externalAuthenticate()";
    
    APDU::Command cmd(CLA_MAC, INS_EXTERNAL_AUTHENTICATE, P1_EXTERNAL_AUTH_CMAC, 0x00);
    cmd.setData(hostCryptogram);
    
    // This command must be wrapped (it's the first secured command)
    if (!m_wrapper) {
        qWarning() << "GPCommandSet: Wrapper not initialized for EXTERNAL AUTHENTICATE";
        return APDU::Response(QByteArray::fromHex("6985"));  // Conditions not satisfied
    }
    
    return sendSecure(cmd);
}

QByteArray GlobalPlatformCommandSet::calculateHostCryptogram(const SCP02Session& session)
{
    // Host cryptogram = MAC(cardChallenge || hostChallenge)
    // IMPORTANT: Order matters! Must match GlobalPlatform spec (card THEN host)
    // Note: mac3DES handles padding internally
    QByteArray data = session.cardChallenge() + session.hostChallenge();
    
    QByteArray cryptogram = Crypto::mac3DES(
        session.sessionKeys().encKey(),
        data,
        Crypto::NULL_BYTES_8());
    
    qDebug() << "GPCommandSet: Host cryptogram:" << cryptogram.toHex();
    
    return cryptogram;
}

APDU::Response GlobalPlatformCommandSet::sendSecure(const APDU::Command& cmd)
{
    if (!m_wrapper) {
        qWarning() << "GPCommandSet: Secure channel not initialized";
        return APDU::Response(QByteArray::fromHex("6985"));
    }
    
    // Wrap command with MAC
    APDU::Command wrappedCmd = m_wrapper->wrap(cmd);
    
    // Send through channel
    return send(wrappedCmd);
}

APDU::Response GlobalPlatformCommandSet::send(const APDU::Command& cmd)
{
    if (!m_channel) {
        qWarning() << "GPCommandSet: No channel available";
        return APDU::Response(QByteArray::fromHex("6985"));
    }
    
    QByteArray rawResp = m_channel->transmit(cmd.serialize());
    return APDU::Response(rawResp);
}

bool GlobalPlatformCommandSet::checkOK(const APDU::Response& response, 
                                        const QVector<uint16_t>& allowedSW)
{
    if (response.isOK()) {
        return true;
    }
    
    // Check additional allowed status words
    for (uint16_t sw : allowedSW) {
        if (response.sw() == sw) {
            return true;
        }
    }
    
    m_lastError = QString("Command failed: SW=%1").arg(response.sw(), 4, 16, QChar('0'));
    return false;
}

} // namespace GlobalPlatform
} // namespace Keycard

