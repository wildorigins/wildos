#include "tab.h"
#include "terminalview.h"

#include <QPainter>
#include <QSplitter>
#include <QVBoxLayout>

namespace
{
// A hairline between panes with a wider invisible grip around it.
class PaneHandle : public QSplitterHandle
{
public:
    PaneHandle(Qt::Orientation o, QSplitter *parent)
        : QSplitterHandle(o, parent)
    {
        setAttribute(Qt::WA_NoSystemBackground);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setPen(Qt::NoPen);
        p.setBrush(QColor(255, 255, 255, underMouse() ? 60 : 30));
        if (orientation() == Qt::Horizontal) {
            p.drawRect(QRect(width() / 2, 0, 1, height()));
        } else {
            p.drawRect(QRect(0, height() / 2, width(), 1));
        }
    }
    bool event(QEvent *e) override
    {
        if (e->type() == QEvent::Enter || e->type() == QEvent::Leave) {
            update();
        }
        return QSplitterHandle::event(e);
    }
};

class PaneSplitter : public QSplitter
{
public:
    PaneSplitter(Qt::Orientation o, QWidget *parent = nullptr)
        : QSplitter(o, parent)
    {
        setHandleWidth(7);
        setChildrenCollapsible(false);
        setAttribute(Qt::WA_TranslucentBackground);
    }

protected:
    QSplitterHandle *createHandle() override
    {
        return new PaneHandle(orientation(), this);
    }
};
}

Tab::Tab(bool settings, QWidget *page, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_page(page)
{
    auto *layout = new QVBoxLayout(m_page);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
}

QString Tab::title() const
{
    if (m_settings) {
        return tr("Settings");
    }
    return m_current ? m_current->displayTitle() : QString();
}

QString Tab::iconName() const
{
    if (m_settings) {
        return QStringLiteral("settings");
    }
    return m_current ? m_current->iconName() : QStringLiteral("terminal");
}

void Tab::track(TerminalView *view)
{
    m_views.append(view);
    connect(view, &TerminalView::focused, this, [this, view] {
        setCurrent(view);
    });
    connect(view, &TerminalView::titleUpdated, this, [this, view] {
        if (view == m_current) {
            Q_EMIT changed();
        }
    });
    Q_EMIT viewAdded(view);
}

void Tab::addFirst(TerminalView *view)
{
    m_page->layout()->addWidget(view);
    track(view);
    setCurrent(view);
}

void Tab::split(TerminalView *view, Qt::Orientation orientation)
{
    TerminalView *from = m_current;
    if (!from) {
        addFirst(view);
        return;
    }
    QWidget *parent = from->parentWidget();
    auto *parentSplitter = qobject_cast<QSplitter *>(parent);
    if (parentSplitter && parentSplitter->orientation() == orientation) {
        // Same direction: the new pane joins the row, and every pane in it
        // gets an equal share.
        const int index = parentSplitter->indexOf(from);
        parentSplitter->insertWidget(index + 1, view);
        QList<int> sizes;
        const int total = orientation == Qt::Horizontal ? parentSplitter->width() : parentSplitter->height();
        for (int i = 0; i < parentSplitter->count(); ++i) {
            sizes.append(total / parentSplitter->count());
        }
        parentSplitter->setSizes(sizes);
    } else {
        auto *splitter = new PaneSplitter(orientation);
        const QList<int> oldSizes = parentSplitter ? parentSplitter->sizes() : QList<int>();
        if (parentSplitter) {
            const int index = parentSplitter->indexOf(from);
            parentSplitter->replaceWidget(index, splitter);
            parentSplitter->setSizes(oldSizes);
        } else {
            delete m_page->layout()->replaceWidget(from, splitter);
        }
        splitter->addWidget(from);
        splitter->addWidget(view);
        from->show();
        splitter->show();
        const int total = orientation == Qt::Horizontal ? from->width() : from->height();
        splitter->setSizes({total / 2, total / 2});
    }
    track(view);
    view->setFocus();
    setCurrent(view);
}

bool Tab::remove(TerminalView *view)
{
    m_views.removeAll(view);
    QWidget *parent = view->parentWidget();
    view->hide();
    view->setParent(nullptr);
    view->deleteLater();

    // A splitter left with one pane gives its place back to that pane.
    auto *splitter = qobject_cast<QSplitter *>(parent);
    if (splitter && splitter->count() == 1) {
        QWidget *only = splitter->widget(0);
        auto *outer = qobject_cast<QSplitter *>(splitter->parentWidget());
        if (outer) {
            const QList<int> sizes = outer->sizes();
            outer->replaceWidget(outer->indexOf(splitter), only);
            outer->setSizes(sizes);
        } else {
            delete m_page->layout()->replaceWidget(splitter, only);
        }
        only->show();
        splitter->hide();
        splitter->deleteLater();
    }

    if (m_views.isEmpty()) {
        return true;
    }
    if (m_current == view || !m_current) {
        m_current = m_views.last();
    }
    m_current->setFocus();
    updateOutlines();
    Q_EMIT changed();
    return false;
}

void Tab::setCurrent(TerminalView *view)
{
    if (m_current != view) {
        m_current = view;
        Q_EMIT changed();
    }
    updateOutlines();
}

void Tab::updateOutlines()
{
    const bool split = m_views.size() > 1;
    for (TerminalView *v : std::as_const(m_views)) {
        v->setOutlined(split && v == m_current);
    }
}

TerminalView *Tab::neighbour(Qt::Edge edge) const
{
    if (!m_current) {
        return nullptr;
    }
    const QRect from(m_current->mapTo(m_page, QPoint(0, 0)), m_current->size());
    TerminalView *best = nullptr;
    int bestDistance = INT_MAX;
    for (TerminalView *v : m_views) {
        if (v == m_current) {
            continue;
        }
        const QRect r(v->mapTo(m_page, QPoint(0, 0)), v->size());
        bool beyond = false;
        bool overlaps = false;
        int distance = 0;
        switch (edge) {
        case Qt::LeftEdge:
            beyond = r.right() < from.left();
            overlaps = r.bottom() >= from.top() && r.top() <= from.bottom();
            distance = from.left() - r.right();
            break;
        case Qt::RightEdge:
            beyond = r.left() > from.right();
            overlaps = r.bottom() >= from.top() && r.top() <= from.bottom();
            distance = r.left() - from.right();
            break;
        case Qt::TopEdge:
            beyond = r.bottom() < from.top();
            overlaps = r.right() >= from.left() && r.left() <= from.right();
            distance = from.top() - r.bottom();
            break;
        case Qt::BottomEdge:
            beyond = r.top() > from.bottom();
            overlaps = r.right() >= from.left() && r.left() <= from.right();
            distance = r.top() - from.bottom();
            break;
        }
        if (beyond && overlaps && distance < bestDistance) {
            best = v;
            bestDistance = distance;
        }
    }
    return best;
}
