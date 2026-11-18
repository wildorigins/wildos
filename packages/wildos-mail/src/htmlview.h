#pragma once

#include <QColor>
#include <QFont>
#include <QHash>
#include <QImage>
#include <QPointer>
#include <QQuickPaintedItem>
#include <QStack>
#include <QtQml/qqmlregistration.h>

#include <litehtml/litehtml.h>

#include <memory>

#include "messageview.h"
class QNetworkAccessManager;

// Draws an email's HTML with litehtml: HTML and CSS only, no scripts, no
// plug-ins, nothing fetched unless the person says "Show pictures". It is a
// viewport onto the rendered message: QML scrolls it by setting contentY,
// so a long newsletter never needs one huge texture.
class HtmlView : public QQuickPaintedItem, private litehtml::document_container
{
    Q_OBJECT
    QML_ELEMENT

    Q_PROPERTY(QString html READ html WRITE setHtml NOTIFY htmlChanged)
    Q_PROPERTY(MessageView *message READ message WRITE setMessage NOTIFY messageChanged)
    Q_PROPERTY(bool allowRemote READ allowRemote WRITE setAllowRemote NOTIFY allowRemoteChanged)
    Q_PROPERTY(bool paper READ paper WRITE setPaper NOTIFY paperChanged)
    Q_PROPERTY(QColor textColor READ textColor WRITE setTextColor NOTIFY coloursChanged)
    Q_PROPERTY(QColor linkColor READ linkColor WRITE setLinkColor NOTIFY coloursChanged)
    Q_PROPERTY(QColor quoteColor READ quoteColor WRITE setQuoteColor NOTIFY coloursChanged)
    Q_PROPERTY(qreal fontSize READ fontSize WRITE setFontSize NOTIFY coloursChanged)
    Q_PROPERTY(qreal contentY READ contentY WRITE setContentY NOTIFY contentYChanged)
    Q_PROPERTY(int contentHeight READ contentHeight NOTIFY contentHeightChanged)
    Q_PROPERTY(int contentWidth READ contentWidth NOTIFY contentHeightChanged)
    Q_PROPERTY(QString hoveredLink READ hoveredLink NOTIFY hoveredLinkChanged)

public:
    explicit HtmlView(QQuickItem *parent = nullptr);
    ~HtmlView() override;

    QString html() const
    {
        return m_html;
    }
    void setHtml(const QString &html);
    MessageView *message() const
    {
        return m_message;
    }
    void setMessage(MessageView *message);
    bool allowRemote() const
    {
        return m_allowRemote;
    }
    void setAllowRemote(bool allow);
    bool paper() const
    {
        return m_paper;
    }
    void setPaper(bool paper);
    QColor textColor() const
    {
        return m_textColor;
    }
    void setTextColor(const QColor &c);
    QColor linkColor() const
    {
        return m_linkColor;
    }
    void setLinkColor(const QColor &c);
    QColor quoteColor() const
    {
        return m_quoteColor;
    }
    void setQuoteColor(const QColor &c);
    qreal fontSize() const
    {
        return m_fontSize;
    }
    void setFontSize(qreal size);
    qreal contentY() const
    {
        return m_contentY;
    }
    void setContentY(qreal y);
    int contentHeight() const
    {
        return m_contentHeight;
    }
    int contentWidth() const
    {
        return m_contentWidth;
    }
    QString hoveredLink() const
    {
        return m_hoveredLink;
    }

    void paint(QPainter *painter) override;

