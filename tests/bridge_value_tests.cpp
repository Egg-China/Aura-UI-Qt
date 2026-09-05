// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Aura Launcher contributors

#include <QtTest/QtTest>

#include "bridge_value.h"

class BridgeValueTests final : public QObject {
    Q_OBJECT

private slots:
    void roundTripsScalars()
    {
        const std::vector<BridgeValue> values = {
            BridgeValue::nullValue(),
            BridgeValue::boolean(true),
            BridgeValue::boolean(false),
            BridgeValue::integer(-42),
            BridgeValue::real(2.5),
            BridgeValue::string(QStringLiteral("aura")),
            BridgeValue::bytes(QByteArrayLiteral("\x01\x02\x03")),
        };
        for (const BridgeValue &value : values) {
            const QByteArray encoded = value.encode();
            QVERIFY(!encoded.isEmpty());
            QString error;
            const std::optional<BridgeValue> decoded = BridgeValue::decode(encoded, &error);
            QVERIFY2(decoded.has_value(), qPrintable(error));
            QCOMPARE(*decoded, value);
        }
    }

    void roundTripsOrderedNestedMaps()
    {
        const BridgeValue value = BridgeValue::map({
            {QStringLiteral("protocol"), BridgeValue::string(QStringLiteral("aura.ui.v1"))},
            {QStringLiteral("abi"), BridgeValue::integer(1)},
            {QStringLiteral("list"), BridgeValue::array({
                BridgeValue::nullValue(),
                BridgeValue::boolean(false),
                BridgeValue::bytes(QByteArrayLiteral("payload")),
            })},
        });
        const std::optional<BridgeValue> decoded = BridgeValue::decode(value.encode());
        QVERIFY(decoded.has_value());
        QCOMPARE(*decoded, value);
        QCOMPARE(decoded->mapEntries()[0].key, QStringLiteral("protocol"));
    }

    void rejectsTrailingBytes()
    {
        QByteArray encoded = BridgeValue::nullValue().encode();
        encoded.append(char(0));
        QVERIFY(!BridgeValue::decode(encoded).has_value());
    }

    void rejectsNonCanonicalBoolean()
    {
        QByteArray encoded = BridgeValue::boolean(true).encode();
        encoded[encoded.size() - 1] = char(0xc1);
        QVERIFY(!BridgeValue::decode(encoded).has_value());
    }
};

QTEST_MAIN(BridgeValueTests)
#include "bridge_value_tests.moc"
