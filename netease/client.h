#pragma once
#include "../common/http.h"
#include "lyrics.h"
#include <nekotune/result.h>
#include <QJsonObject>
#include <QVariantMap>
#include <deque>
#include <functional>
#include <map>

namespace netease {
using nekotune::Result;
using Reply = std::function<void(Result<QVariantMap>)>;

struct Config {
    QString bitrate = "320";
    bool wordLyrics = true;
    bool translation = true;
    QVariantMap toMap() const { return {{"bitrate", bitrate}, {"word_lyrics", wordLyrics}, {"translation", translation}}; }
};
/// Normalize settings, dropping keys from the earlier server-based adapter. Fails with
/// netease_invalid_config.
Result<Config> configuration(const QVariantMap &config);
bool validTrackId(const QString &id);
/// Restrict audio and artwork URLs to NetEase's CDN and force HTTPS; empty when rejected.
QString cdnUrl(const QString &value);
/// Track maps in the JavaScript plugin's shape (id, title, artist, album, duration_ms, cover_url).
QVariantList parseTracks(const QJsonValue &songs, bool *ok = nullptr);

/// Direct weapi client. All callbacks run on the owning thread; cancel() fails in-flight work.
class Client {
  public:
    using Encrypt = std::function<QByteArray(const QByteArray &json)>;
    Client(nekotune::ext::HttpTransport &transport, std::function<Config()> config,
           std::function<qint64()> now = {}, Encrypt encrypt = {});
    void cancel();
    void close();
    void search(const QString &query, const QString &cursor, Reply done);
    void track(const QString &id, Reply done);
    void playlist(const QString &id, Reply done);
    /// ResolvedAudio map for the configured bitrate.
    void resolve(const QString &id, Reply done);
    void lyric(const QString &id, bool dwrc, bool trlrc, Reply done);
    void lyrics(const QString &id, Reply done);

  private:
    void request(const QString &operation, const QByteArray &json, std::function<void(Result<QJsonObject>)> done);
    void details(const QStringList &ids, std::function<void(Result<QVariantList>)> done);
    void cached(const QString &key, qint64 ttl, std::function<void(Reply)> load, Reply done);
    void lyricData(const QString &id, Reply done);

    struct Entry {
        QVariantMap value;
        qint64 size = 0, expires = 0;
    };
    nekotune::ext::HttpTransport &m_transport;
    std::function<Config()> m_config;
    std::function<qint64()> m_now;
    Encrypt m_encrypt;
    std::deque<std::pair<QString, Entry>> m_cache;
    std::map<QString, QList<Reply>> m_inflight;
    quint64 m_generation = 0;
    bool m_closed = false;
};
} // namespace netease
