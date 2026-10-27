#include "scv2_card_emulator.h"
#include "keycard-qt/types.h"

namespace Keycard {
namespace Test {

namespace {
const QByteArray SW_OK = QByteArray::fromHex("9000");
const QByteArray SW_SECURITY_NOT_SATISFIED = QByteArray::fromHex("6982");
const QByteArray SW_CONDITIONS_NOT_SATISFIED = QByteArray::fromHex("6985");
const QByteArray SW_WRONG_P1P2 = QByteArray::fromHex("6A86");
const QByteArray SW_WRONG_DATA = QByteArray::fromHex("6A80");

QByteArray tlv(uint8_t tag, const QByteArray& value)
{
    QByteArray out;
    out.append(static_cast<char>(tag));
    if (value.size() > 0x7F) {
        out.append(static_cast<char>(0x81));
    }
    out.append(static_cast<char>(value.size()));
    out.append(value);
    return out;
}
} // anonymous namespace

ScV2CardEmulator::ScV2CardEmulator(const QByteArray& caPrivateKey)
{
    m_caPublicKey = ScV2::compressPublicKey(ScV2::keyPairFromPrivateKey(caPrivateKey).publicKey);
    m_identity = ScV2::generateKeyPair();
    m_identityPublicKey = ScV2::compressPublicKey(m_identity.publicKey);
    m_certificate = ScV2::Certificate::create(caPrivateKey, m_identityPublicKey);
}

QByteArray ScV2CardEmulator::selectResponse() const
{
    const uint8_t status = initialized ? static_cast<uint8_t>(0x10 | (pinRetries & 0x0F)) : 0x00;
    QByteArray body;
    body.append(tlv(0x02, QByteArray::fromHex("0400")));
    body.append(tlv(0x8C, QByteArray(1, static_cast<char>(status))));
    body.append(tlv(0x8E, keyUID));
    body.append(tlv(0x8D, QByteArray::fromHex("1F")));
    body.append(tlv(0x8A, m_certificate));
    return tlv(0xA4, body) + SW_OK;
}

QByteArray ScV2CardEmulator::handle(const QByteArray& apdu)
{
    if (apdu.size() < 4) {
        return SW_WRONG_DATA;
    }
    const uint8_t ins = static_cast<uint8_t>(apdu.at(1));
    const QByteArray data = apdu.size() > 5 ? apdu.mid(5, static_cast<uint8_t>(apdu.at(4))) : QByteArray();

    switch (ins) {
    case APDU::INS_SELECT:
        m_open = false;  // SELECT drops the session
        return selectResponse();
    case APDU::INS_OPEN_SECURE_CHANNEL:
        return openSecureChannel(data);
    case APDU::INS_SECURED_APDU:
        return securedApdu(data);
    default:
        // Everything else must be wrapped on 4.x
        return SW_CONDITIONS_NOT_SATISFIED;
    }
}

QByteArray ScV2CardEmulator::openSecureChannel(const QByteArray& data)
{
    m_open = false;
    if (data.size() != ScV2::SaltSize + ScV2::PublicKeySize) {
        return SW_WRONG_DATA;
    }
    const QByteArray salt = data.left(ScV2::SaltSize);
    const QByteArray hostPublicKey = data.mid(ScV2::SaltSize);

    const ScV2::KeyPair cardKey = ScV2::generateKeyPair();
    const QByteArray shared = ScV2::ecdhSharedSecret(cardKey.privateKey, hostPublicKey);
    if (shared.isEmpty()) {
        return SW_WRONG_DATA;
    }
    QByteArray transcriptSalt = salt;
    if (signWrongTranscript) {
        transcriptSalt[0] = static_cast<char>(transcriptSalt[0] ^ 0x01);
    }
    const ScV2::RecoverableSignature signature =
        ScV2::sign(m_identity.privateKey, ScV2::transcriptDigest(transcriptSalt, hostPublicKey, cardKey.publicKey));

    m_keys = ScV2::deriveSessionKeys(salt, shared);
    m_counter = 0;
    m_open = true;
    ++openCount;
    return cardKey.publicKey + signature.der + SW_OK;
}

QByteArray ScV2CardEmulator::securedApdu(const QByteArray& data)
{
    if (!m_open) {
        return SW_CONDITIONS_NOT_SATISFIED;
    }
    const QByteArray nonce = ScV2::nonce(m_counter);
    QByteArray inner;
    if (rejectNextSecured || !ScV2::ccmDecrypt(m_keys.hostToCard, nonce, data, inner) || inner.size() < 5
        || static_cast<uint8_t>(inner.at(4)) != inner.size() - 5) {
        rejectNextSecured = false;
        m_open = false;
        return SW_SECURITY_NOT_SATISFIED;
    }

    InnerCommand command;
    command.ins = static_cast<uint8_t>(inner.at(1));
    command.p1 = static_cast<uint8_t>(inner.at(2));
    command.p2 = static_cast<uint8_t>(inner.at(3));
    command.data = inner.mid(5);
    innerCommands.append(command);

    const QByteArray response = processInner(command);
    const QByteArray wrapped = ScV2::ccmEncrypt(m_keys.cardToHost, nonce, response);
    ++m_counter;
    return wrapped + SW_OK;
}

QByteArray ScV2CardEmulator::processInner(const InnerCommand& command)
{
    if (!initialized) {
        if (command.ins != APDU::INS_INIT) {
            return SW_OK;  // a blank 4.x card answers everything but INIT with an empty 9000
        }
        if (command.data.size() < 18) {
            return SW_WRONG_DATA;
        }
        pin = QString::fromUtf8(command.data.left(6));
        puk = QString::fromUtf8(command.data.mid(6, 12));
        initialized = true;
        return SW_OK;
    }

    switch (command.ins) {
    case APDU::INS_GET_STATUS: {
        if (command.p1 != APDU::P1GetStatusApplication) {
            return SW_WRONG_P1P2;
        }
        QByteArray body;
        body.append(tlv(0x02, QByteArray(1, static_cast<char>(pinRetries))));
        body.append(tlv(0x02, QByteArray(1, static_cast<char>(pukRetries))));
        body.append(tlv(0x01, QByteArray(1, keyUID.isEmpty() ? '\x00' : '\xFF')));
        return tlv(0xA3, body) + SW_OK;
    }
    case APDU::INS_VERIFY_PIN:
        if (QString::fromUtf8(command.data) == pin) {
            pinRetries = 3;
            return SW_OK;
        }
        pinRetries = qMax(0, pinRetries - 1);
        return QByteArray::fromHex("63") + QByteArray(1, static_cast<char>(0xC0 | pinRetries));
    case APDU::INS_SIGN:
        return command.p1 == APDU::P1SignDerive ? tlv(0xA0, QByteArray(10, 0x5A)) + SW_OK : SW_WRONG_P1P2;
    case APDU::INS_EXPORT_KEY:
        return tlv(0xA1, tlv(0x80, QByteArray(65, 0x04))) + SW_OK;
    case APDU::INS_GET_DATA:
    case APDU::INS_STORE_DATA:
        return SW_OK;
    default:
        return QByteArray::fromHex("6D00");
    }
}

} // namespace Test
} // namespace Keycard
