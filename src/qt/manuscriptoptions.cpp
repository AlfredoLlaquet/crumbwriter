#include "manuscriptoptions.h"
#include "backupcomparison.h"
#include "documentstore.h"
#include "guillemetmarkup.h"
#include "genericparagraph.h"
#include <QTextBlock>
#include "manuscripteditor.h"

#include <QCheckBox>
#include <QDialogButtonBox>
#include <QFileInfo>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

namespace {
const QByteArray literalQuotesKey("literal-guillemets");

QString documentLabel(const DocumentStore *store)
{
    return QObject::tr("Generic mode settings for %1").arg(store->path().isEmpty()
             ? QObject::tr("this untitled manuscript") : QFileInfo(store->path()).fileName());
}

QByteArray keyOf(const QByteArray &line)
{
    const auto equals = line.indexOf('=');
    return equals < 0 ? QByteArray() : line.left(equals).trimmed();
}

bool enabled(const QByteArray &bytes, const QByteArray &key)
{
    bool result = false;
    for (const QByteArray &line : bytes.split('\n'))
        if (keyOf(line) == key)
            result = line.mid(line.indexOf('=') + 1).trimmed() == "true";
    return result;
}

QByteArray setFlag(const QByteArray &original, const QByteArray &key, bool value)
{
    if (enabled(original, key) == value) return original;
    auto lines = original.split('\n');
    if (original.isEmpty()) lines.clear();
    const QByteArray entry = key + '=' + (value ? "true" : "false");
    bool found = false;
    for (auto &line : lines) {
        if (keyOf(line) != key) continue;
        line = entry;
        found = true;
    }
    if (!found) {
        qsizetype pos = lines.size();
        while (pos && lines[pos - 1].isEmpty()) --pos;
        lines.insert(pos, entry);
    }
    auto bytes = lines.join('\n');
    if (!bytes.endsWith('\n')) bytes += '\n';
    return bytes;
}
}

ManuscriptOptions::ManuscriptOptions(ManuscriptEditor *editor, DocumentStore *store, QObject *parent)
    : QObject(parent), editor_(editor), store_(store) {}

void ManuscriptOptions::apply()
{
    editor_->setLiteralGuillemets(enabled(config_.bytes(), literalQuotesKey));
}

bool ManuscriptOptions::resetForPath(const QString &path, QString *error)
{
    if (settings_) settings_->close();
    if (comparison_) comparison_->close();
    const bool success = config_.openManuscript(path, error);
    apply();
    return success;
}

bool ManuscriptOptions::carryToPath(const QString &path, QString *error)
{
    const bool success = config_.saveAs(path, error);
    if (success) apply();
    if (settings_) {
        auto *label = settings_->findChild<QLabel *>("settingsManuscriptName");
        if (label) label->setText(documentLabel(store_));
    }
    return success;
}

bool ManuscriptOptions::flush(QString *error) { return config_.flush(error); }

bool ManuscriptOptions::copyToDraft(const QString &draft, QString *error)
{
    if (draft.isEmpty()) return false;
    if (recoveryConfig_.manuscriptPath() != draft && !recoveryConfig_.openManuscript(draft, error))
        return false;
    return recoveryConfig_.bytes() == config_.bytes() || recoveryConfig_.write(config_.bytes(), error);
}

// -1 is the main title, 0 ordinary prose, and 1–4 section headings.
void ManuscriptOptions::setLineStyle(int style)
{
    auto &model = editor_->model();
    if (!model.isGeneric() || editor_->isReadOnly()) return;
    auto caret = editor_->textCursor();
    auto block = caret.block();
    if (model.paragraphRole(block.blockNumber()) == ParagraphRole::TitleMarker) block = block.previous();
    const bool first = block.blockNumber() == 0;
    if (first && style == 1) style = -1;
    if (style < -1 || style > 4 || (first && style > 0) || (!first && style == -1) ||
        (style && !GenericParagraph::canHaveMarker(block.text()))) return;
    const auto next = block.next();
    const bool marker = next.isValid() && GenericParagraph::canHaveMarker(block.text()) &&
        GenericParagraph::markerAllowed(block.blockNumber(), GenericParagraph::markerLevel(next.text()));
    if (!style && !marker) return;
    QTextCursor edit(block);
    edit.movePosition(QTextCursor::EndOfBlock);
    edit.beginEditBlock();
    if (marker) {
        edit.setPosition(next.position() + next.length() - 1, QTextCursor::KeepAnchor);
        edit.removeSelectedText();
    }
    if (style) {
        edit.insertBlock();
        const QChar mark = QStringLiteral("=-~.").at(qMax(1, style) - 1);
        edit.insertText(QString(GenericParagraph::markerLength(block.text(), editor_->literalGuillemets()), mark));
    }
    edit.endEditBlock();
    model.confirmMarkerOutside(-1);
    if (caret.block() != block) caret = QTextCursor(block);
    editor_->setTextCursor(caret);
    if (onChanged) onChanged();
}

void ManuscriptOptions::showSettings()
{
    if (!settings_) {
        auto *dialog = new QDialog(editor_);
        settings_ = dialog;
        dialog->setAttribute(Qt::WA_DeleteOnClose);
        dialog->setObjectName("manuscriptSettingsDialog");
        dialog->setWindowTitle(tr("Manuscript Settings"));
        auto *layout = new QVBoxLayout(dialog);
        auto *context = new QLabel(documentLabel(store_), dialog);
        context->setObjectName("settingsManuscriptName");
        context->setTextFormat(Qt::PlainText);
        context->setWordWrap(true);
        layout->addWidget(context);
        auto *literal = new QCheckBox(tr("Display &guillemets literally"), dialog);
        literal->setObjectName("literalGuillemets");
        literal->setChecked(enabled(config_.bytes(), literalQuotesKey));
        layout->addWidget(literal);
        auto *error = new QLabel(dialog);
        error->setTextFormat(Qt::PlainText);
        error->setWordWrap(true);
        error->setVisible(false);
        layout->addWidget(error);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, dialog);
        layout->addWidget(buttons);
        connect(buttons, &QDialogButtonBox::rejected, dialog, &QDialog::reject);
        connect(buttons, &QDialogButtonBox::accepted, dialog, [this, dialog, literal, error] {
            const bool literalChanged = editor_->literalGuillemets() != literal->isChecked();
            const auto bytes = setFlag(config_.bytes(), literalQuotesKey, literal->isChecked());
            QString message;
            const bool changed = bytes != config_.bytes();
            if (changed && !config_.write(bytes, &message)) {
                error->setText(message);
                error->show();
                return;
            }
            apply();
            if (literalChanged && editor_->model().isGeneric()) {
                for (const int line : editor_->model().titleLines()) {
                    const auto title = editor_->document()->findBlockByNumber(line);
                    const auto marker = title.next();
                    if (marker.isValid() && editor_->model().paragraphRole(line + 1) == ParagraphRole::TitleMarker
                        && !GuillemetMarkup::pairs(title.text()).isEmpty()) {
                        store_->markEdited();
                        break;
                    }
                }
            }
            if (changed && onChanged) onChanged();
            dialog->accept();
        });
        literal->setFocus();
    }
    settings_->show();
    settings_->raise();
    settings_->activateWindow();
}

void ManuscriptOptions::showBackupComparison()
{
    if (!comparison_) comparison_ = new BackupComparison(editor_, store_);
    static_cast<BackupComparison *>(comparison_.data())->refresh();
    comparison_->show();
    comparison_->raise();
    comparison_->activateWindow();
}
