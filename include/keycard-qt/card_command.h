#pragma once

#include <QObject>
#include <QUuid>
#include <QVariant>
#include <memory>

namespace Keycard {

// Forward declarations
class CommandSet;

/**
 * @brief Result of a card command execution
 */
struct CommandResult {
    bool success;
    QVariant data;
    QString error;
    
    CommandResult() : success(false) {}
    CommandResult(bool s, const QVariant& d = QVariant(), const QString& e = QString())
        : success(s), data(d), error(e) {}
    
    static CommandResult fromSuccess(const QVariant& data = QVariant()) {
        return CommandResult(true, data);
    }
    
    static CommandResult fromError(const QString& error) {
        return CommandResult(false, QVariant(), error);
    }
};

/**
 * @brief Base class for card commands following Command Pattern
 * 
 * Each command encapsulates a single operation to be executed on the card.
 * Commands are executed sequentially on the communication thread, preventing
 * race conditions and ensuring thread safety.
 */
class CardCommand {
public:
    virtual ~CardCommand() = default;
    
    /**
     * @brief Execute the command on the communication thread
     * @param cmdSet CommandSet to use for card operations
     * @return CommandResult with success/failure and data
     */
    virtual CommandResult execute(CommandSet* cmdSet) = 0;
    
    /**
     * @brief Get timeout for this command in milliseconds
     */
    virtual int timeoutMs() const { return 30000; }
    
    /**
     * @brief Can this command run during card initialization?
     */
    virtual bool canRunDuringInit() const { return false; }
    
    /**
     * @brief Get unique token for this command
     */
    QUuid token() const { return m_token; }
    
    /**
     * @brief Get command name for debugging
     */
    virtual QString name() const = 0;
    
protected:
    CardCommand() : m_token(QUuid::createUuid()) {}
    
private:
    QUuid m_token;
};

// Concrete command declarations
class SelectCommand : public CardCommand {
public:
    explicit SelectCommand(bool force = false) : m_force(force) {}
    CommandResult execute(CommandSet* cmdSet) override;
    QString name() const override { return "SELECT"; }
    bool canRunDuringInit() const override { return true; }
private:
    bool m_force;
};

class VerifyPINCommand : public CardCommand {
public:
    explicit VerifyPINCommand(const QString& pin) : m_pin(pin) {}
    CommandResult execute(CommandSet* cmdSet) override;
    QString name() const override { return "VERIFY_PIN"; }
private:
    QString m_pin;
};

class GetStatusCommand : public CardCommand {
public:
    explicit GetStatusCommand(uint8_t info = 0) : m_info(info) {}
    CommandResult execute(CommandSet* cmdSet) override;
    QString name() const override { return "GET_STATUS"; }
    bool canRunDuringInit() const override { return true; }
private:
    uint8_t m_info;
};

class InitCommand : public CardCommand {
public:
    InitCommand(const QString& pin, const QString& puk, const QString& pairingPassword)
        : m_pin(pin), m_puk(puk), m_pairingPassword(pairingPassword) {}
    CommandResult execute(CommandSet* cmdSet) override;
    QString name() const override { return "INIT"; }
    int timeoutMs() const override { return 60000; }  // Init can take longer
private:
    QString m_pin;
    QString m_puk;
    QString m_pairingPassword;
};

class ChangePINCommand : public CardCommand {
public:
    explicit ChangePINCommand(const QString& newPIN) : m_newPIN(newPIN) {}
    CommandResult execute(CommandSet* cmdSet) override;
    QString name() const override { return "CHANGE_PIN"; }
private:
    QString m_newPIN;
};

class ChangePUKCommand : public CardCommand {
public:
    explicit ChangePUKCommand(const QString& newPUK) : m_newPUK(newPUK) {}
    CommandResult execute(CommandSet* cmdSet) override;
    QString name() const override { return "CHANGE_PUK"; }
private:
    QString m_newPUK;
};

class UnblockPINCommand : public CardCommand {
public:
    UnblockPINCommand(const QString& puk, const QString& newPIN)
        : m_puk(puk), m_newPIN(newPIN) {}
    CommandResult execute(CommandSet* cmdSet) override;
    QString name() const override { return "UNBLOCK_PIN"; }
private:
    QString m_puk;
    QString m_newPIN;
};

class GenerateMnemonicCommand : public CardCommand {
public:
    explicit GenerateMnemonicCommand(int checksumSize) : m_checksumSize(checksumSize) {}
    CommandResult execute(CommandSet* cmdSet) override;
    QString name() const override { return "GENERATE_MNEMONIC"; }
private:
    int m_checksumSize;
};

class LoadSeedCommand : public CardCommand {
public:
    explicit LoadSeedCommand(const QByteArray& seed) : m_seed(seed) {}
    CommandResult execute(CommandSet* cmdSet) override;
    QString name() const override { return "LOAD_SEED"; }
    int timeoutMs() const override { return 60000; }  // Seed loading can take time
private:
    QByteArray m_seed;
};

class FactoryResetCommand : public CardCommand {
public:
    FactoryResetCommand() = default;
    CommandResult execute(CommandSet* cmdSet) override;
    QString name() const override { return "FACTORY_RESET"; }
    int timeoutMs() const override { return 60000; }
};

class ExportKeyCommand : public CardCommand {
public:
    ExportKeyCommand(bool derive, bool makeCurrent, const QString& path, uint8_t exportType = 0x00)
        : m_derive(derive), m_makeCurrent(makeCurrent), m_path(path), m_exportType(exportType) {}
    CommandResult execute(CommandSet* cmdSet) override;
    QString name() const override { return "EXPORT_KEY"; }
private:
    bool m_derive;
    bool m_makeCurrent;
    QString m_path;
    uint8_t m_exportType;
};

class ExportKeyExtendedCommand : public CardCommand {
public:
    ExportKeyExtendedCommand(bool derive, bool makeCurrent, const QString& path)
        : m_derive(derive), m_makeCurrent(makeCurrent), m_path(path) {}
    CommandResult execute(CommandSet* cmdSet) override;
    QString name() const override { return "EXPORT_KEY_EXTENDED"; }
private:
    bool m_derive;
    bool m_makeCurrent;
    QString m_path;
};

class GetMetadataCommand : public CardCommand {
public:
    GetMetadataCommand() = default;
    CommandResult execute(CommandSet* cmdSet) override;
    QString name() const override { return "GET_METADATA"; }
};

class StoreMetadataCommand : public CardCommand {
public:
    StoreMetadataCommand(const QString& name, const QStringList& paths)
        : m_name(name), m_paths(paths) {}
    CommandResult execute(CommandSet* cmdSet) override;
    QString name() const override { return "STORE_METADATA"; }
private:
    QString m_name;
    QStringList m_paths;
};

} // namespace Keycard
