#include "preferences.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QFontComboBox>
#include <QFormLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QScreen>
#include <QSpinBox>
#include <QStandardPaths>
#include <QTreeWidget>
#include <QUrl>
#include <QVBoxLayout>

namespace {
QString preferencesFile()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath("crumbwriter/settings.ini");
}

struct HelpRow { const char *scope; const char *keys; const char *effect; };
constexpr HelpRow shortcuts[] = {
    {"Manuscript","Ctrl+N","Append the next crumb and go to its body"},
    {"Manuscript","Ctrl+1","Select the header and copy its lowercase form without its colon"},
    {"Manuscript","Ctrl+Q","Select and copy the whole crumb without its newline"},
    {"Manuscript","Ctrl+Y","Delete the current crumb when the manuscript has focus"},
    {"Manuscript","Ctrl+W","Delete the selected text or the word or space at the caret"},
    {"Manuscript","Ctrl+Z / Ctrl+Shift+Z","Undo / redo"},
    {"Manuscript","Ctrl+A / X / C / V","Select all / cut / copy / paste"},
    {"Manuscript","Tab / Shift+Tab","Next / previous title or body"},
    {"Manuscript","Up / Down","Move by one wrapped line, including virtual rows below the file"},
    {"Manuscript","Home / End","Beginning / end of the wrapped line"},
    {"Manuscript","Ctrl+Up / Ctrl+Down","Previous / next crumb"},
    {"Manuscript","Ctrl+Home / Ctrl+End","Beginning / end of this crumb"},
    {"Manuscript","Ctrl+Page Up / Ctrl+Page Down","Beginning of the file / end of the last crumb (RePág / AvPág)"},
    {"Manuscript","Ctrl+Enter / Alt+Enter","Center the current line / put this crumb at the top"},
    {"Navigation","F2 / Shift+F2","Back / forward to the exact caret position after a jump"},
    {"Navigation","F4 / Shift+F4","Next / previous numbering warning"},
    {"Navigation","Ctrl+Shift+J","Follow the crumb reference under the caret"},
    {"Navigation","Ctrl+L","Go to a crumb number"},
    {"Navigation","F6 / F7","Focus visit history / genres. Type part of a title or genre, then Enter"},
    {"Navigation","Shift+F7","Sort genres alphabetically or by number then name"},
    {"Navigation","F9 / Shift+F9","Show or hide the right / left panel"},
    {"Synonym lists","Alt+1 / Alt+2","Focus Characters / Recurring"},
    {"Synonym lists","Type to filter","Find entries by any synonym"},
    {"Synonym lists","Insert / F2 / Delete","Add / edit / remove a synonym group"},
    {"Synonym lists","Enter / Ctrl+Enter","Find all occurrences / insert the first synonym at the manuscript caret"},
    {"Synonym lists","F5 / Escape","Reload the list from disk / return to the manuscript"},
    {"Spelling","F8 / Shift+F8","Review next / previous spelling issue"},
    {"Spelling","Ctrl+Shift+D","Open the manuscript dictionary"},
    {"Spelling review","Enter / Alt+A","Apply the replacement / replace all occurrences of the misspelling"},
    {"Spelling review","Alt+I / Alt+L / Alt+D","Ignore once / ignore all / add the word to the manuscript dictionary"},
    {"Dictionary","Enter / Delete / F5","Add the typed word / remove the selected entry / reload from disk"},
    {"Bookmarks","Ctrl+B / Alt+B","Add a bookmark / open the Bookmarks window"},
    {"Bookmarks","Type in Filter, then Enter","Open a matching bookmark at its saved physical line"},
    {"Bookmarks window","Ctrl+F / Up / Down","Focus the filter / move through matching bookmarks"},
    {"Bookmarks window","F2 / Delete","Edit the selected bookmark or category / confirm removal"},
    {"Bookmarks window","F5 / Escape","Reload bookmarks from disk / close this window"},
    {"Find and Replace","Ctrl+F / Ctrl+G","Search forward / backward from the manuscript caret"},
    {"Find and Replace","Ctrl+H","Open Replace, then switch between Find and Replace with"},
    {"Find and Replace","F3 / Shift+F3","Repeat in the opening direction / opposite direction"},
    {"Find and Replace","Ctrl+P / Ctrl+R / Ctrl+L","Find plain text / regular expression / crumb number"},
    {"Find controls","Ctrl+M / Ctrl+W / Alt+W","Toggle match case / whole words / wrap"},
    {"While Find is open","Alt+Down","Open recent searches"},
    {"While Find is open","Enter / Shift+Enter","Next result in the opening direction / opposite direction"},
    {"Window","Escape","Close Find and clear the selection, or leave virtual space"},
    {"Window","Ctrl+O / Ctrl+Shift+N","Open a manuscript / create an untitled window"},
    {"Window","Ctrl+S / Ctrl+Shift+S","Save / Save As"},
    {"Window","Alt+W","Close the manuscript window. In Find controls, toggle Wrap"},
    {"Window","Ctrl+Shift+I","Show manuscript statistics. Ctrl+C in the dialog copies the figures"},
    {"Window","Ctrl+, / F10 / F1","Settings / Document menu / Keyboard Shortcuts"}
};
}

