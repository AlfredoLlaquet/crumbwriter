#ifndef CRUMBWRITER_SYMBOLPICKER_H
#define CRUMBWRITER_SYMBOLPICKER_H

#include <QDialog>
#include <QRawFont>

class ManuscriptEditor;
class QComboBox;
class QLabel;
class QLineEdit;
class QListView;
class QPushButton;
class QSettings;
class SymbolListModel;
struct WriterSymbol;

class SymbolPicker final : public QDialog {
public:
    SymbolPicker(ManuscriptEditor *editor, QSettings &settings, QWidget *parent);
    void openPicker();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void done(int result) override;

private:
    const WriterSymbol *current() const;
    void refresh(const QString &keepCode = {});
    void updatePreview();
    void insertCurrent(bool keepOpen);
    void toggleFavorite();

    ManuscriptEditor *editor_;
    QSettings &settings_;
    QRawFont manuscriptFont_;
    QComboBox *theme_;
    QLineEdit *search_;
    QListView *grid_;
    SymbolListModel *model_;
    QLabel *preview_;
    QLabel *name_;
    QLabel *details_;
    QLabel *status_;
    QPushButton *favorite_;
    QPushButton *insert_;
};

#endif
