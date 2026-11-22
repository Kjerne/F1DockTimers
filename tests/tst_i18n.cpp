// Port of reference-python/tests/test_i18n.py (the template check is a Python tool; not ported).
#include "common.h"
#include "core/i18n.h"
#include "core/timeutil.h"

#include <QTemporaryDir>

using namespace f1;

class TestI18n : public QObject {
    Q_OBJECT
private Q_SLOTS:
    void parseLanguageFile() {
        const auto p = i18n::parse(QStringLiteral("# comment\n@code = xx\n@name = Testish\nSchedule = Skema\nEmpty = \n"
                                                  "in {time} = om {time}\nTwo\\nlines = To\\nlinjer\n"));
        QCOMPARE(p.meta, (QMap<QString, QString>{{"code", "xx"}, {"name", "Testish"}}));
        QCOMPARE(p.table, (QMap<QString, QString>{{"Schedule", "Skema"}, {"in {time}", "om {time}"}, {"Two\nlines", "To\nlinjer"}}));
    }
    void translateWithPlaceholdersAndFallbacks() {
        i18n::setTable({{"in {time}", "om {time}"}, {"Broken {x}", QStringLiteral("Ødelagt {y}")}});
        QCOMPARE(T(QStringLiteral("in {time}"), {{"time", "2d 3h"}}), QStringLiteral("om 2d 3h"));
        QCOMPARE(T(QStringLiteral("Untranslated {n}"), {{"n", "5"}}), QStringLiteral("Untranslated 5"));
        QCOMPARE(T(QStringLiteral("Broken {x}"), {{"x", "1"}}), QStringLiteral("Broken 1"));   // never breaks the UI
        QCOMPARE(fmtLong(3600), QStringLiteral("om 1h 0m"));                                     // used by the formatters
        i18n::setTable({});
    }
    void userLanguageFileIsFoundAndLoaded() {
        QTemporaryDir dir;
        QFile f(dir.filePath(QStringLiteral("xx.txt")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("@code = xx\n@name = Testish\nSchedule = Skema\n");
        f.close();
        const QStringList dirs{QStringLiteral(F1_FIXTURES) + QStringLiteral("../../lang"), dir.path()};
        bool found = false;
        for (const auto& l : i18n::available(dirs)) found |= l.code == u"xx" && l.name == u"Testish";
        QVERIFY(found);
        QCOMPARE(i18n::load(QStringLiteral("xx"), dirs), QStringLiteral("xx"));
        QCOMPARE(T(QStringLiteral("Schedule")), QStringLiteral("Skema"));
        const QString autoCode = i18n::load(QStringLiteral("auto"), dirs);
        QVERIFY(autoCode == u"en" || autoCode == u"xx");
        i18n::load(QStringLiteral("en"), dirs);
        QCOMPARE(T(QStringLiteral("Schedule")), QStringLiteral("Schedule"));
    }
    void onlyEnglishIsBuiltIn() {
        QStringList codes;
        for (const auto& l : i18n::available({QStringLiteral(F1_FIXTURES) + QStringLiteral("../../lang")})) codes << l.code;
        QCOMPARE(codes, QStringList{QStringLiteral("en")});
    }
};

QTEST_GUILESS_MAIN(TestI18n)
#include "tst_i18n.moc"
