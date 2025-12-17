#include "keycard-qt/card_command.h"
#include "keycard-qt/command_set.h"
#include "keycard-qt/types.h"
#include "keycard-qt/metadata_utils.h"
#include <QDebug>
#include <QVariantList>

namespace Keycard {

CommandResult SelectCommand::execute(CommandSet* cmdSet) {
    qDebug() << "SelectCommand::execute() force:" << m_force;
    
    ApplicationInfo appInfo = cmdSet->select(m_force);
    
    if (!appInfo.installed && appInfo.instanceUID.isEmpty() && appInfo.secureChannelPublicKey.isEmpty()) {
        return CommandResult::fromError("Failed to select applet");
    }
    
    QVariantMap map;
    map["installed"] = appInfo.installed;
    map["initialized"] = appInfo.initialized;
    map["instanceUID"] = appInfo.instanceUID.toHex();
    map["availableSlots"] = appInfo.availableSlots;
    
    return CommandResult::fromSuccess(map);
}

CommandResult VerifyPINCommand::execute(CommandSet* cmdSet) {
    qDebug() << "VerifyPINCommand::execute()";
    
    bool success = cmdSet->verifyPIN(m_pin);
    
    if (!success) {
        return CommandResult::fromError(cmdSet->lastError());
    }
    
    QVariantMap result;
    result["success"] = true;
    result["remainingAttempts"] = cmdSet->remainingPINAttempts();
    
    return CommandResult::fromSuccess(result);
}

CommandResult GetStatusCommand::execute(CommandSet* cmdSet) {
    qDebug() << "GetStatusCommand::execute() info:" << m_info;
    
    ApplicationStatus status = cmdSet->getStatus(m_info);
    
    if (status.pinRetryCount < 0) {
        return CommandResult::fromError(cmdSet->lastError());
    }
    
    QVariantMap map;
    map["pinRetryCount"] = status.pinRetryCount;
    map["pukRetryCount"] = status.pukRetryCount;
    map["keyInitialized"] = status.keyInitialized;
    
    return CommandResult::fromSuccess(map);
}

CommandResult InitCommand::execute(CommandSet* cmdSet) {
    qDebug() << "InitCommand::execute()";
    
    QString password = m_pairingPassword.isEmpty() ? "KeycardDefaultPairing" : m_pairingPassword;
    Secrets secrets(m_pin, m_puk, password);
    
    bool result = cmdSet->init(secrets);
    if (!result) {
        return CommandResult::fromError(cmdSet->lastError());
    }
    
    // Get updated info after init
    ApplicationInfo appInfo = cmdSet->select(false);
    ApplicationStatus status = cmdSet->cachedApplicationStatus();
    
    QVariantMap map;
    map["instanceUID"] = appInfo.instanceUID.toHex();
    map["keyUID"] = appInfo.keyUID.toHex();
    map["remainingAttemptsPIN"] = status.pinRetryCount;
    
    return CommandResult::fromSuccess(map);
}

CommandResult ChangePINCommand::execute(CommandSet* cmdSet) {
    qDebug() << "ChangePINCommand::execute()";
    
    bool result = cmdSet->changePIN(m_newPIN);
    if (!result) {
        return CommandResult::fromError(cmdSet->lastError());
    }
    return CommandResult::fromSuccess();
}

CommandResult ChangePUKCommand::execute(CommandSet* cmdSet) {
    qDebug() << "ChangePUKCommand::execute()";
    
    bool result = cmdSet->changePUK(m_newPUK);
    if (!result) {
        return CommandResult::fromError(cmdSet->lastError());
    }
    return CommandResult::fromSuccess();
}

CommandResult UnblockPINCommand::execute(CommandSet* cmdSet) {
    qDebug() << "UnblockPINCommand::execute()";
    
    bool result = cmdSet->unblockPIN(m_puk, m_newPIN);
    if (!result) {
        return CommandResult::fromError(cmdSet->lastError());
    }
    return CommandResult::fromSuccess();
}

CommandResult GenerateMnemonicCommand::execute(CommandSet* cmdSet) {
    qDebug() << "GenerateMnemonicCommand::execute() checksumSize:" << m_checksumSize;
    
    QVector<int> indexes = cmdSet->generateMnemonic(m_checksumSize);
    if (indexes.isEmpty()) {
        return CommandResult::fromError(cmdSet->lastError());
    }
    
    QVariantList list;
    for (int index : indexes) {
        list.append(index);
    }
    
    return CommandResult::fromSuccess(list);
}

CommandResult LoadSeedCommand::execute(CommandSet* cmdSet) {
    qDebug() << "LoadSeedCommand::execute() seedSize:" << m_seed.size();
    
    QByteArray keyUID = cmdSet->loadSeed(m_seed);
    if (keyUID.isEmpty()) {
        return CommandResult::fromError(cmdSet->lastError());
    }
    
    QVariantMap map;
    map["keyUID"] = keyUID.toHex();
    
    return CommandResult::fromSuccess(map);
}

CommandResult FactoryResetCommand::execute(CommandSet* cmdSet) {
    qDebug() << "FactoryResetCommand::execute()";
    
    bool result = cmdSet->factoryReset();
    if (!result) {
        return CommandResult::fromError(cmdSet->lastError());
    }
    
    // Get updated info after reset
    ApplicationInfo appInfo = cmdSet->select(true);
    ApplicationStatus status = cmdSet->cachedApplicationStatus();
    
    QVariantMap map;
    map["initialized"] = appInfo.initialized;
    map["keyInitialized"] = status.keyInitialized;
    
    return CommandResult::fromSuccess(map);
}

CommandResult ExportKeyCommand::execute(CommandSet* cmdSet) {
    qDebug() << "ExportKeyCommand::execute() path:" << m_path;
    
    QByteArray keyData = cmdSet->exportKey(m_derive, m_makeCurrent, m_path, m_exportType);
    if (keyData.isEmpty()) {
        return CommandResult::fromError(cmdSet->lastError());
    }
    
    QVariantMap map;
    map["keyData"] = keyData;
    map["path"] = m_path;
    
    return CommandResult::fromSuccess(map);
}

CommandResult ExportKeyExtendedCommand::execute(CommandSet* cmdSet) {
    qDebug() << "ExportKeyExtendedCommand::execute() path:" << m_path;
    
    QByteArray keyData = cmdSet->exportKeyExtended(m_derive, m_makeCurrent, m_path);
    if (keyData.isEmpty()) {
        return CommandResult::fromError(cmdSet->lastError());
    }
    
    QVariantMap map;
    map["keyData"] = keyData;
    map["path"] = m_path;
    
    return CommandResult::fromSuccess(map);
}

CommandResult GetMetadataCommand::execute(CommandSet* cmdSet) {
    qDebug() << "GetMetadataCommand::execute()";
    
    // Get metadata from card using P1StoreDataPublic (0x00)
    QByteArray tlvData = cmdSet->getData(0x00);  // P1StoreDataPublic
    
    if (tlvData.isEmpty() || tlvData.size() == 2) {
        // Empty or error response (status word), not an error - just no metadata
        return CommandResult::fromSuccess();
    }
    
    QVariantMap map;
    map["tlvData"] = tlvData;
    
    return CommandResult::fromSuccess(map);
}

CommandResult StoreMetadataCommand::execute(CommandSet* cmdSet) {
    qDebug() << "StoreMetadataCommand::execute() name:" << m_name << "paths:" << m_paths.size();
    
    // Encode metadata in keycard format (matching Go's types/metadata.go)
    QString errorMsg;
    QByteArray metadata = Keycard::MetadataEncoding::encode(m_name, m_paths, errorMsg);
    
    if (metadata.isEmpty()) {
        QString error = QString("Failed to encode metadata: %1").arg(errorMsg);
        qWarning() << "StoreMetadataCommand:" << error;
        return CommandResult::fromError(error);
    }
    
    // Store metadata on card using P1StoreDataPublic (0x00)
    bool result = cmdSet->storeData(0x00, metadata);  // P1StoreDataPublic
    if (!result) {
        return CommandResult::fromError(cmdSet->lastError());
    }
    
    qDebug() << "StoreMetadataCommand: Metadata stored successfully";
    return CommandResult::fromSuccess();
}

} // namespace Keycard
