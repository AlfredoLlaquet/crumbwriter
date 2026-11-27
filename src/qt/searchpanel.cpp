#include "searchpanel.h"
#include "editorcolors.h"
#include "crumbmodel.h"
#include "manuscripteditor.h"
#include "manuscripttext.h"

#include <QApplication>
#include <QCheckBox>
#include <QFrame>
#include <QFutureWatcher>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QListView>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPromise>
#include <QPointer>
#include <QPushButton>
#include <QScreen>
#include <QScrollBar>
#include <QScopedValueRollback>
#include <QStackedWidget>
#include <QStringListModel>
#include <QTextBlock>
#include <QThreadPool>
#include <QTimer>
#include <QToolButton>
#include <algorithm>

namespace {

constexpr int backgroundThreshold = 32768;

template<class Result, class Work, class Done>
void runInBackground(QObject *receiver, Work work, Done done)
{
    // A watcher can disappear with its window. The worker only owns immutable
    // inputs and its promise, so it never calls back into a destroyed widget.
    auto promise = std::make_shared<QPromise<Result>>();
    promise->start();
    auto *watcher = new QFutureWatcher<Result>(receiver);
    QObject::connect(watcher, &QFutureWatcher<Result>::finished, receiver, [watcher, done] {
        auto result = watcher->result();
        watcher->deleteLater();
        done(std::move(result));
    });
    watcher->setFuture(promise->future());
    QThreadPool::globalInstance()->start([promise, work] {
        promise->addResult(work());
        promise->finish();
    });
}

QString modeName(cwsearch::Mode mode, bool generic)
{
    switch (mode) {
    case cwsearch::Mode::Regex: return QStringLiteral("Regular expression");
    case cwsearch::Mode::Number: return generic ? QStringLiteral("Line number") : QStringLiteral("Crumb number");
    case cwsearch::Mode::Text: return QStringLiteral("Plain text");
    }
    return {};
}

QPushButton *button(const QString &text, const char *name, const QString &description)
{
    auto *result = new QPushButton(text);
    result->setObjectName(QString::fromLatin1(name));
    result->setAccessibleName(description);
    result->setToolTip(description);
    result->setAutoDefault(false);
    result->setSizePolicy(QSizePolicy::Maximum, QSizePolicy::Fixed);
    return result;
}

QString plural(int count, const char *singular, const char *multiple)
{
    return QStringLiteral("%1 %2").arg(count).arg(QString::fromLatin1(count == 1 ? singular : multiple));
}

} // namespace

SearchPanel::SearchPanel(ManuscriptEditor *editor, QSettings *settings, QWidget *parent)
    : QWidget(parent), editor(editor), history(settings), origin(editor->textCursor())
{
    buildUi();
    scanDelay = new QTimer(this);
    refreshDelay = new QTimer(this);
    highlightDelay = new QTimer(this);
    for (auto timer : {scanDelay, refreshDelay, highlightDelay})
        timer->setSingleShot(true);
    connect(scanDelay, &QTimer::timeout, this, &SearchPanel::collectMatches);
    connect(refreshDelay, &QTimer::timeout, this, [this] { if (!isHidden()) rebuild(false); });
    connect(highlightDelay, &QTimer::timeout, this, &SearchPanel::updateHighlights);
    connect(editor->verticalScrollBar(), &QScrollBar::valueChanged, this, [this] { scheduleHighlights(); });
    editor->viewport()->installEventFilter(this);
    connect(editor, &QPlainTextEdit::cursorPositionChanged, this, [this] {
        if (movingCursor || replacing)
            return;
        hit = {};
        origin = this->editor->textCursor();
        origin.clearSelection();
        recorded = false;
        previewPending = false;
        pendingSteps.clear();
        if (!isHidden())
            showStatus();
    });
    auto invalidatePaper = [this] {
        paperDirty = true;
        if (!visibleHits.isEmpty()) {
            visibleHits.clear();
            this->editor->setSearchHighlights({});
        }
        hit = {};
        invalidateIndex();
        cancelPreview();
        if (!isHidden() && !replacing)
            refreshDelay->start(0);
    };
    connect(editor->document(), &QTextDocument::contentsChange, this, [invalidatePaper](int, int removed, int added) {
        if (removed || added) invalidatePaper();
    });
    editor->onMarkupVisibilityChanged = [panel = QPointer<SearchPanel>(this), invalidatePaper] {
        if (panel) invalidatePaper();
    };
    editor->model().observe(this, [this](const CrumbChange &change) {
        if (change.modeChanged) updateControls();
    });
    updateControls();
    hide();
}

SearchPanel::~SearchPanel()
{
    if (indexCancel)
        indexCancel->store(true);
    if (replacementCancel)
        replacementCancel->store(true);
}

