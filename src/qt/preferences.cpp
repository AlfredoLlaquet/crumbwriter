#include "preferences.h"
#include "editorcolors.h"

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
#include <QTabWidget>
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
    {"Manuscript modes","First line","A // outside a URL selects Crumb mode. Other manuscripts use Generic mode. The status bar shows the current mode."},
    {"Generic mode","Titles","The first paragraph is the document title if it lacks a period, is all caps, or precedes a blank line. Blank lines on both sides mark chapter titles."},
    {"Manuscript","Ctrl+N","Append the next crumb or a new prose paragraph"},
    {"Crumb mode","Ctrl+1","Select the header and copy its lowercase form without its colon"},
    {"Manuscript","Ctrl+Q / Ctrl+Y","Select and copy the current crumb or paragraph without its newline / delete it"},
    {"Manuscript","Ctrl+W","Delete the selected text or the word or space at the caret"},
    {"Manuscript","Ctrl+Z / Ctrl+Shift+Z","Undo / redo one run of typing, Backspaces, or Deletes"},
    {"Manuscript","Ctrl+A / X / C / V","Select all / cut / copy / paste"},
    {"Crumb mode","Tab / Shift+Tab","Next / previous crumb title or body"},
    {"Generic mode","Tab / Shift+Tab","Next / previous document or chapter title"},
    {"Manuscript","Up / Down","Move by one wrapped line, including virtual rows below the file"},
    {"Manuscript","Home / End","Beginning / end of the wrapped line"},
    {"Manuscript","Ctrl+Up / Ctrl+Down","Previous / next crumb or paragraph"},
    {"Manuscript","Ctrl+Home / Ctrl+End","Beginning / end of this crumb or paragraph"},
    {"Manuscript","Ctrl+Page Up / Ctrl+Page Down","Beginning / end of the manuscript (RePág / AvPág)"},
    {"Selection","Ctrl+Shift+Home / End","Select to the beginning / end of the current crumb or paragraph"},
    {"Selection","Ctrl+Shift+Up / Down","Extend or shrink the selection to the previous / next crumb or paragraph"},
    {"Selection","Ctrl+Shift+Page Up / Page Down","Select to the beginning / end of the manuscript"},
    {"Movement selection","Ctrl+Space","Type a movement, then its count. The bottom bar shows the command. Each keystroke previews the selection from the original caret."},
    {"Movement selection","Enter / Escape / Backspace","Accept and keep the selection / restore the original selection / edit the command and preview again"},
    {"Movement selection","h / l · j / k · gj / gk","Left / right one character · next / previous physical line · next / previous wrapped row. Examples: j2, gj2"},
    {"Movement selection","w / b / e · W / B / E","Next word / previous word / through word end. Uppercase treats punctuation as part of the word"},
    {"Movement selection","0 / ^ / $","Beginning / first nonblank / end of the physical line"},
    {"Movement selection","gg / G · G42","First / last physical line · go to line 42"},
    {"Movement selection","f / t · F / T","Type a character. Select through it / stop before it. Uppercase searches backward"},
    {"Movement selection","t-2 · ; / ,","Up to the second hyphen, excluding it · repeat the last character find / reverse it"},
    {"Movement selection","q / Q · qth*2","Through the forward / backward phrase, ignoring case. qth*2 reaches the second th. Type \\* for a literal asterisk."},
    {"Manuscript","Ctrl+Enter / Alt+Enter","Center the current visual line / move the start of this crumb or paragraph to the top"},
    {"Navigation","F2 or Alt+Left / Shift+F2 or Alt+Right","Go back / forward in the shared history of references, names, bookmarks, and search results"},
    {"Crumb mode","F4 / Shift+F4","Next / previous numbering warning"},
    {"Navigation","Ctrl+Shift+J / Ctrl+click","Follow a numbered reference in Crumb mode or find the preceding mention of a name in either mode. Back returns to the original writing position"},
    {"Names","F6 / Shift+F6","Previous / next mention of any synonym in the group under the caret. Ctrl+Shift+click follows a name forward"},
    {"Names","F5 in manuscript","Accept a suggested ending or open name completions. Use arrows and Enter to choose. Escape dismisses"},
    {"Names","Ctrl+, → Names","Choose completion and underlining for Characters and Recurring, the prefix length, and the underline color"},
    {"Symbols","Ctrl+U","Open Insert Symbol. Search by description, theme, symbol, or Unicode code point"},
    {"Insert Symbol","Alt+S / Alt+T / Down","Focus Search / Theme / the grid. Browse symbols with the arrow keys"},
    {"Insert Symbol","Enter / Ctrl+Enter / Ctrl+D","Insert and close / insert and continue browsing / toggle Favorite. Escape closes"},
    {"Editing","Ctrl+, → Writing","Copy deleted text to the clipboard is on by default. Each run of Backspaces or Deletes builds a word in reading order"},
    {"Navigation","Ctrl+L","Go to a crumb number in Crumb mode or a physical line in Generic mode"},
    {"Panels","Ctrl+Shift+1 / Ctrl+Shift+2","Search Characters / Recurring by any synonym"},
    {"Panels","Ctrl+Shift+3 / Ctrl+Shift+4","Search visit history / genres or manuscript titles"},
    {"Panels","Type in Search · Up / Down","Highlight the first matching entry. Move among matches while every entry stays visible"},
    {"History, Genres, Titles","Enter / Ctrl+Enter","Clear the search and visit the selected entry. In Genres, append the next crumb in the selected genre"},
    {"Navigation","Shift+F7","Switch between alphabetical and genre-number order, or alphabetical and document order for titles"},
    {"Panels","F9 / Shift+F9","Show or hide navigation / synonym lists"},
    {"Layout","Ctrl+Shift+F10","Arrange panels and columns in the current window. The View menu can restore all default layouts"},
    {"Synonym lists","Insert / F2 / Delete","Add from the manuscript selection / edit / remove a synonym group"},
    {"Synonym lists","Enter / Ctrl+Enter","Find all occurrences / insert the first synonym at the manuscript caret. Both clear the panel search"},
    {"Synonym lists","F5 / Escape","Reload the list from disk / return to the manuscript"},
    {"Spelling","F8 / Shift+F8","Review next / previous spelling issue"},
    {"Spelling","Ctrl+Shift+D","Open the manuscript dictionary"},
    {"Spelling review","Enter / Alt+A","Apply the replacement / replace all occurrences of the misspelling"},
    {"Spelling review","Alt+I / Alt+L / Alt+D","Ignore once / ignore all / add the word to the manuscript dictionary"},
    {"Dictionary","Enter / Delete / F5","Add the typed word / remove the selected entry / reload from disk"},
    {"Bookmarks","Ctrl+B / Alt+B","Add a bookmark / open the Bookmarks window"},
    {"Bookmarks","Type in Filter, then Enter","Open a matching bookmark. Bookmarks follow edits above them. Deleting a bookmarked line moves its bookmark to the next surviving line"},
    {"Bookmarks window","Ctrl+F / Up / Down","Focus the filter / move through matching bookmarks"},
    {"Bookmarks window","F2 / Delete","Edit the selected bookmark or category / confirm removal"},
    {"Bookmarks window","F5 / Escape","Reload bookmarks from disk / close this window"},
    {"Find and Replace","Ctrl+F / Ctrl+G","Search forward / backward from the manuscript caret"},
    {"Find and Replace","Ctrl+H","Open Replace, then switch between Find and Replace with"},
    {"Find and Replace","F3 / Shift+F3","Repeat in the opening direction / opposite direction"},
    {"Find and Replace","Ctrl+P / Ctrl+R / Ctrl+L","Find plain text / regular expression / crumb or file line number"},
    {"Find controls","Ctrl+M / Ctrl+W / Alt+W","Toggle match case / whole words / wrap"},
    {"While Find is open","Alt+Down","Open recent searches"},
    {"While Find is open","Enter / Shift+Enter","Next result in the opening direction / opposite direction"},
    {"Window","Escape","Close Find and clear the selection, or leave virtual space"},
    {"Window","Ctrl+O / Ctrl+Shift+N","Open a manuscript / create an untitled window"},
    {"Window","Ctrl+S / Ctrl+Shift+S","Save / Save As"},
    {"Window","Alt+W","Close the manuscript window. In Find controls, toggle Wrap"},
    {"Window","Ctrl+Shift+I","Show manuscript statistics. Ctrl+C in the dialog copies the figures"},
    {"Window","F10, A","Open About Crumb Writer. Enter opens the focused repository link"},
    {"About","Alt+L","View License offline. Scroll or copy the full terms. Escape returns to About"},
