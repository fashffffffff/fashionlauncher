// SPDX-License-Identifier: GPL-3.0-only
// The blocked-mods matcher: the name prefilter and hash matching that pick user-downloaded files
// for CurseForge packs. No real files are needed — every function works on paths alone.

#include <QTest>

#include "qmlui/create/BlockedModsMatcher.h"

using namespace BlockedModsMatcher;

class BlockedModsMatchTest : public QObject {
    Q_OBJECT

    static BlockedMod mod(const QString& name, const QString& hash = "")
    {
        BlockedMod m;
        m.name = name;
        m.websiteUrl = "https://example.com/download/1";
        m.hash = hash;
        m.matched = false;
        m.targetFolder = "mods";
        return m;
    }

   private slots:
    void nameCompare();
    void laxNameCompare();
    void hashlessModMatchesByName();
    void hashedModNeedsItsHash();
    void hashMatchIsCaseInsensitive();
    void sameNameDifferentHash();
    void moveOnlyFromDownloads();
    void allFoundGate();
};

void BlockedModsMatchTest::nameCompare()
{
    QVERIFY(sameName("Sodium-Fabric.jar", "sodium-fabric.jar"));
    QVERIFY(!sameName("sodium-fabric.jar", "oculus.jar"));
    // the exact compare keeps separators significant — that is what the lax one is for
    QVERIFY(!sameName("sodium.fabric.jar", "sodium fabric.jar"));
}

void BlockedModsMatchTest::laxNameCompare()
{
    // separators are interchangeable: browsers and mirrors rename files like this all the time
    QVERIFY(laxSameName("Sodium.Fabric_0.6.13.jar", "sodium-fabric-0-6-13.jar"));
    QVERIFY(laxSameName("sodium+fabric 0.6.jar", "Sodium Fabric 0_6.jar"));
    // but the version itself must not change
    QVERIFY(!laxSameName("sodium-fabric-0.6.13.jar", "sodium-fabric-0.6.14.jar"));
    QVERIFY(!laxSameName("sodium-fabric.jar", "oculus.jar"));
}

void BlockedModsMatchTest::hashlessModMatchesByName()
{
    QList<BlockedMod> mods{ mod("old mod.jar") };
    const QString dl = "/home/u/Downloads";

    // a file with the exact same name (case aside) matches without hashing
    QVERIFY(!matchByName(mods, "/home/u/Downloads/old MOD.jar", dl, false));
    QVERIFY(mods[0].matched);
    QCOMPARE(mods[0].localPath, QString("/home/u/Downloads/old MOD.jar"));

    // a name that only matches laxly is a hashing candidate, not a match
    QList<BlockedMod> hashed{ mod("some mod.jar", "aa11") };
    QVERIFY(matchByName(hashed, "/home/u/Downloads/some-mod.jar", dl, false));
    QVERIFY(!hashed[0].matched);
    // unrelated names never match
    QVERIFY(!matchByName(hashed, "/home/u/Downloads/other.jar", dl, false));
}

void BlockedModsMatchTest::hashedModNeedsItsHash()
{
    QList<BlockedMod> mods{ mod("sodium.jar", "abc123") };
    QVERIFY(matchByName(mods, "/home/u/Downloads/sodium.jar", "/home/u/Downloads", false));
    QVERIFY(!mods[0].matched);  // name alone is not enough when a hash is known
    QVERIFY(matchByHash(mods, "ABC123", "/home/u/Downloads/sodium.jar", "/home/u/Downloads", false));
    QVERIFY(mods[0].matched);
    QCOMPARE(mods[0].localPath, QString("/home/u/Downloads/sodium.jar"));
}

void BlockedModsMatchTest::hashMatchIsCaseInsensitive()
{
    QList<BlockedMod> mods{ mod("sodium.jar", "abc123") };
    QVERIFY(matchByHash(mods, "ABC123", "/x/sodium.jar", "/home/u/Downloads", false));
    QVERIFY(mods[0].matched);
    QVERIFY(!matchByHash(mods, "fff456", "/x/other.jar", "/home/u/Downloads", false));
}

void BlockedModsMatchTest::sameNameDifferentHash()
{
    // two versions of one file: only the hash tells them apart
    QList<BlockedMod> mods{ mod("sodium.jar", "aaa"), mod("sodium.jar", "bbb") };
    QVERIFY(matchByHash(mods, "bbb", "/x/sodium-2.jar", "/home/u/Downloads", false));
    QVERIFY(!mods[0].matched);
    QVERIFY(mods[1].matched);
    QCOMPARE(mods[1].localPath, QString("/x/sodium-2.jar"));

    // an already found entry is not matched twice, and a foreign hash matches nothing
    QVERIFY(!matchByHash(mods, "bbb", "/x/copy.jar", "/home/u/Downloads", false));
    QVERIFY(!matchByHash(mods, "ccc", "/x/other.jar", "/home/u/Downloads", false));
}

void BlockedModsMatchTest::moveOnlyFromDownloads()
{
    QList<BlockedMod> inDownloads{ mod("m.jar", "aa") };
    QVERIFY(matchByHash(inDownloads, "aa", "/home/u/Downloads/m.jar", "/home/u/Downloads", true));
    QVERIFY(inDownloads[0].move);

    // a sibling folder whose name only starts the same is not the downloads folder
    QList<BlockedMod> sibling{ mod("m.jar", "aa") };
    QVERIFY(matchByHash(sibling, "aa", "/home/u/Downloads2/m.jar", "/home/u/Downloads", true));
    QVERIFY(!sibling[0].move);

    QList<BlockedMod> elsewhere{ mod("m.jar", "aa") };
    QVERIFY(matchByHash(elsewhere, "aa", "/tmp/m.jar", "/home/u/Downloads", true));
    QVERIFY(!elsewhere[0].move);

    // and the whole thing is off unless the setting asked for it
    QList<BlockedMod> noMove{ mod("m.jar", "aa") };
    QVERIFY(matchByHash(noMove, "aa", "/home/u/Downloads/m.jar", "/home/u/Downloads", false));
    QVERIFY(!noMove[0].move);
}

void BlockedModsMatchTest::allFoundGate()
{
    QList<BlockedMod> mods{ mod("a.jar", "aa"), mod("b.jar", "bb") };
    QVERIFY(!allFound(mods));
    QVERIFY(matchByHash(mods, "aa", "/x/a.jar", "", false));
    QVERIFY(!allFound(mods));
    QVERIFY(matchByHash(mods, "bb", "/x/b.jar", "", false));
    QVERIFY(allFound(mods));
}

QTEST_GUILESS_MAIN(BlockedModsMatchTest)

#include "BlockedModsMatch_test.moc"
