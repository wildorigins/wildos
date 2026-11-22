#include "spellcheck.h"

#include <QLocale>
#include <QSettings>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>

#include <Sonnet/Settings>

namespace
{
struct Word {
    int start = 0;
    int length = 0;
};

// Scripts written without spaces between words: a dictionary of words has
// nothing to say about a whole sentence of them.
bool unspaced(QChar c)
{
    switch (c.script()) {
    case QChar::Script_Han:
    case QChar::Script_Hiragana:
    case QChar::Script_Katakana:
    case QChar::Script_Hangul:
    case QChar::Script_Thai:
    case QChar::Script_Lao:
    case QChar::Script_Khmer:
    case QChar::Script_Myanmar:
        return true;
    default:
        return false;
    }
}

bool apostrophe(QChar c)
{
    return c == QLatin1Char('\'') || c == QChar(0x2019);
}

// The words of a paragraph worth checking. A run of text with a digit in
// it, or the marks of an address, a link or a file name, is left alone.
QList<Word> wordsIn(const QString &text)
{
    QList<Word> words;
    const int size = text.size();
    int at = 0;
    while (at < size) {
        if (text.at(at).isSpace()) {
            ++at;
            continue;
        }
        int end = at;
        bool plain = true;
        while (end < size && !text.at(end).isSpace()) {
            const QChar c = text.at(end);
            if (c.isDigit() || c == QLatin1Char('@') || c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char('_') || unspaced(c)) {
                plain = false;
            }
            // "example.com", "notes.txt": a full stop with a letter straight
            // after it.
            if (c == QLatin1Char('.') && end + 1 < size && text.at(end + 1).isLetter()) {
                plain = false;
            }
            ++end;
        }
        while (plain && at < end) {
            if (!text.at(at).isLetter()) {
                ++at;
                continue;
            }
            int stop = at;
            while (stop < end
                   && (text.at(stop).isLetter() || text.at(stop).isMark() || (apostrophe(text.at(stop)) && stop + 1 < end && text.at(stop + 1).isLetter()))) {
                ++stop;
            }
            words.append(Word{at, stop - at});
            at = stop;
        }
        at = end;
    }
    return words;
}

// Dictionaries spell "don't" with a straight apostrophe; people type both.
QString wordFrom(const QString &text, const Word &word)
{
    QString w = text.mid(word.start, word.length);
    w.replace(QChar(0x2019), QLatin1Char('\''));
    return w;
}
}

SpellCheck::SpellCheck(QObject *parent)
    : QSyntaxHighlighter(parent)
{
    m_format.setUnderlineStyle(QTextCharFormat::SingleUnderline);
    reload();
}

void SpellCheck::setQuickDocument(QQuickTextDocument *document)
{
    if (document == m_quick) {
        return;
    }
    m_quick = document;
    setDocument(document ? document->textDocument() : nullptr);
    Q_EMIT documentChanged();
}

void SpellCheck::setColor(const QColor &color)
{
    if (color == m_color) {
        return;
    }
    m_color = color;
    m_format.setUnderlineColor(color);
    Q_EMIT colorChanged();
    recheckAll();
}

void SpellCheck::setCursorPosition(int position)
{
    if (position == m_cursor) {
        return;
    }
    m_cursor = position;
    Q_EMIT cursorPositionChanged();
    // After the edit that moved it has finished, not in the middle of it.
    if (m_active && !m_recheckQueued) {
        m_recheckQueued = true;
        QMetaObject::invokeMethod(this, &SpellCheck::recheckAroundCursor, Qt::QueuedConnection);
    }
}

// The word the cursor has left can be judged now, and the one it has reached
// is given the time to be finished.
void SpellCheck::recheckAroundCursor()
{
    m_recheckQueued = false;
    if (!m_active || !document()) {
        return;
    }
    const QTextBlock before = document()->findBlock(m_checkedAt);
    const QTextBlock now = document()->findBlock(m_cursor);
    m_checkedAt = m_cursor;
    m_cursorSettled = true;
    if (before.isValid() && before != now) {
        rehighlightBlock(before);
    }
    if (now.isValid()) {
        rehighlightBlock(now);
    }
    m_cursorSettled = false;
}

