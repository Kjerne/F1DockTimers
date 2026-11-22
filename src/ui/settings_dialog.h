#pragma once
// Settings window (port of ui/settings_dialog.py): short hints on the page, the details in hover
// tooltips. Saves into the shared settings.json through the `save` callback; the OpenF1 password
// goes to Windows Credential Manager only.

#include "../services/credentials.h"
#include "../services/store.h"
#include "../settings.h"
#include "taskbar_pill.h"
#include "theme.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDoubleSpinBox>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QPushButton>
#include <QSpinBox>
#include <functional>

namespace f1::ui {

// Where the dialog reads and writes the OpenF1 password (tests pass fakes).
struct CredentialStore {
    std::function<std::optional<QString>(const QString&)> get = credentials::getPassword;
    std::function<bool(const QString&, const QString&)> set = credentials::setPassword;
    std::function<void(const QString&)> remove = credentials::deletePassword;
};

QString dialogStylesheet(const Theme& t);
QStringList zoneNames();
const QMap<QString, QString>& settingsTips();

class SettingsDialog : public QDialog {
    Q_OBJECT
public:
    SettingsDialog(Settings* settings, DataStore* store, const Theme& theme, std::function<void()> save,
                   CredentialStore creds = {}, QWidget* parent = nullptr);
    void saveAndClose();   // the Save button
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

protected:
    void showEvent(QShowEvent* e) override;

private:
    QWidget* general();
    QWidget* appearance();
    QWidget* taskbar();
    QWidget* formula1();
    QWidget* live();
    QWidget* about();
    QLabel* hintLabel(const QString& text);
    QCheckBox* check(const QString& name, const QString& label, bool value);
    QComboBox* combo(const QString& name, const QList<std::pair<QString, QString>>& options, const QString& value);
    QPushButton* colorButton(const QString& name, const QString& value);
    void reg(const QString& name, QWidget* w) { m_controls.insert(name, w); }
    void applyTips();
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
    QString m_accent;
    QList<QSpinBox*> m_wxCustom;
    QWidget* m_wxCustomRow = nullptr;
    QMap<QString, QSpinBox*> m_offsets;
    QList<QWidget*> m_customRows;
    PillPreview* m_preview = nullptr;
    bool m_built = false;
    bool m_wheelGuard = false;
};

}  // namespace f1::ui