void SearchPanel::buildUi()
{
    setObjectName(QStringLiteral("cw-search-panel"));
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(4, 2, 4, 2);
    layout->setSpacing(2);
    auto *row = new QHBoxLayout;
    row->setSpacing(3);
    layout->addLayout(row);
    modes = new QToolButton;
    modes->setObjectName(QStringLiteral("cw-search-mode"));
    modes->setAccessibleName(QStringLiteral("Search mode"));
    modes->setPopupMode(QToolButton::InstantPopup);
    modeMenu = new QMenu(modes);
    for (Mode mode : {Mode::Text, Mode::Regex, Mode::Number}) {
        auto *action = modeMenu->addAction(modeName(mode, editor->model().isGeneric()));
        action->setCheckable(true);
        connect(action, &QAction::triggered, this, [this, mode] { setMode(mode); });
    }
    modes->setMenu(modeMenu);
    row->addWidget(modes);
    queryLabel = new QLabel;
    row->addWidget(queryLabel);
    entries = new QStackedWidget;
    entries->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    entries->setMinimumWidth(16);
    findEntry = new QLineEdit;
    findEntry->setObjectName(QStringLiteral("cw-find"));
    numberEntry = new QLineEdit;
    numberEntry->setObjectName(QStringLiteral("cw-crumb-number"));
    entries->addWidget(findEntry);
    entries->addWidget(numberEntry);
    row->addWidget(entries, 1);
    previous = button(QStringLiteral("↑"), "cw-search-previous", QStringLiteral("Previous match"));
    next = button(QStringLiteral("↓"), "cw-search-next", QStringLiteral("Next match"));
    recent = button(QStringLiteral("▾"), "cw-search-recent", QStringLiteral("Recent searches (Alt+Down)"));
    auto *close = button(QStringLiteral("×"), "cw-search-close", QStringLiteral("Close search (Escape)"));
    for (auto control : {previous, next, recent, close}) {
        control->setFixedWidth(std::max(25, fontMetrics().horizontalAdvance(u'×') + 14));
        row->addWidget(control);
    }
    connect(previous, &QPushButton::clicked, this, [this] { step(true); });
    connect(next, &QPushButton::clicked, this, [this] { step(options.mode == Mode::Number && backwards); });
    connect(recent, &QPushButton::clicked, this, &SearchPanel::showRecent);
    connect(close, &QPushButton::clicked, this, [this] { closeSearch(); });

    auto *toggles = new QHBoxLayout;
    toggles->setSpacing(6);
    layout->addLayout(toggles);
    matchCase = new QCheckBox(QStringLiteral("Case"));
    matchCase->setObjectName(QStringLiteral("cw-search-case"));
    matchCase->setToolTip(QStringLiteral("Match case (Ctrl+M)"));
    wholeWords = new QCheckBox(QStringLiteral("Words"));
    wholeWords->setObjectName(QStringLiteral("cw-search-word"));
    wholeWords->setToolTip(QStringLiteral("Whole words (Ctrl+W)"));
    wrap = new QCheckBox(QStringLiteral("Wrap"));
    wrap->setObjectName(QStringLiteral("cw-search-wrap"));
#ifdef Q_OS_MACOS
    wrap->setToolTip(QStringLiteral("Wrap around document (Ctrl+Shift+W)"));
#else
    wrap->setToolTip(QStringLiteral("Wrap around document (Alt+W)"));
#endif
    for (auto control : {matchCase, wholeWords, wrap}) {
        toggles->addWidget(control);
        connect(control, &QCheckBox::toggled, this, [this] {
            if (settingControls)
                return;
            options.matchCase = matchCase->isChecked();
            options.wholeWords = wholeWords->isChecked();
            options.wrap = wrap->isChecked();
            rebuild(true);
        });
    }
    status = new QLabel;
    status->setTextFormat(Qt::PlainText);
    status->setObjectName(QStringLiteral("cw-search-status"));
    status->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
    toggles->addWidget(status, 1);

    replaceRow = new QWidget;
    auto *tools = new QHBoxLayout(replaceRow);
    tools->setContentsMargins(0, 0, 0, 0);
    tools->setSpacing(3);
    auto *label = new QLabel(QStringLiteral("Replace with"));
    tools->addWidget(label);
    replacement = new QLineEdit;
    replacement->setObjectName(QStringLiteral("cw-replacement"));
    replacement->setAccessibleName(QStringLiteral("Replace with"));
    replacement->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Fixed);
    replacement->setMinimumWidth(16);
    label->setBuddy(replacement);
    tools->addWidget(replacement, 1);
    replaceButton = button(QStringLiteral("Replace"), "cw-replace-one", QStringLiteral("Replace this match"));
    allButton = button(QStringLiteral("All…"), "cw-replace-all", QStringLiteral("Preview all replacements"));
    cancelButton = button(QStringLiteral("Cancel"), "cw-cancel-preview", QStringLiteral("Cancel replacement preview"));
    tools->addWidget(replaceButton);
    tools->addWidget(allButton);
    tools->addWidget(cancelButton);
    cancelButton->hide();
    connect(replaceButton, &QPushButton::clicked, this, &SearchPanel::replaceOne);
    connect(allButton, &QPushButton::clicked, this, &SearchPanel::replaceAll);
    connect(cancelButton, &QPushButton::clicked, this, [this] { cancelPreview(); setNotice({}); });
    connect(replacement, &QLineEdit::textChanged, this, [this] { cancelPreview(); setNotice({}); });
    connect(replacement, &QLineEdit::returnPressed, this, &SearchPanel::replaceOne);
    layout->addWidget(replaceRow);
    preview = new QPlainTextEdit;
    preview->setObjectName(QStringLiteral("cw-replace-preview"));
    preview->setAccessibleName(QStringLiteral("Replacement preview"));
    preview->setReadOnly(true);
    preview->setUndoRedoEnabled(false);
    preview->setTabChangesFocus(true);
    preview->setMinimumHeight(fontMetrics().height() * 2);
    preview->setMaximumHeight(fontMetrics().height() * 6);
    preview->hide();
    layout->addWidget(preview);

    for (auto field : {findEntry, numberEntry}) {
        connect(field, &QLineEdit::textChanged, this, [this] { if (!settingControls) rebuild(true); });
        connect(field, &QLineEdit::returnPressed, this, [this] { repeat(); });
        field->installEventFilter(this);
    }

    recentPopup = new QFrame(this, Qt::Popup);
    recentPopup->setFrameShape(QFrame::StyledPanel);
    auto *popupLayout = new QVBoxLayout(recentPopup);
    popupLayout->setContentsMargins(2, 2, 2, 2);
    recentList = new QListView;
    recentList->setEditTriggers(QAbstractItemView::NoEditTriggers);
    recentList->setObjectName(QStringLiteral("cw-recent-list"));
    recentList->setAccessibleName(QStringLiteral("Recent searches"));
    recentList->installEventFilter(this);
    recentModel = new QStringListModel(recentList);
    recentList->setModel(recentModel);
    recentPopup->setFocusProxy(recentList);
    popupLayout->addWidget(recentList);
    connect(recentList, &QListView::activated, this, [this](const QModelIndex &i) { recall(i.row()); });
    connect(recentList, &QListView::clicked, this, [this](const QModelIndex &i) { recall(i.row()); });
}

