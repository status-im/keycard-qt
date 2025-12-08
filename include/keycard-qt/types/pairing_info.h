#pragma once

#include <QByteArray>

namespace Keycard {

/**
 * @brief Pairing information for secure channel
 */
class PairingInfo {
public:
    PairingInfo() = default;
    PairingInfo(const QByteArray& key, int index)
        : m_key(key), m_index(index) {}
    
    QByteArray key() const { return m_key; }
    int index() const { return m_index; }
    
    void setKey(const QByteArray& key) { m_key = key; }
    void setIndex(int index) { m_index = index; }
    
    bool isValid() const {
        return !m_key.isEmpty() && m_index >= 0;
    }
    
private:
    QByteArray m_key;
    int m_index = -1;
};

} // namespace Keycard

