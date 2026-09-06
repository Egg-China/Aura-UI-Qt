// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Aura Launcher contributors

#include "bridge_value.h"

#include <QtEndian>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>

namespace {
constexpr int kMaximumDepth = 63;
constexpr quint8 kValueHeader = 0x92;
constexpr quint8 kArray32 = 0xdd;
constexpr quint8 kBin32 = 0xc6;
constexpr quint8 kFloat64 = 0xcb;
constexpr quint8 kInt64 = 0xd3;
constexpr quint8 kString32 = 0xdb;

void appendByte(QByteArray &output, quint8 value)
{
    output.push_back(static_cast<char>(value));
}

void appendU32(QByteArray &output, quint32 value)
{
    const quint32 encoded = qToBigEndian(value);
    output.append(reinterpret_cast<const char *>(&encoded), sizeof(encoded));
}

void appendI64(QByteArray &output, std::int64_t value)
{
    quint64 encoded = 0;
    static_assert(sizeof(encoded) == sizeof(value), "Bridge Value requires 64-bit integers");
    std::memcpy(&encoded, &value, sizeof(value));
    encoded = qToBigEndian(encoded);
    output.append(reinterpret_cast<const char *>(&encoded), sizeof(encoded));
}

void appendString(QByteArray &output, const QString &value)
{
    const QByteArray encoded = value.toUtf8();
    if (static_cast<quint64>(encoded.size()) > std::numeric_limits<quint32>::max()) {
        throw std::length_error("Bridge Value string exceeds the wire limit");
    }
    appendByte(output, kString32);
    appendU32(output, static_cast<quint32>(encoded.size()));
    output.append(encoded);
}

void appendContainerHeader(QByteArray &output, std::size_t size)
{
    if (size > std::numeric_limits<quint32>::max()) {
        throw std::length_error("Bridge Value container exceeds the wire limit");
    }
    appendByte(output, kArray32);
    appendU32(output, static_cast<quint32>(size));
}

bool takeByte(const QByteArray &input, qsizetype &cursor, quint8 &value, QString *error)
{
    if (cursor >= input.size()) {
        if (error != nullptr) {
            *error = QStringLiteral("Bridge Value is truncated");
        }
        return false;
    }
    value = static_cast<quint8>(input.at(cursor));
    ++cursor;
    return true;
}

bool expectByte(const QByteArray &input, qsizetype &cursor, quint8 expected,
                const char *label, QString *error)
{
    quint8 actual = 0;
    if (!takeByte(input, cursor, actual, error)) {
        return false;
    }
    if (actual != expected) {
        if (error != nullptr) {
            *error = QString::fromLatin1(label) + QStringLiteral(" is not canonical");
        }
        return false;
    }
    return true;
}

bool readU32(const QByteArray &input, qsizetype &cursor, quint32 &value, QString *error)
{
    if (cursor + 4 > input.size()) {
        if (error != nullptr) {
            *error = QStringLiteral("Bridge Value u32 is truncated");
        }
        return false;
    }
    quint32 raw = 0;
    std::memcpy(&raw, input.constData() + cursor, sizeof(raw));
    value = qFromBigEndian(raw);
    cursor += 4;
    return true;
}

bool readI64(const QByteArray &input, qsizetype &cursor, std::int64_t &value, QString *error)
{
    if (cursor + 8 > input.size()) {
        if (error != nullptr) {
            *error = QStringLiteral("Bridge Value i64 is truncated");
        }
        return false;
    }
    quint64 raw = 0;
    std::memcpy(&raw, input.constData() + cursor, sizeof(raw));
    raw = qFromBigEndian(raw);
    std::memcpy(&value, &raw, sizeof(value));
    cursor += 8;
    return true;
}

bool readString(const QByteArray &input, qsizetype &cursor, QString &value, QString *error)
{
    if (!expectByte(input, cursor, kString32, "string marker", error)) {
        return false;
    }
    quint32 length = 0;
    if (!readU32(input, cursor, length, error)) {
        return false;
    }
    const qsizetype byteCount = static_cast<qsizetype>(length);
    if (byteCount < 0 || cursor + byteCount > input.size()) {
        if (error != nullptr) {
            *error = QStringLiteral("Bridge Value string is truncated");
        }
        return false;
    }
    value = QString::fromUtf8(input.constData() + cursor, byteCount);
    cursor += byteCount;
    return true;
}
} // namespace

