#pragma once
#include <nekotune/result.h>
#include <QVariantMap>

namespace netease {
struct LyricData {
    QString lrc, yrc, tlyric;
    bool instrumental = false;
};
/// Append translations (" / text") to LRC lines with the same timestamp.
QString mergeTranslation(const QString &lrc, const QString &translation);
/// Convert NetEase YRC (absolute word times) to the host's KRC text (line-relative offsets).
QString yrcToKrc(const QString &yrc, const QString &translation = {});
/// Host lyrics document; fails with netease_no_lyrics when nothing usable exists.
nekotune::Result<QVariantMap> lyricDocument(const LyricData &data, bool wordLyrics, bool translation);
} // namespace netease