Preferences::Preferences(const QString &file)
    : settings_(file.isEmpty() ? preferencesFile() : file, QSettings::IniFormat)
{
}

bool Preferences::enabled(const QString &key) const
{
    return settings_.value("editor/" + key, true).toBool();
}

int Preferences::saveDelay() const
{
    return qBound(1, settings_.value("editor/save-delay", 1).toInt(), 30);
}

QFont Preferences::manuscriptFont() const
{
    const QString family = settings_.value("editor/font-family", "Serif").toString();
    QFont font(family, qBound(6, settings_.value("editor/font-size", 14).toInt(), 96));
    if (family.compare("Serif", Qt::CaseInsensitive) == 0) font.setStyleHint(QFont::Serif);
    return font;
}

QPalette Preferences::paletteForDesktop() const
{
    const QString choice = settings_.value("editor/theme", "desktop").toString();
    QPalette p = QApplication::palette();
    if (choice != "light" && choice != "dark") return p;
    const bool dark = choice == "dark";
    const QColor window(dark ? "#2b2d30" : "#f5f5f5");
    const QColor text(dark ? "#f2f2f2" : "#242424");
    p.setColor(QPalette::Window, window);
    p.setColor(QPalette::WindowText, text);
    p.setColor(QPalette::Base, QColor(dark ? "#202124" : "#ffffff"));
    p.setColor(QPalette::AlternateBase, window);
    p.setColor(QPalette::Text, text);
    p.setColor(QPalette::Button, window);
    p.setColor(QPalette::ButtonText, text);
    p.setColor(QPalette::Highlight, QColor(dark ? "#406895" : "#3b70ac"));
    p.setColor(QPalette::HighlightedText, Qt::white);
    p.setColor(QPalette::PlaceholderText, QColor(dark ? "#b1b2b5" : "#60666b"));
    for (auto role : {QPalette::Text, QPalette::WindowText, QPalette::ButtonText})
        p.setColor(QPalette::Disabled, role, QColor(dark ? "#95979a" : "#737678"));
    return p;
}

QString Preferences::lastDocument() const
{
    return QUrl(settings_.value("session/last-document").toString()).toLocalFile();
}

void Preferences::remember(const QString &path)
{
    if (path.isEmpty() || path == lastDocument()) return;
    settings_.setValue("session/last-document", QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded));
    settings_.sync();
}

void Preferences::set(const QString &key, const QVariant &value)
{
    settings_.setValue("editor/" + key, value);
    settings_.sync();
    if (changed) changed();
}

