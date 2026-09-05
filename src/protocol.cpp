// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Aura Launcher contributors

#include "protocol.h"

#include <set>
#include <vector>

namespace AuraProtocol {
namespace {
std::optional<std::int64_t> integerField(const BridgeValue &value, const QString &key)
{
    const BridgeValue *found = value.entry(key);
    if (found == nullptr || found->type() != BridgeValue::Type::Integer) {
        return std::nullopt;
    }
    return found->toInteger();
}

std::optional<QString> stringField(const BridgeValue &value, const QString &key)
{
    return value.optionalString(key);
}

bool exactFields(const BridgeValue &value, const std::set<QString> &fields, QString *error)
{
    if (value.type() != BridgeValue::Type::Map) {
        if (error != nullptr) {
            *error = QStringLiteral("aura.ui.v1 envelope is not a map");
        }
        return false;
    }
    std::set<QString> actual;
    for (const BridgeValue::MapEntry &entry : value.mapEntries()) {
        actual.insert(entry.key);
    }
    if (actual != fields) {
        if (error != nullptr) {
            *error = QStringLiteral("aura.ui.v1 envelope fields do not match the contract");
        }
        return false;
    }
    return true;
}

} // namespace

QByteArray encodeRequest(std::int64_t requestId, const QString &method, const BridgeValue &params)
{
    std::vector<BridgeValue::MapEntry> entries;
    entries.push_back({QStringLiteral("schemaVersion"), BridgeValue::integer(kSchemaVersion)});
    entries.push_back({QStringLiteral("type"), BridgeValue::string(QStringLiteral("request"))});
    entries.push_back({QStringLiteral("requestId"), BridgeValue::integer(requestId)});
    entries.push_back({QStringLiteral("method"), BridgeValue::string(method)});
    entries.push_back({QStringLiteral("params"), params});
    return BridgeValue::map(std::move(entries)).encode();
}

QByteArray encodeResult(std::int64_t requestId, const BridgeValue &value)
{
    std::vector<BridgeValue::MapEntry> entries;
    entries.push_back({QStringLiteral("schemaVersion"), BridgeValue::integer(kSchemaVersion)});
    entries.push_back({QStringLiteral("type"), BridgeValue::string(QStringLiteral("result"))});
    entries.push_back({QStringLiteral("requestId"), BridgeValue::integer(requestId)});
    entries.push_back({QStringLiteral("value"), value});
    return BridgeValue::map(std::move(entries)).encode();
}

QByteArray encodeError(std::int64_t requestId, const QString &code, const QString &message)
{
    std::vector<BridgeValue::MapEntry> entries;
    entries.push_back({QStringLiteral("schemaVersion"), BridgeValue::integer(kSchemaVersion)});
    entries.push_back({QStringLiteral("type"), BridgeValue::string(QStringLiteral("error"))});
    entries.push_back({QStringLiteral("requestId"), BridgeValue::integer(requestId)});
    entries.push_back({QStringLiteral("code"), BridgeValue::string(code)});
    entries.push_back({QStringLiteral("message"), BridgeValue::string(message)});
    return BridgeValue::map(std::move(entries)).encode();
}
std::optional<Envelope> decode(const QByteArray &body, bool launcherRequest, QString *error)
{
    std::optional<BridgeValue> decoded = BridgeValue::decode(body, error);
    if (!decoded.has_value()) {
        return std::nullopt;
    }
    const BridgeValue root = *decoded;
    const std::optional<std::int64_t> schema = integerField(root, QStringLiteral("schemaVersion"));
    if (!schema.has_value() || *schema != kSchemaVersion) {
        if (error != nullptr) {
            *error = QStringLiteral("unsupported aura.ui.v1 schema version");
        }
        return std::nullopt;
    }
    const std::optional<std::int64_t> requestId = integerField(root, QStringLiteral("requestId"));
    if (!requestId.has_value() || *requestId <= 0) {
        if (error != nullptr) {
            *error = QStringLiteral("invalid aura.ui.v1 request identifier");
        }
        return std::nullopt;
    }
    const std::optional<QString> type = stringField(root, QStringLiteral("type"));
    if (!type.has_value()) {
        if (error != nullptr) {
            *error = QStringLiteral("invalid aura.ui.v1 envelope type");
        }
        return std::nullopt;
    }

    Envelope result;
    result.requestId = *requestId;
    if (*type == QStringLiteral("request")) {
        if (!exactFields(root,
                         {QStringLiteral("schemaVersion"), QStringLiteral("type"),
                          QStringLiteral("requestId"), QStringLiteral("method"),
                          QStringLiteral("params")},
                         error)) {
            return std::nullopt;
        }
        const bool requestDirectionIsLauncher = (*requestId % 2) != 0;
        if (requestDirectionIsLauncher != launcherRequest) {
            if (error != nullptr) {
                *error = QStringLiteral("aura.ui.v1 request direction mismatch");
            }
            return std::nullopt;
        }
        const BridgeValue *method = root.entry(QStringLiteral("method"));
        const BridgeValue *params = root.entry(QStringLiteral("params"));
        if (method == nullptr || method->type() != BridgeValue::Type::String) {
            if (error != nullptr) {
                *error = QStringLiteral("invalid aura.ui.v1 method");
            }
            return std::nullopt;
        }
        result.kind = Envelope::Kind::Request;
        result.method = method->toString();
        result.params = *params;
        return result;
    }
    if (*type == QStringLiteral("result")) {
        if (!exactFields(root,
                         {QStringLiteral("schemaVersion"), QStringLiteral("type"),
                          QStringLiteral("requestId"), QStringLiteral("value")},
                         error)) {
            return std::nullopt;
        }
        result.kind = Envelope::Kind::Result;
        result.value = *root.entry(QStringLiteral("value"));
        return result;
    }
    if (*type == QStringLiteral("error")) {
        if (!exactFields(root,
                         {QStringLiteral("schemaVersion"), QStringLiteral("type"),
                          QStringLiteral("requestId"), QStringLiteral("code"),
                          QStringLiteral("message")},
                         error)) {
            return std::nullopt;
        }
        const BridgeValue *code = root.entry(QStringLiteral("code"));
        const BridgeValue *message = root.entry(QStringLiteral("message"));
        if (code == nullptr || message == nullptr
                || code->type() != BridgeValue::Type::String
                || message->type() != BridgeValue::Type::String) {
            if (error != nullptr) {
                *error = QStringLiteral("invalid aura.ui.v1 error fields");
            }
            return std::nullopt;
        }
        result.kind = Envelope::Kind::Error;
        result.code = code->toString();
        result.message = message->toString();
        return result;
    }
    if (error != nullptr) {
        *error = QStringLiteral("unsupported aura.ui.v1 envelope type");
    }
    return std::nullopt;
}

bool validateHello(const BridgeValue &params, QString *error)
{
    if (params.type() != BridgeValue::Type::Map || params.mapEntries().size() != 2) {
        if (error != nullptr) {
            *error = QStringLiteral("aura.ui.v1 hello must have exactly two fields");
        }
        return false;
    }
    const BridgeValue::MapEntry &protocol = params.mapEntries()[0];
    const BridgeValue::MapEntry &abi = params.mapEntries()[1];
    if (protocol.key != QStringLiteral("protocol")
            || abi.key != QStringLiteral("abi")
            || protocol.value.type() != BridgeValue::Type::String
            || protocol.value.toString() != protocolId()
            || abi.value.type() != BridgeValue::Type::Integer
            || abi.value.toInteger() != kAbi) {
        if (error != nullptr) {
            *error = QStringLiteral("aura.ui.v1 hello does not match ABI 1");
        }
        return false;
    }
    return true;
}
} // namespace AuraProtocol
