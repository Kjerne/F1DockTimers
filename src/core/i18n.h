#pragma once
// Translations: every text the user sees goes through T() (port of f1win/i18n.py).
//
// A language is one UTF-8 text file: "English text = Translation" per line, plus "@code" and
// "@name" lines (see lang/_template.txt). Placeholders like {round} are filled in after
// translating; a broken translation falls back to the English text.

#include <QMap>
#include <QString>
#include <QStringList>
#include <initializer_list>
#include <utility>

namespace f1 {

using Args = std::initializer_list<std::pair<const char*, QString>>;

QString T(const QString& english);
// Marks a text that reaches T() later (from an array), so tools/make_lang_template.py finds it.
#define F1_TR_NOOP(text) text
QString T(const QString& english, Args values);

namespace i18n {

struct Language {
    QString code;
    QString name;
    QString file;
};

struct Parsed {
    QMap<QString, QString> meta;
    QMap<QString, QString> table;
};

Parsed parse(const QString& text);

// Every language file in `dirs` (later folders override earlier ones; files starting "_" skipped).
QList<Language> available(const QStringList& dirs);

// Activate `choice` ("auto", "" or a code) from `dirs`. Returns the code in use ("en" by default).
QString load(const QString& choice, const QStringList& dirs);

// For tests: replace the active table directly.
void setTable(const QMap<QString, QString>& table, const QString& code = QStringLiteral("xx"));
QString current();

// "da" from the Windows display language.
QString windowsLanguage();

// Python str.format with named fields only ({name}, {{ and }}). ok=false on unknown/malformed fields.
QString format(const QString& text, Args values, bool* ok);

}  // namespace i18n
}  // namespace f1