void Preferences::show(QWidget *parent)
{
    if (dialog_) { dialog_->raise(); dialog_->activateWindow(); return; }
    auto *dialog = new QDialog(parent);
    dialog_ = dialog;
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle("Crumb Writer Settings");
    auto *form = new QFormLayout(dialog);
    auto *family = new QFontComboBox(dialog);
    family->setCurrentFont(manuscriptFont());
    form->addRow("Font &family", family);
    QObject::connect(family, &QFontComboBox::currentFontChanged, dialog, [this](const QFont &font) {
        set("font-family", font.family());
    });
    auto spin = [&](const QString &caption, const QString &key, int value, int low, int high) {
        auto *field = new QSpinBox(dialog);
        field->setRange(low, high);
        field->setValue(value);
        form->addRow(caption, field);
        QObject::connect(field, &QSpinBox::valueChanged, dialog, [this, key](int n) { set(key, n); });
    };
    spin("Font &size (points)", "font-size", manuscriptFont().pointSize(), 6, 96);
    auto *theme = new QComboBox(dialog);
    theme->addItem("Follow desktop", "desktop");
    theme->addItem("Light", "light");
    theme->addItem("Dark", "dark");
    theme->setCurrentIndex(qMax(0, theme->findData(settings_.value("editor/theme", "desktop"))));
    form->addRow("&Appearance", theme);
    QObject::connect(theme, &QComboBox::currentIndexChanged, dialog, [this, theme](int i) {
        set("theme", theme->itemData(i));
    });
    auto toggle = [&](const QString &caption, const QString &key) {
        auto *check = new QCheckBox(caption, dialog);
        check->setChecked(enabled(key));
        form->addRow(check);
        QObject::connect(check, &QCheckBox::toggled, dialog, [this, key](bool on) { set(key, on); });
    };
    toggle("Use smart &punctuation", "smart-punctuation");
    toggle("Check American English &spelling", "spell-check");
    toggle("Make a &backup before editing", "backup");
    toggle("Save &live while typing", "live-save");
    spin("Save &interval (seconds)", "save-delay", saveDelay(), 1, 30);
    toggle("&Reopen the last manuscript at startup", "reopen-last");
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::close);
    dialog->show();
}

void showShortcutHelp(QWidget *parent)
{
    if (auto *existing = parent->findChild<QDialog *>("shortcutHelp", Qt::FindDirectChildrenOnly)) {
        existing->raise();
        existing->activateWindow();
        return;
    }
    auto *dialog = new QDialog(parent);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setObjectName("shortcutHelp");
    dialog->setWindowTitle("Keyboard Shortcuts — Crumb Writer");
    auto *layout = new QVBoxLayout(dialog);
    auto *filter = new QLineEdit(dialog);
    filter->setPlaceholderText("Filter shortcuts");
    filter->setAccessibleName("Filter shortcuts");
    auto *list = new QTreeWidget(dialog);
    list->setRootIsDecorated(false);
    list->setHeaderLabels({"Context", "Keys", "Action"});
    list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    for (const auto &row : shortcuts)
        new QTreeWidgetItem(list, {row.scope, row.keys, row.effect});
    layout->addWidget(filter);
    layout->addWidget(list);
    QObject::connect(filter, &QLineEdit::textChanged, list, [list](const QString &query) {
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            auto *row = list->topLevelItem(i);
            row->setHidden(!(row->text(0) + " " + row->text(1) + " " + row->text(2))
                           .contains(query, Qt::CaseInsensitive));
        }
    });
    QObject::connect(filter, &QLineEdit::returnPressed, list, [list] {
        for (int i = 0; i < list->topLevelItemCount(); ++i) {
            if (!list->topLevelItem(i)->isHidden()) {
                list->setCurrentItem(list->topLevelItem(i));
                list->setFocus();
                return;
            }
        }
    });
    const QSize available = parent->screen()->availableGeometry().size();
    dialog->resize(QSize(780, 460).boundedTo(available * 0.85));
    dialog->show();
    filter->setFocus();
}
