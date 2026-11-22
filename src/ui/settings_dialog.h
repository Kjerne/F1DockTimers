#pragma once
// Settings (port of ui/settings_dialog.py). SettingsForm holds every setting on sub-pages; the panel
// shows it as its Settings tab, with Save / Revert below the page. SettingsDialog is the same form
// in a window of its own (a fallback). Short hints on the page, the details in hover tooltips.
// Saves into the shared settings.json through the `save` callback; the OpenF1 password goes to
// Windows Credential Manager only.

#include "../services/credentials.h"
#include "../services/store.h"
#include "../settings.h"
#include "taskbar_pill.h"
#include "theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPushButton>
#include <QSpinBox>
#include <functional>

namespace f1::ui {

// Where the form reads and writes the OpenF1 password (tests pass fakes).
struct CredentialStore {
    std::function<std::optional<QString>(const QString&)> get = credentials::getPassword;
    std::function<bool(const QString&, const QString&)> set = credentials::setPassword;
    std::function<void(const QString&)> remove = credentials::deletePassword;
};

// `embedded`: drawn on the panel (transparent pages).
QString dialogStylesheet(const Theme& t, bool embedded = false);
QStringList zoneNames();
const QMap<QString, QString>& settingsTips();

struct SettingsPage {
    QString key, label;
};
// General, Appearance, Taskbar, Formula 1, Live data, About.
QList<SettingsPage> settingsPages();
int settingsPageIndex(const QString& key);   // 0 for an unknown key

// A check box whose text wraps (BatteryHub's settings toggle): the box, the text beside it, and a
// click anywhere on the row flips it. The form registers `box` as the control.
class Toggle : public QWidget {
    Q_OBJECT
public:
    Toggle(const QString& text, bool checked, QWidget* parent = nullptr);
    QCheckBox* box;
    QLabel* label;

protected:
    void mouseReleaseEvent(QMouseEvent* e) override;
    void changeEvent(QEvent* e) override;
};

class SettingsForm : public QWidget {
    Q_OBJECT
public:
    SettingsForm(Settings* settings, DataStore* store, const Theme& theme, std::function<void()> save,
                 CredentialStore creds = {}, int page = 0, bool embedded = true, QWidget* parent = nullptr);
    void save();   // writes every field, calls the save callback, emits applied()
    void setPage(int page);
    int page() const { return m_page; }
    bool dirty() const { return m_dirty; }   // anything changed since the form was built or saved
    void setClean() { m_dirty = false; }
    QWidget* pageWidget(int i) const { return m_pages.value(i); }
    // Controls by the Python attribute names (tests and tooltips).
    QWidget* control(const QString& name) const { return m_controls.value(name); }
    QComboBox* liveMode = nullptr;
    QLineEdit* user = nullptr;
    QLineEdit* pw = nullptr;

Q_SIGNALS:
    void applied();
    void refreshRequested();
    void clearCacheRequested();
    void reopenRequested();
    void checkUpdatesRequested();
    void changed();   // the first edit (dirty became true)
    void pageChanged(int page);

private:
    QWidget* general();
    QWidget* appearance();
    QWidget* taskbar();
    QWidget* formula1();
    QWidget* live();
    QWidget* about();
    QLabel* hintLabel(const QString& text);
    void section(QFormLayout* f, const QString& text);
    Toggle* check(const QString& name, const QString& label, bool value);
    QComboBox* combo(const QString& name, const QList<std::pair<QString, QString>>& options, const QString& value);
    QPushButton* colorButton(const QString& name, const QString& value);
    void reg(const QString& name, QWidget* w) { m_controls.insert(name, w); }
    void applyTips();
    void tidyInputs();
    void watchChanges();
    void markDirty();
    void setAccent(const QString& c);
    void updatePreview();
    void pillValues(Settings& s) const;
    void exportSettings();
    void importSettings();
    QString colorOf(const QString& name) const;

    Settings* m_s;
    DataStore* m_store;
    const Theme& m_t;
    std::function<void()> m_save;
    CredentialStore m_creds;
    QMap<QString, QWidget*> m_controls;
    QList<QWidget*> m_pages;
    int m_page = 0;
    bool m_dirty = false;
    QString m_accent;
    QList<QSpinBox*> m_wxCustom;
    QWidget* m_wxCustomRow = nullptr;
    QMap<QString, QSpinBox*> m_offsets;
    QList<QWidget*> m_customRows;
    PillPreview* m_preview = nullptr;
    bool m_built = false;
};

// The same form in a window of its own, with Save / Cancel (kept as a fallback; the panel's
// Settings tab is the normal place).
class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(Settings* settings, DataStore* store, const Theme& theme, std::function<void()> save,
                   CredentialStore creds = {}, QWidget* parent = nullptr, int page = 0);
    void saveAndClose();   // the Save button
    QWidget* control(const QString& name) const { return form->control(name); }
    SettingsForm* form;
    QComboBox* liveMode = nullptr;
    QLineEdit* user = nullptr;
    QLineEdit* pw = nullptr;

Q_SIGNALS:
    void applied();
    void refreshRequested();
    void clearCacheRequested();
    void reopenRequested();
    void checkUpdatesRequested();
};

}  // namespace f1::ui
