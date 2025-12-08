#pragma once

#include "types.h"
#include <QByteArray>

namespace Keycard {

/**
 * @brief Parse ApplicationInfo from SELECT response
 * @param data Response data
 * @return Parsed ApplicationInfo
 */
ApplicationInfo parseApplicationInfo(const QByteArray& data);

/**
 * @brief Parse ApplicationStatus from GET STATUS response
 * @param data Response data
 * @return Parsed ApplicationStatus
 */
ApplicationStatus parseApplicationStatus(const QByteArray& data);

} // namespace Keycard