    // Asks which printer, then prints the message on white paper in dark
    // ink whatever the window's colours: who it is from and to, when it was
    // sent and its subject, then the message. False only when the printer
    // couldn't be started; closing the dialog is not a failure.
    Q_INVOKABLE bool print();

Q_SIGNALS:
    void htmlChanged();
    void messageChanged();
    void allowRemoteChanged();
    void paperChanged();
    void coloursChanged();
    void contentYChanged();
    void contentHeightChanged();
    void hoveredLinkChanged();
    void linkActivated(const QString &url);
    // A right click, with the link under the pointer if there is one.
    void contextRequested(const QString &link, qreal x, qreal y);

protected:
    void geometryChange(const QRectF &newGeometry, const QRectF &oldGeometry) override;
    void hoverMoveEvent(QHoverEvent *event) override;
    void hoverLeaveEvent(QHoverEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;

private:
    // litehtml::document_container
    litehtml::uint_ptr create_font(const litehtml::tchar_t *faceName, int size, int weight, litehtml::font_style italic, unsigned int decoration,
                                   litehtml::font_metrics *fm) override;
    void delete_font(litehtml::uint_ptr hFont) override;
    int text_width(const litehtml::tchar_t *text, litehtml::uint_ptr hFont) override;
    void draw_text(litehtml::uint_ptr hdc, const litehtml::tchar_t *text, litehtml::uint_ptr hFont, litehtml::web_color color, const litehtml::position &pos) override;
    int pt_to_px(int pt) const override;
    int get_default_font_size() const override;
    const litehtml::tchar_t *get_default_font_name() const override;
    void draw_list_marker(litehtml::uint_ptr hdc, const litehtml::list_marker &marker) override;
    void load_image(const litehtml::tchar_t *src, const litehtml::tchar_t *baseurl, bool redraw_on_ready) override;
    void get_image_size(const litehtml::tchar_t *src, const litehtml::tchar_t *baseurl, litehtml::size &sz) override;
    void draw_background(litehtml::uint_ptr hdc, const litehtml::background_paint &bg) override;
    void draw_borders(litehtml::uint_ptr hdc, const litehtml::borders &borders, const litehtml::position &draw_pos, bool root) override;
    void set_caption(const litehtml::tchar_t *caption) override;
    void set_base_url(const litehtml::tchar_t *base_url) override;
    void link(const std::shared_ptr<litehtml::document> &doc, const litehtml::element::ptr &el) override;
    void on_anchor_click(const litehtml::tchar_t *url, const litehtml::element::ptr &el) override;
    void set_cursor(const litehtml::tchar_t *cursor) override;
    void transform_text(litehtml::tstring &text, litehtml::text_transform tt) override;
    void import_css(litehtml::tstring &text, const litehtml::tstring &url, litehtml::tstring &baseurl) override;
    void set_clip(const litehtml::position &pos, const litehtml::border_radiuses &bdr_radius, bool valid_x, bool valid_y) override;
    void del_clip() override;
    void get_client_rect(litehtml::position &client) const override;
    std::shared_ptr<litehtml::element> create_element(const litehtml::tchar_t *tag_name, const litehtml::string_map &attributes,
                                                      const std::shared_ptr<litehtml::document> &doc) override;
    void get_media_features(litehtml::media_features &media) const override;
    void get_language(litehtml::tstring &language, litehtml::tstring &culture) const override;

    void rebuild();
    void relayout();
    void drawSlice(QPainter *painter, int top, int bottom);
    int drawHeading(QPainter *painter, int width, const QString &subject, const QStringList &lines) const;
    QString resolve(const QString &src) const;
    void fetch(const QString &url);
    QFont fontFor(litehtml::uint_ptr handle) const;

    QString m_html;
    QPointer<MessageView> m_message;
    bool m_allowRemote = false;
    bool m_paper = false;
    QColor m_textColor = QColor(238, 241, 248);
    QColor m_linkColor = QColor(143, 180, 255);
    QColor m_quoteColor = QColor(238, 241, 248, 158);
    qreal m_fontSize = 14.5;
    qreal m_contentY = 0;
    int m_contentHeight = 0;
    int m_contentWidth = 0;
    int m_renderedWidth = -1;
    QString m_hoveredLink;
    QString m_pressedLink;

    std::unique_ptr<litehtml::context> m_context;
    litehtml::document::ptr m_document;
    QHash<litehtml::uint_ptr, QFont> m_fonts;
    litehtml::uint_ptr m_nextFont = 1;
    QHash<QString, QImage> m_images;
    QSet<QString> m_fetching;
    QNetworkAccessManager *m_network = nullptr;
    QString m_baseUrl;
    QPainter *m_painter = nullptr;
    QStack<QRect> m_clips;
    // While printing: the part of the message that belongs on this sheet,
    // which nothing may draw outside, and where each line of text and each
    // picture sits, so a page can end between them.
    QRect m_slice;
    QList<QPair<int, int>> *m_boxes = nullptr;
    // The colour a newsletter gives its <body>, which browsers paint across
    // the whole page and litehtml only behind the body's own box.
    QColor m_pageColour;
};