QLineEdit *SearchPanel::entry() const
{
    return options.mode == Mode::Number ? numberEntry : findEntry;
}

void SearchPanel::focusEntry()
{
    entry()->setFocus(Qt::ShortcutFocusReason);
    entry()->selectAll();
}

void SearchPanel::updateControls()
{
    QScopedValueRollback<bool> guard(settingControls, true);
    const bool number = options.mode == Mode::Number;
    const bool generic = editor->model().isGeneric();
    const QString numberLabel = modeName(Mode::Number, generic);
    numberEntry->setAccessibleName(numberLabel);
    modes->setToolTip(QStringLiteral("Plain text (Ctrl+P), regular expression (Ctrl+R), %1 (Ctrl+L)")
                      .arg(numberLabel.toLower()));
    modes->setText(number ? QStringLiteral("Number") : options.mode == Mode::Text ?
                   QStringLiteral("Text") : QStringLiteral("Regex"));
    const auto actions = modeMenu->actions();
    actions.at(static_cast<int>(Mode::Number))->setText(numberLabel);
    for (int i = 0; i < actions.size(); ++i)
        actions[i]->setChecked(i == static_cast<int>(options.mode));
    entries->setCurrentWidget(entry());
    queryLabel->setText(number ? QStringLiteral("Go to") : backwards ?
                        QStringLiteral("Find ↑") : QStringLiteral("Find ↓"));
    queryLabel->setBuddy(entry());
    findEntry->setAccessibleName(backwards ? QStringLiteral("Find backward") : QStringLiteral("Find forward"));
    next->setText(number ? QStringLiteral("→") : QStringLiteral("↓"));
    next->setAccessibleName(number ? (generic ? QStringLiteral("Go to line") : QStringLiteral("Go to crumb"))
                                   : QStringLiteral("Next match"));
    next->setToolTip(next->accessibleName());
    previous->setVisible(!number);
    matchCase->setVisible(!number);
    wholeWords->setVisible(!number);
    wrap->setVisible(!number || !generic);
    matchCase->setChecked(options.matchCase);
    wholeWords->setChecked(options.wholeWords);
    wrap->setChecked(options.wrap);
    replaceRow->setVisible(showingReplace && !number);
}

void SearchPanel::beginOrigin()
{
    editor->prepareSearch();
    origin = editor->textCursor();
    origin.clearSelection();
    hit = {};
    recorded = false;
    pendingSteps.clear();
}

void SearchPanel::saveOrigin(int destination)
{
    if (recorded || origin.position() == destination)
        return;
    recorded = true;
    if (recordOrigin)
        recordOrigin(origin);
}

void SearchPanel::open(std::optional<Mode> requestedMode, bool reverse, bool replace)
{
    const bool wasHidden = isHidden();
    const Span previousHit = hit;
    beginOrigin();
    Mode mode = requestedMode.value_or(lastTextMode);
    const QTextCursor selected = editor->textCursor();
    if ((wasHidden || requestedMode) && mode != Mode::Number && selected.hasSelection() &&
        selected.document()->findBlock(selected.selectionStart()) == selected.document()->findBlock(selected.selectionEnd()) &&
        !(previousHit == Span{selected.selectionStart(), selected.selectionEnd()})) {
        QScopedValueRollback<bool> guard(settingControls, true);
        mode = requestedMode.value_or(Mode::Text);
        findEntry->setText(ManuscriptText::selectedText(selected));
    }
    options.mode = mode;
    if (mode != Mode::Number)
        lastTextMode = mode;
    backwards = reverse;
    showingReplace = replace && mode != Mode::Number;
    show();
    updateControls();
    rebuild(false);
    focusEntry();
}