BridgeValue BridgeValue::boolean(bool value)
{
    BridgeValue result;
    result.m_type = Type::Boolean;
    result.m_boolean = value;
    return result;
}

BridgeValue BridgeValue::integer(std::int64_t value)
{
    BridgeValue result;
    result.m_type = Type::Integer;
    result.m_integer = value;
    return result;
}

BridgeValue BridgeValue::real(double value)
{
    if (!std::isfinite(value)) {
        throw std::invalid_argument("Bridge Value floats must be finite");
    }
    BridgeValue result;
    result.m_type = Type::Float;
    result.m_real = value;
    return result;
}

BridgeValue BridgeValue::string(QString value)
{
    BridgeValue result;
    result.m_type = Type::String;
    result.m_string = std::move(value);
    return result;
}

BridgeValue BridgeValue::bytes(QByteArray value)
{
    BridgeValue result;
    result.m_type = Type::Bytes;
    result.m_bytes = std::move(value);
    return result;
}

BridgeValue BridgeValue::array(std::vector<BridgeValue> values)
{
    BridgeValue result;
    result.m_type = Type::Array;
    result.m_array = std::move(values);
    return result;
}

BridgeValue BridgeValue::map(std::vector<MapEntry> entries)
{
    BridgeValue result;
    result.m_type = Type::Map;
    result.m_map = std::make_shared<std::vector<MapEntry>>(std::move(entries));
    return result;
}

const std::vector<BridgeValue::MapEntry> &BridgeValue::mapEntries() const
{
    static const std::vector<MapEntry> empty;
    return m_map == nullptr ? empty : *m_map;
}
bool BridgeValue::operator==(const BridgeValue &other) const
{
    if (m_type != other.m_type) {
        return false;
    }
    switch (m_type) {
    case Type::Null:
        return true;
    case Type::Boolean:
        return m_boolean == other.m_boolean;
    case Type::Integer:
        return m_integer == other.m_integer;
    case Type::Float:
        return m_real == other.m_real;
    case Type::String:
        return m_string == other.m_string;
    case Type::Bytes:
        return m_bytes == other.m_bytes;
    case Type::Array:
        return m_array == other.m_array;
    case Type::Map:
        return mapEntries() == other.mapEntries();
    }
    return false;
}

const BridgeValue *BridgeValue::entry(const QString &key) const
{
    if (m_type != Type::Map || m_map == nullptr) {
        return nullptr;
    }
    const auto found = std::find_if(m_map->cbegin(), m_map->cend(),
                                    [&key](const MapEntry &candidate) {
                                        return candidate.key == key;
                                    });
    return found == m_map->cend() ? nullptr : &found->value;
}

std::optional<QString> BridgeValue::optionalString(const QString &key) const
{
    const BridgeValue *found = entry(key);
    if (found == nullptr || found->m_type != Type::String) {
        return std::nullopt;
    }
    return found->m_string;
}

