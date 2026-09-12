// SPDX-License-Identifier: Apache-2.0
// Copyright 2026 Aura Launcher contributors

#include <QtTest/QtTest>

#include "export_selection.h"

namespace {

ExportFileEntryData entry(const QString &name, const QString &path,
                          bool directory, bool suggested)
{
    return ExportFileEntryData{name, path, directory, suggested};
}

} // namespace

class ExportSelectionTests final : public QObject {
    Q_OBJECT

private slots:
    void materializesCheckedDirectories()
    {
        ExportSelectionTree tree;
        tree.reset({entry(QStringLiteral("mods"), QStringLiteral("mods"), true, true),
                    entry(QStringLiteral("saves"), QStringLiteral("saves"), true, false),
                    entry(QStringLiteral("servers.dat"), QStringLiteral("servers.dat"), false, false)},
                   false);
        QCOMPARE(tree.unloadedSelectedDirectory(), QStringLiteral("mods"));
        tree.markPending(QStringLiteral("mods"), true);
        QString error;
        QVERIFY(tree.applyListing(
            QStringLiteral("mods"),
            {entry(QStringLiteral("a.jar"), QStringLiteral("mods/a.jar"), false, true),
             entry(QStringLiteral("private.jar"), QStringLiteral("mods/private.jar"), false, true)},
            false, &error));
        const std::optional<QStringList> paths = tree.collect(&error);
        QVERIFY(paths.has_value());
        const QStringList expected{QStringLiteral("mods"), QStringLiteral("mods/a.jar"),
                                   QStringLiteral("mods/private.jar")};
        QCOMPARE(*paths, expected);
    }

    void excludesDeselectedChildren()
    {
        ExportSelectionTree tree;
        tree.reset({entry(QStringLiteral("mods"), QStringLiteral("mods"), true, true)}, false);
        tree.markPending(QStringLiteral("mods"), true);
        QString error;
        QVERIFY(tree.applyListing(
            QStringLiteral("mods"),
            {entry(QStringLiteral("a.jar"), QStringLiteral("mods/a.jar"), false, true),
             entry(QStringLiteral("private.jar"), QStringLiteral("mods/private.jar"), false, true)},
            false, &error));
        tree.setNodeChecked(QStringLiteral("mods/private.jar"), false);
        const ExportSelectionTree::Node *mods = tree.nodeAt(QStringLiteral("mods"));
        QVERIFY(mods != nullptr);
        QVERIFY(!mods->checked);
        QVERIFY(mods->partial);
        const std::optional<QStringList> paths = tree.collect(&error);
        QVERIFY(paths.has_value());
        const QStringList expected{QStringLiteral("mods"), QStringLiteral("mods/a.jar")};
        QCOMPARE(*paths, expected);
    }

    void rejectsTruncatedCheckedDirectory()
    {
        ExportSelectionTree tree;
        tree.reset({entry(QStringLiteral("mods"), QStringLiteral("mods"), true, true)}, false);
        tree.markPending(QStringLiteral("mods"), true);
        QString error;
        QVERIFY(tree.applyListing(
            QStringLiteral("mods"),
            {entry(QStringLiteral("a.jar"), QStringLiteral("mods/a.jar"), false, true)},
            true, &error));
        QVERIFY(!tree.collect(&error).has_value());
        QVERIFY(error.contains(QStringLiteral("truncated")));
    }

    void allowsTruncatedPartialDirectory()
    {
        ExportSelectionTree tree;
        tree.reset({entry(QStringLiteral("mods"), QStringLiteral("mods"), true, false)}, false);
        tree.markPending(QStringLiteral("mods"), true);
        QString error;
        // Manual expansion keeps suggested defaults; unseen truncated entries stay excluded.
        QVERIFY(tree.applyListing(
            QStringLiteral("mods"),
            {entry(QStringLiteral("a.jar"), QStringLiteral("mods/a.jar"), false, true),
             entry(QStringLiteral("b.jar"), QStringLiteral("mods/b.jar"), false, false)},
            true, &error));
        const ExportSelectionTree::Node *mods = tree.nodeAt(QStringLiteral("mods"));
        QVERIFY(mods != nullptr);
        QVERIFY(!mods->checked);
        QVERIFY(mods->partial);
        const std::optional<QStringList> paths = tree.collect(&error);
        QVERIFY(paths.has_value());
        const QStringList expected{QStringLiteral("mods"), QStringLiteral("mods/a.jar")};
        QCOMPARE(*paths, expected);
    }

    void rejectsUnloadedAndEmptySelections()
    {
        ExportSelectionTree tree;
        tree.reset({entry(QStringLiteral("mods"), QStringLiteral("mods"), true, true)}, false);
        QString error;
        QVERIFY(!tree.collect(&error).has_value());
        QVERIFY(error.contains(QStringLiteral("loading")));
        tree.markPending(QStringLiteral("mods"), true);
        QVERIFY(tree.applyListing(
            QStringLiteral("mods"),
            {entry(QStringLiteral("a.jar"), QStringLiteral("mods/a.jar"), false, true)},
            false, &error));
        tree.setNodeChecked(QStringLiteral("mods"), false);
        const std::optional<QStringList> empty = tree.collect(&error);
        QVERIFY(empty.has_value());
        QVERIFY(empty->isEmpty());
    }

