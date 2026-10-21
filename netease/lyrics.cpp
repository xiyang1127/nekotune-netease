#include "lyrics.h"
#include <QHash>
#include <QRegularExpression>

namespace netease {
namespace {
struct Row {
    qint64 time;
    QString text;
};
QList<Row> lrcRows(const QString &text) {
    static const QRegularExpression offsetPattern(R"(^\[offset:([+-]?\d+)\]\s*$)",
                                                  QRegularExpression::MultilineOption | QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression stampPattern(R"(\[(\d+):(\d{2})(?:\.(\d{1,3}))?\])");
    qint64 offset = 0;
    for (auto it = offsetPattern.globalMatch(text); it.hasNext();) {
        bool ok = false;
        const auto value = it.next().captured(1).toLongLong(&ok);
        offset = ok ? value : 0;
    }
    QList<Row> rows;
    for (const auto &line : text.split(QRegularExpression("\r?\n"))) {
        const auto content = QString(line).remove(stampPattern).trimmed();
        if (content.isEmpty())
            continue;
        for (auto it = stampPattern.globalMatch(line); it.hasNext();) {
            const auto stamp = it.next();
            const qint64 time = stamp.captured(1).toLongLong() * 60000 + stamp.captured(2).toLongLong() * 1000 +
                                stamp.captured(3).leftJustified(3, '0').toLongLong() + offset;
            rows.append({qMax<qint64>(0, time), content});
        }
    }
    return rows;
}
QHash<qint64, QString> translations(const QString &text) {
    QHash<qint64, QString> result;
    for (const auto &row : lrcRows(text))
        result.insert(row.time, row.text);
    return result;
}
QString timestamp(qint64 time) {
    return QStringLiteral("[%1:%2.%3]")
        .arg(time / 60000, 2, 10, QLatin1Char('0'))
        .arg((time / 1000) % 60, 2, 10, QLatin1Char('0'))
        .arg(time % 1000, 3, 10, QLatin1Char('0'));
}
} // namespace

QString mergeTranslation(const QString &lrc, const QString &translation) {
    if (translation.isEmpty())
        return lrc;
    const auto translated = translations(translation);
    const auto rows = lrcRows(lrc);
    if (rows.isEmpty())
        return lrc;
    QStringList lines;
    for (const auto &row : rows) {
        auto line = timestamp(row.time) + row.text;
        if (translated.contains(row.time) && translated.value(row.time) != row.text)
            line += " / " + translated.value(row.time);
        lines.append(line);
    }
    return lines.join('\n');
}

QString yrcToKrc(const QString &yrc, const QString &translation) {
    static const QRegularExpression linePattern(R"(^\[(\d+),(\d+)\](.*)$)");
    static const QRegularExpression wordPattern(R"(\((\d+),(\d+),\d+\)([\s\S]*?)(?=\(\d+,\d+,\d+\)|$))");
    static const QRegularExpression markup(R"(<(-?\d+),(\d+),(-?\d+)>)");
    const auto translated = translations(translation);
    QStringList lines;
    for (const auto &source : yrc.split(QRegularExpression("\r?\n"))) {
        const auto line = linePattern.match(source);
        if (!line.hasMatch())
            continue;
        bool startOk = false, durationOk = false;
        const qint64 start = line.captured(1).toLongLong(&startOk), duration = line.captured(2).toLongLong(&durationOk);
        if (!startOk || !durationOk || start + duration > 86'400'000)
            continue;
        QStringList pieces;
        bool valid = true, any = false;
        for (auto it = wordPattern.globalMatch(line.captured(3)); it.hasNext();) {
            const auto word = it.next();
            any = true;
            bool lengthOk = false;
            const qint64 offset = word.captured(1).toLongLong() - start, length = word.captured(2).toLongLong(&lengthOk);
            if (offset < 0 || !lengthOk || offset + length > duration + 100) {
                valid = false;
                break;
            }
            // YRC timestamps are absolute; the host's KRC word offsets are relative to the line.
            pieces.append(QStringLiteral("<%1,%2,0>%3").arg(offset).arg(length).arg(QString(word.captured(3)).remove(markup)));
        }
        if (!any || !valid)
            continue;
        lines.append(QStringLiteral("[%1,%2]").arg(start).arg(duration) + pieces.join(QString()) +
                     (translated.contains(start) ? " / " + translated.value(start) : QString()));
    }
    return lines.join('\n');
}

nekotune::Result<QVariantMap> lyricDocument(const LyricData &data, bool wordLyrics, bool translation) {
    const auto merged = mergeTranslation(data.lrc, translation ? data.tlyric : QString());
    const auto krc = wordLyrics ? yrcToKrc(data.yrc, translation ? data.tlyric : QString()) : QString();
    if (merged.trimmed().isEmpty() && krc.isEmpty() && !data.instrumental)
        return nekotune::failure("netease_no_lyrics", nekotune::ErrorCode::NotFound);
    return QVariantMap{{"synced_lyrics", lrcRows(merged).isEmpty() ? QString() : merged},
                       {"plain_lyrics", merged},
                       {"krc_lyrics", krc},
                       {"instrumental", data.instrumental}};
}
} // namespace netease
