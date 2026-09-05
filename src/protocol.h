// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Aura Launcher contributors

#ifndef AURA_UI_QT_PROTOCOL_H
#define AURA_UI_QT_PROTOCOL_H

#include "bridge_value.h"

#include <QString>

#include <optional>

namespace AuraProtocol {
constexpr std::int64_t kSchemaVersion = 1;
constexpr std::int64_t kAbi = 1;

inline QString protocolId() { return QStringLiteral("aura.ui.v1"); }

struct Envelope {
    enum class Kind { Request, Result, Error };

    Kind kind = Kind::Request;
    std::int64_t requestId = 0;
    QString method;
    BridgeValue params;
    BridgeValue value;
    QString code;
    QString message;
};

QByteArray encodeRequest(std::int64_t requestId, const QString &method, const BridgeValue &params);
QByteArray encodeResult(std::int64_t requestId, const BridgeValue &value);
QByteArray encodeError(std::int64_t requestId, const QString &code, const QString &message);

std::optional<Envelope> decode(const QByteArray &body, bool launcherRequest,
                               QString *error = nullptr);
bool validateHello(const BridgeValue &params, QString *error = nullptr);
} // namespace AuraProtocol

#endif // AURA_UI_QT_PROTOCOL_H