void SearchPanel::find(bool reverse)
{
    open(std::nullopt, reverse, false);
}

void SearchPanel::findInMode(Mode mode)
{
    open(mode, false, false);
}

void SearchPanel::findReplace()
{
    if (!isHidden() && showingReplace && options.mode != Mode::Number) {
        QLineEdit *destination = findEntry->hasFocus() ? replacement : findEntry;
        destination->setFocus(Qt::ShortcutFocusReason);
        destination->selectAll();
        return;
    }
    open(std::nullopt, false, true);
}

void SearchPanel::goToNumber()
{
    open(Mode::Number, false, false);
}

void SearchPanel::setMode(Mode mode)
{
    options.mode = mode;
    if (mode == Mode::Number)
        showingReplace = false;
    else
        lastTextMode = mode;
    updateControls();
    focusEntry();
    rebuild(true);
}

void SearchPanel::remember()
{
    history.remember({entry()->text(), options});
}

void SearchPanel::showRecent()
{
    recentEntries = history.entries();
    QStringList labels;
    for (const auto &entry : recentEntries) {
        QString details = modeName(entry.options.mode, editor->model().isGeneric());
        if (entry.options.mode != Mode::Number) {
            if (entry.options.matchCase)
                details += QStringLiteral(" · case");
            if (entry.options.wholeWords)
                details += QStringLiteral(" · whole words");
        }
        if (!entry.options.wrap)
            details += QStringLiteral(" · no wrap");
        labels.append(entry.query + QStringLiteral("   ·   ") + details);
    }
    if (labels.isEmpty())
        labels.append(QStringLiteral("No recent searches"));
    recentModel->setStringList(labels);
    const QRect available = recent->screen()->availableGeometry();
    const int w = std::min({420, std::max(100, window()->width()), available.width()});
    const int h = std::min({int(labels.size()) * (recentList->fontMetrics().height() + 6) + 12,
                            280, available.height()});
    recentPopup->resize(w, h);
    const QPoint anchor = recent->mapToGlobal(QPoint(recent->width(), recent->height()));
    const int x = std::clamp(anchor.x() - w, available.left(), available.right() - w + 1);
    int y = anchor.y();
    if (y + h > available.bottom())
        y = std::max(available.top(), anchor.y() - recent->height() - h);
    recentPopup->move(x, y);
    recentPopup->show();
    recentList->setCurrentIndex(recentModel->index(0));
    recentList->setFocus(Qt::PopupFocusReason);
}

void SearchPanel::recall(int row)
{
    if (!recentPopup->isVisible() || row < 0 || row >= recentEntries.size())
        return;
    auto recalled = recentEntries[row];
    recentPopup->hide();
    {
        QScopedValueRollback<bool> guard(settingControls, true);
        options = recalled.options;
        if (options.mode == Mode::Number)
            showingReplace = false;
        else
            lastTextMode = options.mode;
        entry()->setText(recalled.query);
    }
    updateControls();
    focusEntry();
    rebuild(true);
    if (options.mode == Mode::Text)
        remember();
    else
        step(backwards);
}

void SearchPanel::closeSearch(bool settleCaret)
{
    const bool selectedMatch = hit.found();
    if (!isHidden())
        remember();
    recentPopup->hide();
    cancelPreview();
    refreshDelay->stop();
    previewPending = false;
    pendingSteps.clear();
    hide();
    visibleHits.clear();
    editor->setSearchHighlights({});
    {
        QScopedValueRollback<bool> guard(movingCursor, true);
        if (settleCaret) editor->clearEditorSelection();
        editor->setFocus(Qt::ShortcutFocusReason);
        if (settleCaret && selectedMatch)
            editor->jumpTo(hit.start, true);
    }
    if (options.mode == Mode::Number) {
        options.mode = lastTextMode;
        invalidateIndex();
        updateControls();
    }
}

void SearchPanel::reset()
{
    QScopedValueRollback<bool> guard(settingControls, true);
    recentPopup->hide();
    hide();
    invalidateIndex();
    cancelPreview();
    refreshDelay->stop();
    highlightDelay->stop();
    findEntry->clear();
    numberEntry->clear();
    replacement->clear();
    paper.clear();
    paperDirty = true;
    options = {};
    hit = {};
    visibleHits.clear();
    editor->setSearchHighlights({});
    notice.clear();
    backwards = showingReplace = recorded = wrapped = false;
    lastTextMode = Mode::Text;
    origin = editor->textCursor();
    updateControls();
}

const QString &SearchPanel::snapshot()
{
    if (paperDirty) {
        paper = viewOffsets.reset(editor->document(), editor->model().isGeneric() && !editor->showManuscriptMarkup());
        paperDirty = false;
    }
    return paper;
}

void SearchPanel::invalidateIndex()
{
    if (indexCancel)
        indexCancel->store(true);
    indexCancel.reset();
    indexReady = false;
    previewPending = false;
    matches = {};
    pendingSteps.clear();
    scanDelay->stop();
}