    void tracksPendingRequests()
    {
        ExportSelectionTree tree;
        tree.reset({entry(QStringLiteral("mods"), QStringLiteral("mods"), true, true)}, false);
        QVERIFY(tree.busy() == false);
        tree.markPending(QStringLiteral("mods"), true);
        QVERIFY(tree.busy());
        QVERIFY(tree.unloadedSelectedDirectory().isEmpty());
        QString error;
        QVERIFY(tree.applyListing(
            QStringLiteral("mods"),
            {entry(QStringLiteral("a.jar"), QStringLiteral("mods/a.jar"), false, true)},
            false, &error));
        QVERIFY(!tree.busy());
    }

    void rejectsUnexpectedListings()
    {
        ExportSelectionTree tree;
        tree.reset({entry(QStringLiteral("mods"), QStringLiteral("mods"), true, true)}, false);
        QString error;
        QVERIFY(!tree.applyListing(
            QStringLiteral("mods"),
            {entry(QStringLiteral("a.jar"), QStringLiteral("mods/a.jar"), false, true)},
            false, &error));
        QVERIFY(error.contains(QStringLiteral("unexpected")));
    }

    void derivesForceFromCheckedState()
    {
        ExportSelectionTree tree;
        tree.reset({entry(QStringLiteral("mods"), QStringLiteral("mods"), true, false)}, false);
        tree.setNodeChecked(QStringLiteral("mods"), true);
        tree.markPending(QStringLiteral("mods"), true);
        QString error;
        QVERIFY(tree.applyListing(
            QStringLiteral("mods"),
            {entry(QStringLiteral("a.jar"), QStringLiteral("mods/a.jar"), false, false),
             entry(QStringLiteral("b.jar"), QStringLiteral("mods/b.jar"), false, true)},
            false, &error));
        const std::optional<QStringList> paths = tree.collect(&error);
        QVERIFY(paths.has_value());
        const QStringList expected{QStringLiteral("mods"), QStringLiteral("mods/a.jar"),
                                   QStringLiteral("mods/b.jar")};
        QCOMPARE(*paths, expected);
    }

    void keepsExplicitEmptyDirectorySelection()
    {
        ExportSelectionTree tree;
        tree.reset({entry(QStringLiteral("empty"), QStringLiteral("empty"), true, false)}, false);
        tree.setNodeChecked(QStringLiteral("empty"), true);
        tree.markPending(QStringLiteral("empty"), true);
        QString error;
        QVERIFY(tree.applyListing(QStringLiteral("empty"), {}, false, &error));
        const ExportSelectionTree::Node *empty = tree.nodeAt(QStringLiteral("empty"));
        QVERIFY(empty != nullptr);
        QVERIFY(empty->checked);
        const std::optional<QStringList> paths = tree.collect(&error);
        QVERIFY(paths.has_value());
        QCOMPARE(*paths, QStringList{QStringLiteral("empty")});
    }

    void clearPendingUnblocksCollection()
    {
        ExportSelectionTree tree;
        tree.reset({entry(QStringLiteral("mods"), QStringLiteral("mods"), true, true)}, false);
        tree.markPending(QStringLiteral("mods"), true);
        QVERIFY(tree.busy());
        tree.clearPending();
        QVERIFY(!tree.busy());
        QString error;
        QVERIFY(!tree.applyListing(
            QStringLiteral("mods"),
            {entry(QStringLiteral("a.jar"), QStringLiteral("mods/a.jar"), false, true)},
            false, &error));
    }

    void recomputesNestedAncestors()
    {
        ExportSelectionTree tree;
        tree.reset({entry(QStringLiteral("config"), QStringLiteral("config"), true, true)}, false);
        tree.markPending(QStringLiteral("config"), true);
        QString error;
        QVERIFY(tree.applyListing(
            QStringLiteral("config"),
            {entry(QStringLiteral("deep"), QStringLiteral("config/deep"), true, true)},
            false, &error));
        tree.markPending(QStringLiteral("config/deep"), true);
        QVERIFY(tree.applyListing(
            QStringLiteral("config/deep"),
            {entry(QStringLiteral("a.cfg"), QStringLiteral("config/deep/a.cfg"), false, true),
             entry(QStringLiteral("b.cfg"), QStringLiteral("config/deep/b.cfg"), false, true)},
            false, &error));
        tree.setNodeChecked(QStringLiteral("config/deep/b.cfg"), false);
        const ExportSelectionTree::Node *config = tree.nodeAt(QStringLiteral("config"));
        const ExportSelectionTree::Node *deep = tree.nodeAt(QStringLiteral("config/deep"));
        QVERIFY(config != nullptr && deep != nullptr);
        QVERIFY(config->partial);
        QVERIFY(deep->partial);
        const std::optional<QStringList> paths = tree.collect(&error);
        QVERIFY(paths.has_value());
        const QStringList expected{QStringLiteral("config"), QStringLiteral("config/deep"),
                                   QStringLiteral("config/deep/a.cfg")};
        QCOMPARE(*paths, expected);
    }
};

QTEST_MAIN(ExportSelectionTests)
#include "export_selection_tests.moc"