void BridgeValue::encodeInto(QByteArray &output, int depth) const
{
    if (depth > kMaximumDepth) {
        throw std::length_error("Bridge Value depth exceeds the wire limit");
    }
    appendByte(output, kValueHeader);
    switch (m_type) {
    case Type::Null:
        appendByte(output, 0x00);
        appendByte(output, 0xc0);
        break;
    case Type::Boolean:
        appendByte(output, 0x01);
        appendByte(output, m_boolean ? 0xc3 : 0xc2);
        break;
    case Type::Integer:
        appendByte(output, 0x02);
        appendByte(output, kInt64);
        appendI64(output, m_integer);
        break;
    case Type::Float: {
        appendByte(output, 0x03);
        appendByte(output, kFloat64);
        quint64 bits = 0;
        std::memcpy(&bits, &m_real, sizeof(m_real));
        bits = qToBigEndian(bits);
        output.append(reinterpret_cast<const char *>(&bits), sizeof(bits));
        break;
    }
    case Type::String:
        appendByte(output, 0x04);
        appendString(output, m_string);
        break;
    case Type::Bytes:
        appendByte(output, 0x05);
        appendByte(output, kBin32);
        if (static_cast<quint64>(m_bytes.size()) > std::numeric_limits<quint32>::max()) {
            throw std::length_error("Bridge Value bytes exceed the wire limit");
        }
        appendU32(output, static_cast<quint32>(m_bytes.size()));
        output.append(m_bytes);
        break;
    case Type::Array:
        appendByte(output, 0x06);
        appendContainerHeader(output, m_array.size());
        for (const BridgeValue &value : m_array) {
            value.encodeInto(output, depth + 1);
        }
        break;
    case Type::Map:
        appendByte(output, 0x07);
        appendContainerHeader(output, mapEntries().size());
        for (const MapEntry &item : mapEntries()) {
            appendByte(output, kValueHeader);
            appendString(output, item.key);
            item.value.encodeInto(output, depth + 1);
        }
        break;
    }
}

QByteArray BridgeValue::encode() const
{
    QByteArray output;
    output.reserve(128);
    try {
        encodeInto(output, 0);
    } catch (const std::exception &) {
        return QByteArray();
    }
    return output;
}