void SearchPanel::rebuild(bool livePreview)
{
    refreshDelay->stop();
    const bool restore = hit.found() || editor->textCursor().hasSelection();
    hit = {};
    invalidateIndex();
    cancelPreview();
    notice.clear();
    wrapped = false;
    pattern = cwsearch::Pattern(findEntry->text(), options);
    const bool textSearch = options.mode != Mode::Number && !findEntry->text().isEmpty();
    if (textSearch && pattern.error().isEmpty()) {
        if (snapshot().size() < backgroundThreshold)
            collectMatches();
        else
            scanDelay->start(16);
    }
    if (livePreview && options.mode == Mode::Text)
        previewText(restore);
    updateHighlights();
    showStatus();
}

void SearchPanel::collectMatches()
{
    scanDelay->stop();
    if (indexReady || indexCancel || options.mode == Mode::Number ||
        findEntry->text().isEmpty() || !pattern.error().isEmpty())
        return;
    const auto cancel = std::make_shared<std::atomic_bool>(false);
    indexCancel = cancel;
    const QString text = snapshot();
    const auto work = [expression = pattern, text, cancel] { return expression.collect(text, *cancel); };
    if (text.size() < backgroundThreshold) {
        indexFinished(work());
        return;
    }
    runInBackground<cwsearch::Matches>(this, work, [this, cancel](cwsearch::Matches result) {
        if (indexCancel == cancel && !cancel->load())
            indexFinished(std::move(result));
    });
}

void SearchPanel::indexFinished(cwsearch::Matches result)
{
    indexCancel.reset();
    matches = std::move(result);
    indexReady = true;
    if (previewPending && !isHidden()) {
        previewPending = false;
        previewText(restorePending);
    }
    if (matches.spans.isEmpty() &&
        (notice == QStringLiteral("No earlier match") || notice == QStringLiteral("No later match")))
        notice.clear();
    updateHighlights();
    showStatus();
    const auto queued = std::move(pendingSteps);
    pendingSteps.clear();
    for (bool reverse : queued)
        step(reverse);
}

void SearchPanel::scheduleHighlights()
{
    if (!isHidden() && indexReady && !highlightDelay->isActive())
        highlightDelay->start(0);
}

void SearchPanel::updateHighlights()
{
    QVector<Span> visible;
    if (!isHidden() && indexReady && options.mode != Mode::Number && !matches.spans.isEmpty()) {
        const QSize viewport = editor->viewport()->size();
        const int first = viewOffsets.visiblePosition(editor->cursorForPosition(QPoint(0, 0)).position());
        const int last = viewOffsets.visiblePosition(editor->cursorForPosition(QPoint(viewport.width(), viewport.height() - 1)).position());
        auto it = std::lower_bound(matches.spans.begin(), matches.spans.end(), first,
                                   [](Span span, int position) { return span.end < position; });
        for (; it != matches.spans.end() && it->start <= last; ++it) {
            if (it->end > it->start)
                visible.append(*it);
        }
    }
    const QColor background = EditorColors::searchPaper(editor->palette());
    if (visible == visibleHits && background == highlightColor)
        return;
    visibleHits = visible;
    highlightColor = background;
    QList<QTextEdit::ExtraSelection> selections;
    QTextCharFormat format;
    format.setBackground(background);
    for (Span range : visible) {
        QTextEdit::ExtraSelection mark;
        mark.cursor = QTextCursor(editor->document());
        mark.cursor.setPosition(viewOffsets.sourcePosition(range.start));
        mark.cursor.setPosition(viewOffsets.sourcePosition(range.end), QTextCursor::KeepAnchor);
        mark.format = format;
        selections.append(mark);
    }
    editor->setSearchHighlights(selections);
}

void SearchPanel::showMatch(Span selected, bool didWrap)
{
    saveOrigin(viewOffsets.sourcePosition(selected.start));
    hit = selected;
    wrapped = didWrap;
    QScopedValueRollback<bool> guard(movingCursor, true);
    editor->selectMatch(viewOffsets.sourcePosition(selected.start), viewOffsets.sourcePosition(selected.end));
    scheduleHighlights();
    showStatus();
}

SearchPanel::Span SearchPanel::literalAt(int position, bool reverse, bool &didWrap, bool &waitForIndex)
{
    waitForIndex = false;
    if (indexReady) {
        const int index = cwsearch::nearestIndex(matches.spans, position, reverse, options.wrap, didWrap);
        return index >= 0 ? matches.spans[index] : Span{};
    }
    const QString &text = snapshot();
    const auto candidate = pattern.nearestLiteral(text, position, reverse, options.wrap, didWrap);
    // For "ana" in "bananana", the count and Replace All use occurrences at
    // positions 1 and 5. Position 3 overlaps the first and must never become a
    // navigation result. Only this ambiguous case needs the completed index.
    waitForIndex = pattern.hasOverlappingPredecessor(text, candidate);
    return candidate;
}

