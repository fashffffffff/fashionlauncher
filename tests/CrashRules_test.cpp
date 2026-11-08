// SPDX-License-Identifier: GPL-3.0-only
// The crash doctor's rule matcher on real-world log snippets (testdata/CrashRules). Negative cases matter
// as much as positive ones: a "conflict" message, a mixin failure or a stack trace through a mod must
// never turn into a diagnosis.

#include <QTest>

#include <QFile>

#include "qmlui/doctor/CrashRules.h"

using namespace CrashRules;

class CrashRulesTest : public QObject {
    Q_OBJECT

    static Diagnosis run(const QString& name)
    {
        QFile f(QFINDTESTDATA("testdata/CrashRules/" + name));
        if (!f.open(QIODevice::ReadOnly)) {
            qFatal("missing fixture %s", qPrintable(name));
        }
        return diagnose(QString::fromUtf8(f.readAll()));
    }
    static QList<Finding> ofKind(const Diagnosis& d, const QString& kind)
    {
        QList<Finding> out;
        for (const auto& f : d.findings) {
            if (f.kind == kind) {
                out << f;
            }
        }
        return out;
    }

   private slots:
    void fabricIncompatibleModSet()
    {
        const auto d = run("fabric-incompatible.log");
        const auto missing = ofKind(d, "missing-dependency");
        QCOMPARE(missing.size(), 1);
        QCOMPARE(missing[0].mod, QString("Mod Menu"));
        QCOMPARE(missing[0].dependency, QString("fabric-api"));
        QCOMPARE(missing[0].range, QString("any version"));
        QVERIFY(missing[0].evidence.first().startsWith("- Mod 'Mod Menu' (modmenu) 7.2.2 requires"));

        const auto wrong = ofKind(d, "wrong-dependency");
        QCOMPARE(wrong.size(), 1);
        QCOMPARE(wrong[0].mod, QString("Iris"));
        QCOMPARE(wrong[0].dependency, QString("sodium"));
        QCOMPARE(wrong[0].dependencyName, QString("Sodium"));
        QCOMPARE(wrong[0].range, QString("version 0.5.0 or later"));
        QCOMPARE(wrong[0].found, QString("0.4.10+build.27"));

        // a "breaks" declaration stops loading: the loader states it, names with apostrophes included
        const auto breaks = ofKind(d, "incompatible");
        QCOMPARE(breaks.size(), 1);
        QCOMPARE(breaks[0].mod, QString("Xaero's Minimap"));
        QCOMPARE(breaks[0].dependency, QString("xaerobetterpvp"));

        // "conflicts with" and "recommends" are warnings the game starts with: never findings
        QCOMPARE(d.findings.size(), 3);
        for (const auto& f : d.findings) {
            QVERIFY(f.mod != "Some Mod");
        }
    }

    void fabricConflictWordingIsNotADiagnosis()
    {
        const auto d = run("fabric-conflict-only.log");
        QVERIFY2(d.findings.isEmpty(), "conflict / mixin / NPE lines must not produce a diagnosis");
        // the excerpt starts at the unreported exception, not at the first warning
        QVERIFY(!d.excerpt.isEmpty());
        QVERIFY(d.excerpt.join('\n').contains("NullPointerException"));
        // mods in the trace are only "mentioned", and the game's own jars are left out
        QVERIFY(d.mentioned.contains("sodium"));
        QVERIFY(d.mentioned.contains("optifabric-1.13.24.jar"));
        QVERIFY(!d.mentioned.contains("client-intermediary.jar"));
    }

    void forgeMandatoryDependencies()
    {
        const auto d = run("forge-mandatory.log");
        const auto missing = ofKind(d, "missing-dependency");
        QCOMPARE(missing.size(), 1);
        QCOMPARE(missing[0].dependency, QString("architectury"));
        QCOMPARE(missing[0].mod, QString("roughlyenoughitems"));
        QCOMPARE(missing[0].range, QString("[9.1.12,)"));
        const auto wrong = ofKind(d, "wrong-dependency");
        QCOMPARE(wrong.size(), 1);
        QCOMPARE(wrong[0].dependency, QString("minecraft"));
        QCOMPARE(wrong[0].found, QString("1.20.4"));
        const auto java = ofKind(d, "java");
        QCOMPARE(java.size(), 1);
        QCOMPARE(java[0].javaMajor, 21);
        // the optional jei line above the mandatory section is ignored
        for (const auto& f : d.findings) {
            QVERIFY(f.dependency != "jei");
        }
    }

    void forgeOptionalDependenciesAreNotACause()
    {
        const auto d = run("forge-optional-only.log");
        QVERIFY(d.findings.isEmpty());
        QVERIFY(d.excerpt.first().contains("---- Minecraft Crash Report ----") || d.excerpt.join('\n').contains("Minecraft Crash Report"));
        QVERIFY(d.mentioned.contains("create"));
        QVERIFY(d.mentioned.contains("flywheel"));
        QVERIFY(!d.mentioned.contains("minecraft"));
    }

