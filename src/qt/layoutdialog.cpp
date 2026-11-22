#include "layoutparts.h"

#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QVBoxLayout>

namespace {
class LayoutEditor final : public QDialog {
public:
    LayoutEditor(QWidget *owner, const QList<LayoutPart *> &parts)
        : QDialog(owner), parts_(parts), areas_(new QComboBox(this)), rows_(new QListWidget(this)),
          earlier_(new QPushButton(tr("Move &Earlier"), this)), later_(new QPushButton(tr("Move &Later"), this)),
          dimension_(new QLabel(this)), pixels_(new QSpinBox(this))
    {
        setObjectName("layoutEditor");
        setWindowTitle(tr("Arrange Panels and Columns"));
        setAttribute(Qt::WA_DeleteOnClose);
        auto *layout = new QVBoxLayout(this);
        auto *choose = new QFormLayout;
        choose->addRow(tr("&Area"), areas_);
        areas_->setObjectName("layoutArea");
        for (auto *part : parts_) areas_->addItem(part->label);
        layout->addLayout(choose);
        auto *label = new QLabel(tr("&Items in display order (Space shows or hides an item)"), this);
        label->setBuddy(rows_);
        rows_->setObjectName("layoutItems");
        layout->addWidget(label);
        layout->addWidget(rows_);
        auto *adjustments = new QHBoxLayout;
        adjustments->addWidget(earlier_);
        adjustments->addWidget(later_);
        adjustments->addStretch(1);
        dimension_->setBuddy(pixels_);
        adjustments->addWidget(dimension_);
        adjustments->addWidget(pixels_);
        earlier_->setObjectName("layoutEarlier");
        later_->setObjectName("layoutLater");
        pixels_->setObjectName("layoutSize");
        pixels_->setRange(1, 100000);
        pixels_->setSuffix(tr(" px"));
        pixels_->setKeyboardTracking(false);
        for (auto *b : {earlier_, later_}) b->setAutoDefault(false);
        layout->addLayout(adjustments);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Close, this);
        connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::close);
        layout->addWidget(buttons);

        connect(areas_, &QComboBox::currentIndexChanged, this, [this] { fill(0); });
        connect(rows_, &QListWidget::currentRowChanged, this, [this] { controls(); });
        connect(rows_, &QListWidget::itemChanged, this, [this](QListWidgetItem *item) {
            const int row = rows_->row(item);
            part()->setVisible(row, item->checkState() == Qt::Checked);
            fill(row);
        });
        connect(earlier_, &QPushButton::clicked, this, [this] { moveRow(-1); });
        connect(later_, &QPushButton::clicked, this, [this] { moveRow(1); });
        connect(pixels_, &QSpinBox::valueChanged, this, [this](int n) {
            if (rows_->currentRow() >= 0) part()->resize(rows_->currentRow(), n);
            controls();
        });
        fill(0);
        rows_->setFocus();
    }
private:
    LayoutPart *part() const { return parts_.at(areas_->currentIndex()); }
    void fill(int active)
    {
        const QSignalBlocker blocks(rows_);
        rows_->clear();
        auto *p = part();
        for (int row = 0; row < p->count(); ++row) {
            auto *item = new QListWidgetItem(p->title(row), rows_);
            item->setCheckState(p->visible(row) ? Qt::Checked : Qt::Unchecked);
            item->setFlags(item->flags().setFlag(Qt::ItemIsUserCheckable, p->canHide(row)));
        }
        rows_->setCurrentRow(active);
        controls();
    }
    void controls()
    {
        const int row = rows_->currentRow();
        earlier_->setEnabled(row > 0);
        later_->setEnabled(row >= 0 && row + 1 < part()->count());
        dimension_->setText(part()->vertical() ? tr("&Height") : tr("&Width"));
        pixels_->setEnabled(row >= 0 && part()->visible(row));
        const QSignalBlocker block(pixels_);
        if (row >= 0) pixels_->setValue(part()->size(row));
    }
    void moveRow(int by)
    {
        const int row = rows_->currentRow(), to = row + by;
        if (to < 0 || to >= part()->count()) return;
        part()->move(row, to);
        fill(to);
        rows_->setFocus();
    }
    QList<LayoutPart *> parts_;
    QComboBox *areas_;
    QListWidget *rows_;
    QPushButton *earlier_, *later_;
    QLabel *dimension_;
    QSpinBox *pixels_;
};
}

void showLayoutEditor(QWidget *owner, const QList<LayoutPart *> &parts)
{
    if (parts.isEmpty()) return;
    auto *window = owner->findChild<QDialog *>("layoutEditor", Qt::FindDirectChildrenOnly);
    if (!window) window = new LayoutEditor(owner, parts);
    window->show();
    window->raise();
    window->activateWindow();
}