#ifdef Q_OS_MACOS
    {"Mac keyboard","Control / Option","Ctrl means the physical Control key. Option is Alt. On a media-key keyboard, hold Fn for F1 through F10"},
    {"Mac keyboard","Control+Shift+Space","Start a movement selection when Control+Space switches input sources"},
    {"Mac keyboard","Command+A / X / C / V","Select all / cut / copy / paste"},
    {"Mac keyboard","Command+Z / Command+Shift+Z","Undo / redo"},
    {"Mac keyboard","Command+N / O / S / Shift+S / F","New window / open / save / Save As / find"},
    {"Mac keyboard","Command+,","Open Settings"},
    {"Mac keyboard","Command+W / Command+Q","Close this window / quit Crumb Writer, with normal save confirmation"},
    {"Mac navigation","Command+click / Command+Shift+click","Follow a reference or the previous / next mention of a name"},
    {"Mac keyboard","Control+Shift+B / Control+Shift+W","Open Bookmarks / toggle Wrap in Find. Option combinations can still type special characters"},
    {"Mac synonym lists","Control+I","Add a group from the manuscript selection while Characters or Recurring has focus. Works in its search field or list"},
    {"Mac lists","Backspace","Remove the selected entry in a focused synonym, bookmark, or manuscript dictionary list. In search and edit fields, Backspace edits the text"},
#endif
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
    return EditorColors::explicitTheme(p, choice == "dark");
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
    dialog->setObjectName("preferencesDialog");
    dialog->setWindowTitle("Crumb Writer Settings");
    auto *outer = new QVBoxLayout(dialog);
    auto *tabs = new QTabWidget(dialog);
    auto *writing = new QWidget(tabs);
    auto *form = new QFormLayout(writing);
    tabs->addTab(writing, "&Writing");
    tabs->addTab(synonymPage(tabs), "&Names");
    outer->addWidget(tabs);
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
    toggle("&Copy deleted text to the clipboard", "copy-deleted");
    toggle("Check American English &spelling", "spell-check");
    toggle("Make a &backup before editing", "backup");
    toggle("Save &live while typing", "live-save");
    spin("Save &interval (seconds)", "save-delay", saveDelay(), 1, 30);
    toggle("&Reopen the last manuscript at startup", "reopen-last");
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, dialog);
    outer->addWidget(buttons);
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
    list->setObjectName("shortcutTable");
    list->setRootIsDecorated(false);
    list->setWordWrap(true);
    list->setHeaderLabels({"Context", "Keys", "Action"});
    list->header()->setSectionResizeMode(0, QHeaderView::ResizeToContents);
    list->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    for (const auto &row : shortcuts) {
        auto *item = new QTreeWidgetItem(list, {row.scope, row.keys, row.effect});
        item->setToolTip(2, item->text(2));
    }
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