    void forgeDuplicates()
    {
        const auto d = run("forge-duplicate.log");
        QCOMPARE(d.findings.size(), 1);
        QCOMPARE(d.findings[0].kind, QString("duplicate"));
        QCOMPARE(d.findings[0].dependency, QString("jei"));
        QCOMPARE(d.findings[0].files, QStringList({ "jei-1.20.1-forge-15.2.0.27.jar", "jei-1.20.1-forge-15.3.0.4.jar" }));
        QCOMPARE(d.findings[0].evidence.size(), 2);
    }

    void neoForgeMissing()
    {
        const auto d = run("neoforge-missing.log");
        QCOMPARE(d.findings.size(), 1);
        QCOMPARE(d.findings[0].kind, QString("missing-dependency"));
        QCOMPARE(d.findings[0].mod, QString("jei"));
        QCOMPARE(d.findings[0].dependency, QString("cloth_config"));
        QVERIFY(!d.findings[0].evidence.first().contains(QChar(0xA7)));  // colour codes stripped
    }

    void forge112Missing()
    {
        const auto d = run("forge-1.12-missing.log");
        QCOMPARE(d.findings.size(), 1);
        QCOMPARE(d.findings[0].kind, QString("missing-dependency"));
        QCOMPARE(d.findings[0].mod, QString("Just Enough Resources"));
        QCOMPARE(d.findings[0].dependency, QString("jei"));
    }

    void memory()
    {
        auto d = run("oom-heap.log");
        QCOMPARE(d.findings.size(), 1);
        QCOMPARE(d.findings[0].kind, QString("memory"));
        QCOMPARE(d.findings[0].evidence, QStringList{ "java.lang.OutOfMemoryError: Java heap space" });

        d = run("oom-native-thread.log");
        QVERIFY2(d.findings.isEmpty(), "thread limits are not fixed by more heap");

        d = run("reserve-heap.log");
        QCOMPARE(d.findings.size(), 1);
        QCOMPARE(d.findings[0].kind, QString("memory-reserve"));
    }

    void javaVersion()
    {
        auto d = run("java-class-version.log");
        QCOMPARE(d.findings.size(), 1);
        QCOMPARE(d.findings[0].kind, QString("java"));
        QCOMPARE(d.findings[0].javaMajor, 21);
        QCOMPARE(d.findings[0].javaCurrent, 17);
        QVERIFY(!d.findings[0].javaTooNew);

        d = run("java-launchwrapper.log");
        QCOMPARE(d.findings.size(), 1);
        QCOMPARE(d.findings[0].javaMajor, 8);
        QVERIFY(d.findings[0].javaTooNew);

        d = run("fabric-java.log");
        QCOMPARE(d.findings.size(), 1);
        QCOMPARE(d.findings[0].kind, QString("java"));
        QCOMPARE(d.findings[0].javaMajor, 21);
        QCOMPARE(d.findings[0].javaCurrent, 17);

        QCOMPARE(javaForClassVersion(52), 8);
        QCOMPARE(javaForClassVersion(61), 17);
        QCOMPARE(javaForClassVersion(65), 21);
    }

    void graphicsDriver()
    {
        const auto d = run("graphics.log");
        QCOMPARE(d.findings.size(), 1);
        QCOMPARE(d.findings[0].kind, QString("graphics"));
        QCOMPARE(d.findings[0].evidence.size(), 2);
    }

    void corruptModFile()
    {
        auto d = run("corrupt-jar.log");
        QCOMPARE(d.findings.size(), 1);
        QCOMPARE(d.findings[0].kind, QString("corrupt-file"));
        QCOMPARE(d.findings[0].files, QStringList{ "sodium-fabric-0.5.8+mc1.20.1.jar" });

        // a broken library outside the mods folder is not "reinstall this mod"
        d = run("zip-library.log");
        QVERIFY(d.findings.isEmpty());
    }

    void plainStackTraceIsUnknown()
    {
        const QString log = R"([12:00:00] [Render thread/ERROR]: Reported exception thrown!
net.minecraft.class_148: Rendering overlay
	at net.minecraft.class_310.method_1523(class_310.java:1210) ~[client-intermediary.jar:?]
Caused by: java.lang.IllegalStateException: Mod 'Create' (create) caused a conflict with mod 'Flywheel' (flywheel)
	at com.simibubi.create.Create.init(Create.java:100) ~[create-fabric-0.5.1.jar:?]
	at net.minecraft.class_310.handler$zza000$create$onInit(class_310.java:5000) ~[client-intermediary.jar:?]
)";
        const auto d = diagnose(log);
        QVERIFY(d.findings.isEmpty());
        QVERIFY(d.mentioned.contains("create"));
        QVERIFY(d.mentioned.contains("create-fabric-0.5.1.jar"));
    }
};

QTEST_GUILESS_MAIN(CrashRulesTest)

#include "CrashRules_test.moc"