void SpellCheck::recheckAll()
{
    m_cursorSettled = true;
    rehighlight();
    m_cursorSettled = false;
}

void SpellCheck::reload()
{
    const QSettings stored(QStringLiteral("KDE"), QStringLiteral("Sonnet"));
    const QString language = stored.value(QStringLiteral("defaultLanguage"), QLocale::system().name()).toString();
    m_skipUppercase = !stored.value(QStringLiteral("checkUppercase"), true).toBool();
    const QStringList own = stored.value(QStringLiteral("ignore_%1").arg(language), Sonnet::Settings::defaultIgnoreList()).toStringList();
    m_own = QSet<QString>(own.cbegin(), own.cend());
    if (m_speller.language() != language) {
        m_speller.setLanguage(language);
    }
    // "Check spelling" and "Check as I type" in Settings > Typing, and a
    // dictionary for the language.
    // Mail's own "Check spelling as you type" (Settings, Writing) wins once
    // it has been set.
    const QVariant mine = QSettings().value(QStringLiteral("writing/spelling"));
    const bool on = mine.isValid() ? mine.toBool()
                                  : stored.value(QStringLiteral("checkerEnabledByDefault"), false).toBool() && stored.value(QStringLiteral("backgroundCheckerEnabled"), true).toBool();
    m_active = on && m_speller.isValid();
    recheckAll();
}

bool SpellCheck::wrong(const QString &word) const
{
    if (word.size() < 2 || m_own.contains(word) || m_session.contains(word)) {
        return false;
    }
    if (m_skipUppercase && word.isUpper()) {
        return false;
    }
    return m_speller.isMisspelled(word);
}

void SpellCheck::highlightBlock(const QString &text)
{
    if (!m_active) {
        return;
    }
    const int blockStart = currentBlock().position();
    const QList<Word> words = wordsIn(text);
    // A paragraph is looked at as each letter goes in, a moment before the
    // cursor is known to have moved on by one, so then the word a step from
    // the cursor counts as the one being typed too.
    const int reach = m_cursorSettled ? 0 : 1;
    for (const Word &word : words) {
        if (qAbs(blockStart + word.start + word.length - m_cursor) <= reach) {
            continue;
        }
        if (wrong(wordFrom(text, word))) {
            setFormat(word.start, word.length, m_format);
        }
    }
}

QVariantMap SpellCheck::wordAt(int position) const
{
    if (!m_active || !document()) {
        return {};
    }
    const QTextBlock block = document()->findBlock(position);
    if (!block.isValid()) {
        return {};
    }
    const QString text = block.text();
    const int at = position - block.position();
    const QList<Word> words = wordsIn(text);
    for (const Word &word : words) {
        if (at < word.start || at > word.start + word.length) {
            continue;
        }
        const QString w = wordFrom(text, word);
        if (!wrong(w)) {
            return {};
        }
        QVariantList suggestions;
        const QStringList all = m_speller.suggest(w);
        for (const QString &s : all.mid(0, 5)) {
            suggestions << s;
        }
        return {
            {QStringLiteral("word"), w},
            {QStringLiteral("start"), block.position() + word.start},
            {QStringLiteral("end"), block.position() + word.start + word.length},
            {QStringLiteral("suggestions"), suggestions},
        };
    }
    return {};
}

void SpellCheck::replace(int start, int end, const QString &with)
{
    if (!document() || start < 0 || end <= start || end > document()->characterCount()) {
        return;
    }
    QTextCursor cursor(document());
    cursor.setPosition(start);
    cursor.setPosition(end, QTextCursor::KeepAnchor);
    // The new word keeps the old one's bold, italic and the rest.
    const QTextCharFormat format = cursor.charFormat();
    cursor.insertText(with, format);
}

void SpellCheck::learn(const QString &word)
{
    m_speller.addToPersonal(word);
    m_session.insert(word);
    recheckAll();
}

void SpellCheck::ignore(const QString &word)
{
    m_session.insert(word);
    recheckAll();
}
