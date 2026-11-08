// SPDX-License-Identifier: GPL-3.0-only
// The pure parts of "Move to another Minecraft version": status classification, default copy name
// and version comparison (the network plan and the copy/apply tasks are integration-level).

#include <QTest>

#include "qmlui/upgrade/UpgradeLogic.h"

using namespace UpgradeLogic;

class UpgradeTest : public QObject {
    Q_OBJECT

   private slots:
    // ---- status classification
    void classifyStatuses_data()
    {
        QTest::addColumn<bool>("hasMetadata");
        QTest::addColumn<bool>("hasVersion");
        QTest::addColumn<Status>("expected");

        QTest::newRow("ready") << true << true << Status::Ready;
        QTest::newRow("missing version") << true << false << Status::Missing;
        QTest::newRow("no metadata") << false << true << Status::Unknown;
        QTest::newRow("no metadata no version") << false << false << Status::Unknown;
    }

    void classifyStatuses()
    {
        QFETCH(bool, hasMetadata);
        QFETCH(bool, hasVersion);
        QFETCH(Status, expected);
        QCOMPARE(UpgradeLogic::classify(hasMetadata, hasVersion), expected);
    }

    void statusKeys()
    {
        QCOMPARE(QString::fromLatin1(statusKey(Status::Ready)), QStringLiteral("ready"));
        QCOMPARE(QString::fromLatin1(statusKey(Status::Missing)), QStringLiteral("missing"));
        QCOMPARE(QString::fromLatin1(statusKey(Status::Unknown)), QStringLiteral("unknown"));
    }

    // ---- version comparison (drives the one-way world warning)
    void versionComparisons()
    {
        QCOMPARE(UpgradeLogic::compareVersions("1.20.1", "1.21"), Delta::Upgrade);
        QCOMPARE(UpgradeLogic::compareVersions("1.20.1", "1.20.2"), Delta::Upgrade);
        QCOMPARE(UpgradeLogic::compareVersions("1.21", "1.20.1"), Delta::Downgrade);
        QCOMPARE(UpgradeLogic::compareVersions("1.20.1", "1.20.1"), Delta::Same);
        QCOMPARE(UpgradeLogic::compareVersions("1.20.10", "1.20.1"), Delta::Downgrade);  // numeric, not lexicographic
        QCOMPARE(UpgradeLogic::compareVersions("1.20.1", "1.20.10"), Delta::Upgrade);
        QCOMPARE(UpgradeLogic::compareVersions("", "1.21"), Delta::Unknown);
        QCOMPARE(UpgradeLogic::compareVersions("1.21", ""), Delta::Unknown);
    }

    // ---- default name of the upgraded copy: "<name> <version>"
    void defaultNames()
    {
        QCOMPARE(defaultName("Cherry build", "1.21.1"), QStringLiteral("Cherry build 1.21.1"));
        QCOMPARE(defaultName("  padded  ", "1.21"), QStringLiteral("padded 1.21"));
        QCOMPARE(defaultName("", "1.21"), QStringLiteral("1.21"));
        QCOMPARE(defaultName("   ", "1.21"), QStringLiteral("1.21"));
        QCOMPARE(defaultName("Only name", ""), QStringLiteral("Only name"));
        QCOMPARE(defaultName("", ""), QString());
    }

    // ---- loader rules (Forge/NeoForge are answered from metadata lists, not here)
    void loaders()
    {
        QCOMPARE(loader("fabric"), Loader::Fabric);
        QCOMPARE(loader("quilt"), Loader::Quilt);
        QCOMPARE(loader("forge"), Loader::Forge);
        QCOMPARE(loader("neoforge"), Loader::NeoForge);
        QCOMPARE(loader(""), Loader::None);
        QCOMPARE(loader("legacy-fabric"), Loader::Other);

        QVERIFY(loaderVersionIndependent(Loader::Fabric));
        QVERIFY(loaderVersionIndependent(Loader::Quilt));
        QVERIFY(!loaderVersionIndependent(Loader::Forge));
        QVERIFY(!loaderVersionIndependent(Loader::None));

        QVERIFY(loaderSupportsMc(Loader::None, "1.20.1"));
        QVERIFY(loaderSupportsMc(Loader::Fabric, "1.14"));
        QVERIFY(loaderSupportsMc(Loader::Fabric, "1.21.1"));
        QVERIFY(!loaderSupportsMc(Loader::Fabric, "1.12.2"));
        QVERIFY(!loaderSupportsMc(Loader::Quilt, "1.13"));
        QVERIFY(loaderSupportsMc(Loader::Forge, "1.20.1"));  // checked against metadata lists elsewhere
        QVERIFY(!loaderSupportsMc(Loader::Fabric, ""));
    }
};

QTEST_GUILESS_MAIN(UpgradeTest)
#include "Upgrade_test.moc"
