// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Aura Launcher contributors

#ifndef AURA_UI_QT_BRIDGE_VALUE_H
#define AURA_UI_QT_BRIDGE_VALUE_H

#include <QByteArray>
#include <QString>

#include <cstdint>
#include <optional>
#include <vector>

/// One value in the frozen Aura Bridge Value v1 tree.
struct BridgeValueMapEntry;

class BridgeValue {
public:
    enum class Type { Null, Boolean, Integer, Float, String, Bytes, Array, Map };
    using MapEntry = BridgeValueMapEntry;

    BridgeValue() = default;

    static BridgeValue nullValue() { return BridgeValue(); }
    static BridgeValue boolean(bool value);
    static BridgeValue integer(std::int64_t value);
    static BridgeValue real(double value);
    static BridgeValue string(QString value);
    static BridgeValue bytes(QByteArray value);
    static BridgeValue array(std::vector<BridgeValue> values);
    static BridgeValue map(std::vector<MapEntry> entries);

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Type::Null; }
    bool isMap() const { return m_type == Type::Map; }
    bool isArray() const { return m_type == Type::Array; }
    bool toBoolean() const { return m_boolean; }
    std::int64_t toInteger() const { return m_integer; }
    double toReal() const { return m_real; }
    QString toString() const { return m_string; }
    QByteArray toBytes() const { return m_bytes; }
    const std::vector<BridgeValue> &arrayValues() const { return m_array; }
    const std::vector<MapEntry> &mapEntries() const { return m_map; }

    bool operator==(const BridgeValue &other) const;
    const BridgeValue *entry(const QString &key) const;
    std::optional<QString> optionalString(const QString &key) const;

    QByteArray encode() const;
    static std::optional<BridgeValue> decode(const QByteArray &input, QString *error = nullptr);

private:
    void encodeInto(QByteArray &output, int depth) const;
    static std::optional<BridgeValue> decodeAt(const QByteArray &input, qsizetype &cursor,
                                               int depth, QString *error);

    Type m_type = Type::Null;
    bool m_boolean = false;
    std::int64_t m_integer = 0;
    double m_real = 0.0;
    QString m_string;
    QByteArray m_bytes;
    std::vector<BridgeValue> m_array;
    std::vector<MapEntry> m_map;
};

struct BridgeValueMapEntry {
    QString key;
    BridgeValue value;

    bool operator==(const BridgeValueMapEntry &other) const
    {
        return key == other.key && value == other.value;
    }
};

#endif // AURA_UI_QT_BRIDGE_VALUE_H