std::optional<BridgeValue> BridgeValue::decodeAt(const QByteArray &input, qsizetype &cursor,
                                                int depth, QString *error)
{
    if (depth > kMaximumDepth) {
        if (error != nullptr) {
            *error = QStringLiteral("Bridge Value depth exceeds the wire limit");
        }
        return std::nullopt;
    }
    if (!expectByte(input, cursor, kValueHeader, "value header", error)) {
        return std::nullopt;
    }
    quint8 tag = 0;
    if (!takeByte(input, cursor, tag, error)) {
        return std::nullopt;
    }
    switch (tag) {
    case 0x00:
        if (!expectByte(input, cursor, 0xc0, "null payload", error)) {
            return std::nullopt;
        }
        return BridgeValue::nullValue();
    case 0x01: {
        quint8 payload = 0;
        if (!takeByte(input, cursor, payload, error)) {
            return std::nullopt;
        }
        if (payload == 0xc2) {
            return BridgeValue::boolean(false);
        }
        if (payload == 0xc3) {
            return BridgeValue::boolean(true);
        }
        if (error != nullptr) {
            *error = QStringLiteral("Bridge Value boolean is not canonical");
        }
        return std::nullopt;
    }
    case 0x02: {
        if (!expectByte(input, cursor, kInt64, "integer marker", error)) {
            return std::nullopt;
        }
        std::int64_t value = 0;
        if (!readI64(input, cursor, value, error)) {
            return std::nullopt;
        }
        return BridgeValue::integer(value);
    }
    case 0x03: {
        if (!expectByte(input, cursor, kFloat64, "float marker", error)) {
            return std::nullopt;
        }
        quint64 raw = 0;
        if (cursor + 8 > input.size()) {
            if (error != nullptr) {
                *error = QStringLiteral("Bridge Value float is truncated");
            }
            return std::nullopt;
        }
        std::memcpy(&raw, input.constData() + cursor, sizeof(raw));
        raw = qFromBigEndian(raw);
        double value = 0.0;
        std::memcpy(&value, &raw, sizeof(value));
        cursor += 8;
        if (!std::isfinite(value)) {
            if (error != nullptr) {
                *error = QStringLiteral("Bridge Value float is not finite");
            }
            return std::nullopt;
        }
        return BridgeValue::real(value);
    }
    case 0x04: {
        QString value;
        if (!readString(input, cursor, value, error)) {
            return std::nullopt;
        }
        return BridgeValue::string(std::move(value));
    }
    case 0x05: {
        if (!expectByte(input, cursor, kBin32, "bytes marker", error)) {
            return std::nullopt;
        }
        quint32 length = 0;
        if (!readU32(input, cursor, length, error)) {
            return std::nullopt;
        }
        const qsizetype byteCount = static_cast<qsizetype>(length);
        if (byteCount < 0 || cursor + byteCount > input.size()) {
            if (error != nullptr) {
                *error = QStringLiteral("Bridge Value bytes are truncated");
            }
            return std::nullopt;
        }
        BridgeValue result;
        result.m_type = Type::Bytes;
        result.m_bytes = QByteArray(input.constData() + cursor, byteCount);
        cursor += byteCount;
        return result;
    }
    case 0x06: {
        if (!expectByte(input, cursor, kArray32, "array marker", error)) {
            return std::nullopt;
        }
        quint32 count = 0;
        if (!readU32(input, cursor, count, error)) {
            return std::nullopt;
        }
        const qsizetype remaining = input.size() - cursor;
        if (static_cast<quint64>(count) > static_cast<quint64>(remaining / 3)) {
            if (error != nullptr) {
                *error = QStringLiteral("Bridge Value array length exceeds the frame");
            }
            return std::nullopt;
        }
        std::vector<BridgeValue> values;
        values.reserve(static_cast<std::size_t>(count));
        for (quint32 index = 0; index < count; ++index) {
            std::optional<BridgeValue> value = decodeAt(input, cursor, depth + 1, error);
            if (!value.has_value()) {
                return std::nullopt;
            }
            values.push_back(std::move(*value));
        }
        return BridgeValue::array(std::move(values));
    }
    case 0x07: {
        if (!expectByte(input, cursor, kArray32, "map marker", error)) {
            return std::nullopt;
        }
        quint32 count = 0;
        if (!readU32(input, cursor, count, error)) {
            return std::nullopt;
        }
        const qsizetype remaining = input.size() - cursor;
        if (static_cast<quint64>(count) > static_cast<quint64>(remaining / 8)) {
            if (error != nullptr) {
                *error = QStringLiteral("Bridge Value map length exceeds the frame");
            }
            return std::nullopt;
        }
        std::vector<MapEntry> entries;
        entries.reserve(static_cast<std::size_t>(count));
        for (quint32 index = 0; index < count; ++index) {
            if (!expectByte(input, cursor, kValueHeader, "map entry header", error)) {
                return std::nullopt;
            }
            QString key;
            if (!readString(input, cursor, key, error)) {
                return std::nullopt;
            }
            std::optional<BridgeValue> value = decodeAt(input, cursor, depth + 1, error);
            if (!value.has_value()) {
                return std::nullopt;
            }
            const auto duplicate = std::any_of(entries.cbegin(), entries.cend(),
                                              [&key](const MapEntry &candidate) {
                                                  return candidate.key == key;
                                              });
            if (duplicate) {
                if (error != nullptr) {
                    *error = QStringLiteral("Bridge Value map contains a duplicate key: ") + key;
                }
                return std::nullopt;
            }
            entries.push_back({std::move(key), std::move(*value)});
        }
        return BridgeValue::map(std::move(entries));
    }
    default:
        if (error != nullptr) {
            *error = QStringLiteral("unsupported Bridge Value tag: %1").arg(tag);
        }
        return std::nullopt;
    }
}

std::optional<BridgeValue> BridgeValue::decode(const QByteArray &input, QString *error)
{
    qsizetype cursor = 0;
    std::optional<BridgeValue> value = decodeAt(input, cursor, 0, error);
    if (value.has_value() && cursor != input.size()) {
        if (error != nullptr) {
            *error = QStringLiteral("trailing bytes after the Bridge Value root");
        }
        return std::nullopt;
    }
    return value;
}
