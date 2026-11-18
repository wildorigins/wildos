#pragma once

#include <QColor>
#include <QList>
#include <QMetaMethod>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <qtermwidget.h>

class QScrollBar;
class TermLinkLines;

// The links in one terminal. The emulator finds an address in the text of
// a row, and of the rows a long line wraps onto by itself. Two kinds it
// misses are found here:
//
//   An address a program broke across rows itself, as a full-screen
//   program does with a sign-in link: the emulator sees only its first row
//   and opens it cut short. The rows that carry on from the left edge are
//   joined to it, so a click anywhere on it opens the whole address, the
//   whole of it is underlined under the pointer, and it copies whole.
//
//   A link a program marked (OSC 8), whose text need not be an address.
//   The emulator Wild OS ships draws and opens those itself and says where
//   one goes through hyperlinkAt(); with an emulator that cannot, they
//   stay plain text.
//
// The emulator has no way to ask for the text of a row, so the rows are
// read from its copy of everything it has kept (saveHistory) and matched
// to the screen by counting rows. An address is only joined when that
// count comes out exactly, or when every place the address appears reads
// the same.
class TermLinks : public QObject
{
    Q_OBJECT

public:
    TermLinks(QTermWidget *terminal, QWidget *display);

    // The link at a point of the terminal widget, as a click there opens
    // it; not valid when there is none.
    QUrl linkAt(const QPoint &position);
    // The colour of the line under a joined address: the scheme's text
    // colour.
    void setColour(const QColor &colour);

Q_SIGNALS:
    // A link was clicked.
    void activated(const QUrl &url);

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    struct Span {
        int row = 0;
        int from = 0;
        int to = 0;
    };
    struct Link {
        QString text;
        QList<Span> spans;
        // More rows than the emulator found.
        bool joined = false;
        // Marked by the program (OSC 8) rather than read from the text.
        bool marked = false;
    };
    // Where the emulator draws its characters.
    struct Grid {
        int left = 0;
        int top = 0;
        int width = 0;
        int height = 0;
        int columns = 0;
        int rows = 0;
        bool valid = false;
    };
    // The terminal's text, one entry for each line as the program wrote
    // it (a line the terminal wrapped is one entry), with the row each
    // ends on counted from the oldest row kept.
    struct Rows {
        bool valid = false;
        bool exact = false;
        int columns = 0;
        QStringList lines;
        QList<int> last;
    };

    Grid grid() const;
    Link find(const QPoint &point);
    Link whole(const Konsole::UrlFilter::HotSpot *head, const Grid &grid);
    bool headed(int line, const QString &head, int tail) const;
    Link extend(int line, const Konsole::UrlFilter::HotSpot *head, int tail, const Grid &grid) const;
    void loadRows();
    QUrl url(const Link &link) const;
    static QUrl openable(const QUrl &url, bool marked);
    void hover();
    void later();

    QTermWidget *m_terminal;
    QPointer<QWidget> m_display;
    QPointer<QScrollBar> m_scroll;
    TermLinkLines *m_lines;
    QMetaMethod m_marked;
    QTimer m_refresh;
    Rows m_rows;
    QColor m_colour = Qt::white;
    QPoint m_pointer;
    bool m_inside = false;
    // The press being handled opened a joined address here, so the cut
    // short one the emulator reports for the same press is dropped.
    bool m_tookPress = false;
    bool m_pressMarked = false;
};
