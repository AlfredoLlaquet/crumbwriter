#include "mainwindow.h"
#include "manuscripteditor.h"
#include "manuscriptoptions.h"
#include "manuscriptsession.h"
#include "preferences.h"

#include <QAction>
#include <QComboBox>
#include <QFontComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolBar>

void MainWindow::buildFormattingTools(QToolBar *bar)
{
    auto group = [bar](int rightMargin) {
        auto *widget = new QWidget(bar);
        auto *layout = new QHBoxLayout(widget);
        layout->setContentsMargins(6, 0, rightMargin, 0);
        layout->setSpacing(6);
        bar->addWidget(widget);
        return layout;
    };
    auto *fonts = group(6);
    auto *label = new QLabel(tr("Font"));
    fonts->addWidget(label);
    fontFamily_ = new QFontComboBox;
    fontFamily_->setObjectName("writingFont");
    fontFamily_->setAccessibleName(tr("Font"));
    fontFamily_->setMaximumWidth(170);
    label->setBuddy(fontFamily_);
    fonts->addWidget(fontFamily_);
    fontSize_ = new QSpinBox;
    fontSize_->setObjectName("writingFontSize");
    fontSize_->setAccessibleName(tr("Font size"));
    fontSize_->setRange(6, 96);
    fonts->addWidget(fontSize_);
    auto *styles = group(0);
    auto *styleLabel = new QLabel(tr("Style"));
    styles->addWidget(styleLabel);
    lineStyle_ = new QComboBox;
    lineStyle_->setObjectName("lineStyle");
    lineStyle_->setAccessibleName(tr("Style"));
    styleLabel->setBuddy(lineStyle_);
    styles->addWidget(lineStyle_);
    auto changeFont = [this] {
        QFont font = fontFamily_->currentFont();
        font.setPointSize(fontSize_->value());
        prefs_.setWritingFont(font);
    };
    connect(fontFamily_, &QFontComboBox::currentFontChanged, this, changeFont);
    connect(fontSize_, &QSpinBox::valueChanged, this, changeFont);
    connect(lineStyle_, &QComboBox::activated, this, [this](int index) {
        applyLineStyle(lineStyle_->itemData(index).toInt());
    });
}

void MainWindow::syncFormattingTools()
{
    if (!active_ || styleActions_.isEmpty()) return;
    const auto font = prefs_.manuscriptFont();
    const QSignalBlocker familyGuard(fontFamily_), sizeGuard(fontSize_), styleGuard(lineStyle_);
    if (fontFamily_->currentFont().family() != font.family()) fontFamily_->setCurrentFont(font);
    fontSize_->setValue(font.pointSize());
    const auto &model = editor()->model();
    int line = editor()->textCursor().blockNumber();
    if (model.paragraphRole(line) == ParagraphRole::TitleMarker) --line;
    const bool generic = model.isGeneric();
    const bool first = line == 0;
    const int choices = !generic ? 0 : first ? 2 : 5;
    if (lineStyle_->count() != choices) {
        lineStyle_->clear();
        for (int style = -1; style <= 4; ++style) {
            auto *action = styleActions_.at(style + 1);
            const bool valid = generic && (style == 0 || (first ? style == -1 : style > 0));
            action->setEnabled(valid);
            if (valid) lineStyle_->addItem(action->text().section(u'\t', 0, 0), style);
        }
    } else if (!generic) {
        for (auto *action : styleActions_) action->setEnabled(false);
    }
    lineStyle_->setEnabled(generic);
    if (generic) {
        const int style = model.paragraphRole(line) == ParagraphRole::DocumentTitle ? -1 : model.headingLevel(line);
        lineStyle_->setCurrentIndex(lineStyle_->findData(style));
    }
}

void MainWindow::applyLineStyle(int style)
{
    QString error;
    if (!active_->options_->setLineStyle(style, &error) && !error.isEmpty()) showNotice(error);
    syncFormattingTools();
    editor()->setFocus();
}