void SearchPanel::previewText(bool restorePrevious)
{
    bool didWrap = false;
    Span selected;
    bool wait = false;
    if (!findEntry->text().isEmpty())
        selected = literalAt(viewOffsets.visiblePosition(origin.position()), backwards, didWrap, wait);
    if (wait) {
        previewPending = true;
        restorePending = restorePrevious;
        collectMatches();
        return;
    }
    if (selected.found()) {
        showMatch(selected, didWrap);
        return;
    }
    hit = {};
    if (restorePrevious) {
        QScopedValueRollback<bool> guard(movingCursor, true);
        editor->jumpTo(origin.position(), false);
    }
    if (!findEntry->text().isEmpty() && !options.wrap &&
        (!indexReady || !matches.spans.isEmpty()))
        notice = backwards ? QStringLiteral("No earlier match") : QStringLiteral("No later match");
}

void SearchPanel::stepNumber(bool reverse)
{
    const QString input = numberEntry->text().trimmed();
    const bool generic = editor->model().isGeneric();
    bool ok = false;
    const quint64 number = input.toULongLong(&ok);
    if (!ok || !number || !std::all_of(input.begin(), input.end(), [](QChar c) { return c >= u'0' && c <= u'9'; })) {
        setNotice(generic ? QStringLiteral("Enter a positive line number.") : QStringLiteral("Enter a positive crumb number."));
        return;
    }
    remember();
    int at = -1;
    if (generic) {
        quint64 visibleLine = 0;
        for (auto block = editor->document()->begin(); block.isValid(); block = block.next()) {
            if (!block.isVisible()) continue;
            if (!block.next().isValid() && block.blockNumber() > 0 && block.length() == 1) break;
            if (++visibleLine == number) { at = block.position(); break; }
        }
    } else {
        at = editor->model().findNumber(number, origin.position(), reverse, options.wrap);
    }
    if (at < 0) {
        const QString scope = options.wrap ? QString() : reverse ?
                              QStringLiteral(" earlier") : QStringLiteral(" later");
        setNotice(generic ? QStringLiteral("No line numbered %1").arg(number)
                          : QStringLiteral("No%1 crumb numbered %2").arg(scope).arg(number));
        return;
    }
    saveOrigin(at);
    hit = {};
    closeSearch();
    QScopedValueRollback<bool> guard(movingCursor, true);
    editor->jumpTo(at, true);
    origin = editor->textCursor();
}

void SearchPanel::repeat(bool opposite)
{
    step(opposite ? !backwards : backwards);
}

void SearchPanel::step(bool reverse)
{
    editor->prepareSearch();
    if (options.mode == Mode::Number) {
        stepNumber(reverse);
        return;
    }
    if (findEntry->text().isEmpty()) {
        find(backwards);
        return;
    }
    if (refreshDelay->isActive() || paperDirty || (!indexReady && !scanDelay->isActive() && !indexCancel))
        rebuild(false);
    if (!pattern.error().isEmpty() || !matches.error.isEmpty()) {
        show();
        showStatus();
        return;
    }
    if (!isHidden())
        remember();
    bool didWrap = false;
    Span selected;
    const int start = hit.found() ? (reverse ? hit.start : hit.end) : viewOffsets.visiblePosition(origin.position());
    if (options.mode == Mode::Text) {
        bool wait = false;
        selected = literalAt(start, reverse, didWrap, wait);
        if (wait || previewPending) {
            pendingSteps.append(reverse);
            collectMatches();
            return;
        }
    } else if (!indexReady) {
        pendingSteps.append(reverse);
        collectMatches();
        return;
    } else {
        int index = -1;
        if (hit.found()) {
            auto at = std::lower_bound(matches.spans.constBegin(), matches.spans.constEnd(), hit.start,
                                       [](Span a, int pos) { return a.start < pos; });
            while (at != matches.spans.constEnd() && at->start == hit.start && !(*at == hit))
                ++at;
            if (at != matches.spans.constEnd() && *at == hit) {
                index = int(at - matches.spans.constBegin()) + (reverse ? -1 : 1);
                if (index < 0 || index >= matches.spans.size()) {
                    index = options.wrap ? (reverse ? int(matches.spans.size()) - 1 : 0) : -1;
                    didWrap = options.wrap;
                }
            }
        } else {
            index = cwsearch::nearestIndex(matches.spans, start, reverse, options.wrap, didWrap);
        }
        if (index >= 0)
            selected = matches.spans[index];
    }
    notice.clear();
    if (selected.found()) {
        showMatch(selected, didWrap);
    } else {
        show();
        const bool noMatches = indexReady && matches.spans.isEmpty();
        setNotice(noMatches || options.wrap ? QStringLiteral("No matches") :
                  reverse ? QStringLiteral("No earlier match") : QStringLiteral("No later match"));
    }
}

void SearchPanel::showStatus()
{
    const bool text = options.mode != Mode::Number && !findEntry->text().isEmpty();
    const auto error = pattern.error().isEmpty() ? matches.error : pattern.error();
    QString message = notice;
    if (text && !error.isEmpty())
        message = error;
    if (message.isEmpty() && text) {
        if (!indexReady)
            message = QStringLiteral("Searching…");
        else if (matches.spans.isEmpty())
            message = QStringLiteral("No matches");
        else {
            auto at = std::lower_bound(matches.spans.constBegin(), matches.spans.constEnd(), hit.start,
                                       [](Span span, int position) { return span.start < position; });
            if (hit.found() && at != matches.spans.constEnd() && *at == hit)
                message = QStringLiteral("%1 of %2").arg(at - matches.spans.constBegin() + 1).arg(matches.spans.size());
            else
                message = plural(matches.spans.size(), "match", "matches");
            if (wrapped)
                message += QStringLiteral(" · wrapped");
        }
    }
    status->setText(message);
    status->setToolTip(message);
    const bool replace = showingReplace && text && error.isEmpty() && indexReady &&
                         !matches.spans.isEmpty() && !editor->isReadOnly();
    replaceButton->setEnabled(replace && !replacementCancel);
    allButton->setEnabled(replace && !replacementCancel);
}

void SearchPanel::setNotice(const QString &text)
{
    notice = text;
    showStatus();
}

void SearchPanel::cancelPreview()
{
    if (replacementCancel)
        replacementCancel->store(true);
    replacementCancel.reset();
    pendingReplacement.reset();
    allButton->setText(QStringLiteral("All…"));
    allButton->setAccessibleName(QStringLiteral("Preview all replacements"));
    preview->hide();
    cancelButton->hide();
}

void SearchPanel::replaceOne()
{
    if (!showingReplace || options.mode == Mode::Number || editor->isReadOnly())
        return;
    cancelPreview();
    if (!indexReady || !hit.found()) {
        step(false);
        return;
    }
    const Span selected = hit;
    const auto cursor = editor->textCursor();
    if (cursor.selectionStart() != viewOffsets.sourcePosition(selected.start) || cursor.selectionEnd() != viewOffsets.sourcePosition(selected.end)) {
        step(false);
        return;
    }
    QString error;
    const QString expanded = pattern.expandOne(snapshot(), selected, replacement->text(), error);
    if (!error.isEmpty()) {
        setNotice(error);
        return;
    }
    if (QStringView(snapshot()).mid(selected.start, selected.end - selected.start) == expanded) {
        step(false);
        return;
    }
    int caret;
    {
        QScopedValueRollback<bool> guard(replacing, true);
        caret = editor->replaceRangeLiteral(viewOffsets.sourcePosition(selected.start), viewOffsets.sourcePosition(selected.end), expanded);
        editor->jumpTo(caret, false);
    }
    origin = editor->textCursor();
    origin.clearSelection();
    rebuild(false);
    step(false);
}

void SearchPanel::replaceAll()
{
    if (!showingReplace || options.mode == Mode::Number || editor->isReadOnly() || !indexReady ||
        matches.spans.isEmpty() || replacementCancel)
        return;
    if (pendingReplacement) {
        applyPreview();
        return;
    }
    remember();
    const QString text = snapshot();
    const auto cancel = std::make_shared<std::atomic_bool>(false);
    replacementCancel = cancel;
    const auto work = [expression = pattern, value = replacement->text(), text, cancel] {
        return expression.plan(text, value, [](QString s) {
            return ManuscriptEditor::normalizeInsertion(std::move(s), false);
        }, *cancel);
    };
    if (text.size() < backgroundThreshold) {
        previewReady(work());
    } else {
        cancelButton->show();
        setNotice(QStringLiteral("Preparing replacement preview…"));
        runInBackground<cwsearch::ReplacementPlan>(this, work, [this, cancel](cwsearch::ReplacementPlan plan) {
            if (replacementCancel == cancel && !cancel->load())
                previewReady(std::move(plan));
        });
    }
}

void SearchPanel::previewReady(cwsearch::ReplacementPlan plan)
{
    replacementCancel.reset();
    if (!plan.error.isEmpty() || plan.edits.isEmpty()) {
        cancelPreview();
        setNotice(plan.error.isEmpty() ? QStringLiteral("The replacement makes no changes.") : plan.error);
        return;
    }
    QString message = QStringLiteral("%1 in %2")
        .arg(plural(plan.changes, "replacement", "replacements"), affectedUnits(plan.affectedLines));
    QStringList lines{message};
    for (const auto &example : plan.examples) {
        lines.append(QStringLiteral("Line %1 before   %2").arg(example.line).arg(example.before));
        lines.append(QStringLiteral("Line %1 after      %2").arg(example.line).arg(example.after));
    }
    preview->setPlainText(lines.join(u'\n'));
    QTextCharFormat bold;
    bold.setFontWeight(QFont::Bold);
    auto block = preview->document()->begin().next();
    QTextCursor highlight(preview->document());
    highlight.beginEditBlock();
    for (const auto &example : plan.examples) {
        const int start = block.position() + block.length() - 1 - int(example.before.size());
        for (const auto match : example.beforeMatches) {
            highlight.setPosition(start + match.start);
            highlight.setPosition(start + match.end, QTextCursor::KeepAnchor);
            highlight.mergeCharFormat(bold);
        }
        block = block.next().next();
    }
    highlight.endEditBlock();
    preview->show();
    cancelButton->show();
    allButton->setText(QStringLiteral("Apply all"));
    allButton->setAccessibleName(QStringLiteral("Apply all previewed replacements"));
    pendingReplacement = std::move(plan);
    setNotice(message);
}

void SearchPanel::applyPreview()
{
    // contentsChange immediately invalidates a preview on any intervening edit.
    // Each patch already reconciles whitespace at both edges of its source lines.
    auto plan = std::move(*pendingReplacement);
    cancelPreview();
    QString removed;
    if (editor->copiesDeletedText()) {
        const QString source = snapshot();
        // A compact patch may include unchanged words between two matches.
        // Only the actual matched passages belong on the clipboard.
        for (const auto span : plan.removedMatches) {
            if (!removed.isEmpty()) removed += u'\n';
            removed += QStringView(source).mid(span.start, span.end - span.start);
        }
    }
    {
        QScopedValueRollback<bool> guard(replacing, true);
        QTextCursor transaction(editor->document());
        transaction.beginEditBlock();
        for (auto it = plan.edits.crbegin(); it != plan.edits.crend(); ++it) {
            QTextCursor edit(editor->document());
            edit.setPosition(viewOffsets.sourcePosition(it->original.start));
            edit.setPosition(viewOffsets.sourcePosition(it->original.end), QTextCursor::KeepAnchor);
            ManuscriptText::replaceVisibleRange(edit, it->replacement);
        }
        transaction.endEditBlock();
    }
    editor->copyRemovedText(removed);
    origin = editor->textCursor();
    origin.clearSelection();
    rebuild(false);
    setNotice(QStringLiteral("Applied %1 in %2")
              .arg(plural(plan.changes, "replacement", "replacements"), affectedUnits(plan.affectedLines)));
}

QString SearchPanel::affectedUnits(int lines) const
{
    return editor->model().isGeneric() ? plural(lines, "paragraph", "paragraphs") : plural(lines, "crumb", "crumbs");
}

bool SearchPanel::eventFilter(QObject *object, QEvent *event)
{
    if (object == editor->viewport() && (event->type() == QEvent::Resize || event->type() == QEvent::Show ||
                                         event->type() == QEvent::PaletteChange))
        scheduleHighlights();
    if (event->type() == QEvent::KeyPress) {
        auto *key = static_cast<QKeyEvent *>(event);
        const bool enter = key->key() == Qt::Key_Return || key->key() == Qt::Key_Enter;
        const auto mods = key->modifiers() & ~Qt::KeypadModifier;
        if (object == recentList) {
            if (key->key() == Qt::Key_Escape) {
                recentPopup->hide();
                entry()->setFocus();
                return true;
            }
            if (enter && mods == Qt::NoModifier) {
                recall(recentList->currentIndex().row());
                return true;
            }
        }
        if (enter && mods == Qt::ShiftModifier && (object == findEntry || object == numberEntry)) {
            repeat(true);
            return true;
        }
    }
    return QWidget::eventFilter(object, event);
}

bool SearchPanel::handleKey(QKeyEvent *event)
{
    const auto key = event->key();
    const auto mods = event->modifiers() & ~Qt::KeypadModifier;
    if (recentPopup->isVisible() || modeMenu->isVisible())
        return false; // Native popup navigation owns Escape, arrows and Return.
    const bool inSearch = isAncestorOf(QApplication::focusWidget());
    if (mods == Qt::ControlModifier) {
        switch (key) {
        case Qt::Key_F: find(false); return true;
        case Qt::Key_G: find(true); return true;
        case Qt::Key_H: findReplace(); return true;
        case Qt::Key_L:
            if (!isHidden()) setMode(Mode::Number);
            else goToNumber();
            return true;
        case Qt::Key_P:
        case Qt::Key_R: {
            const Mode mode = key == Qt::Key_P ? Mode::Text : Mode::Regex;
            if (editor->hasFocus()) findInMode(mode);
            else if (inSearch) setMode(mode);
            else return false;
            return true;
        }
        default: break;
        }
    }
    if (key == Qt::Key_F3 && (mods == Qt::NoModifier || mods == Qt::ShiftModifier)) {
        repeat(mods == Qt::ShiftModifier);
        return true;
    }
    if (key == Qt::Key_Escape && mods == Qt::NoModifier) {
        if (!isHidden() || (hit.found() && editor->textCursor().hasSelection())) {
            closeSearch();
            return true;
        }
        return false;
    }
    if (isHidden())
        return false;
    if (mods == Qt::ControlModifier) {
        switch (key) {
        case Qt::Key_M: if (options.mode != Mode::Number) matchCase->toggle(); return true;
        case Qt::Key_W:
            if (!inSearch) return false;
            if (options.mode != Mode::Number) wholeWords->toggle();
            return true;
        default: break;
        }
    }
    bool wrapShortcut = mods == Qt::AltModifier;
#ifdef Q_OS_MACOS
    wrapShortcut |= mods == (Qt::ControlModifier | Qt::ShiftModifier);
#endif
    if (inSearch && wrapShortcut && key == Qt::Key_W) {
        wrap->toggle();
        return true;
    }
    if (mods == Qt::AltModifier && key == Qt::Key_Down) {
        showRecent();
        return true;
    }
    return false;
}
