#include "client.h"
#include "../common/json.h"
#include "weapi.h"
#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <memory>

using namespace nekotune;
using nekotune::ext::HttpRequest;
using nekotune::ext::HttpResponse;

namespace netease {
namespace {
const QHash<QString, QString> endpoints{{"search", "/weapi/cloudsearch/pc"},
                                        {"song", "/weapi/v3/song/detail/"},
                                        {"playlist", "/weapi/v6/playlist/detail"},
                                        {"lyric", "/weapi/song/lyric"},
                                        {"url", "/weapi/song/enhance/player/url"}};
constexpr int pageSize = 30;
const QByteArray referer = "https://music.163.com/";
const QByteArray userAgent = "Mozilla/5.0 (iPhone; CPU iPhone OS 15_0 like Mac OS X) AppleWebKit/605.1.15 (KHTML, like "
                             "Gecko) Mobile/15E148 CloudMusic/0.1.1 NeteaseMusic/8.2.30";
AppError fail(const QString &code) { return failure("netease_" + code); }
QString truncated(const QString &text, int length) {
    // Slice by code points like JavaScript's String.prototype.slice on most text.
    return text.left(length);
}
qsizetype codePoints(const QString &text) { return text.toUcs4().size(); }
} // namespace

Result<Config> configuration(const QVariantMap &config) {
    Config result;
    const auto bitrate = config.value("bitrate", "320");
    const auto word = config.value("word_lyrics", true), translation = config.value("translation", true);
    if (bitrate.typeId() != QMetaType::QString || !QStringList{"128", "192", "320"}.contains(bitrate.toString()) ||
        word.typeId() != QMetaType::Bool || translation.typeId() != QMetaType::Bool)
        return fail("invalid_config");
    result.bitrate = bitrate.toString();
    result.wordLyrics = word.toBool();
    result.translation = translation.toBool();
    return result;
}
bool validTrackId(const QString &id) {
    static const QRegularExpression pattern("^[1-9]\\d{0,19}$");
    return pattern.match(id).hasMatch();
}
QString cdnUrl(const QString &value) {
    QUrl url(value, QUrl::StrictMode);
    if (!url.isValid() || (url.scheme() != "http" && url.scheme() != "https") || !url.userName().isEmpty() ||
        !url.password().isEmpty() || (url.port() != -1 && url.port() != 80 && url.port() != 443))
        return {};
    const auto host = url.host();
    if (!host.endsWith(".music.126.net") && !host.endsWith(".music.163.com"))
        return {};
    url.setScheme("https");
    url.setPort(-1);
    url.setFragment({});
    return url.toString(QUrl::FullyEncoded);
}
QVariantList parseTracks(const QJsonValue &songs, bool *ok) {
    if (ok)
        *ok = songs.isArray();
    QVariantList tracks;
    QSet<QString> seen;
    for (const auto &value : songs.toArray()) {
        if (!value.isObject())
            continue;
        const auto song = value.toObject();
        const auto id = ext::stringId(song.value("id"));
        const auto name = song.value("name").toString().trimmed();
        if (!validTrackId(id) || seen.contains(id) || name.isEmpty())
            continue;
        const auto album = song.contains("al") && song.value("al").isObject() ? song.value("al").toObject()
                                                                               : song.value("album").toObject();
        auto cover = cdnUrl(album.value("picUrl").toString());
        if (cover.isEmpty()) {
            QString picture;
            for (const auto *key : {"pic_str", "picId_str", "pic", "picId"})
                if (album.contains(key) && !album.value(key).isNull()) {
                    picture = ext::stringId(album.value(key));
                    break;
                }
            cover = pictureUrl(picture.isEmpty() ? id : picture);
        }
        const auto durationValue = song.contains("dt") ? song.value("dt") : song.value("duration");
        const double duration = durationValue.isDouble() ? durationValue.toDouble()
                                : durationValue.isString() ? durationValue.toString().toDouble()
                                                           : 0;
        QStringList artists;
        const auto artistList = song.contains("ar") ? song.value("ar") : song.value("artists");
        for (const auto &artist : artistList.toArray())
            if (const auto artistName = artist.toObject().value("name").toString(); !artistName.isEmpty())
                artists.append(artistName);
        tracks.append(QVariantMap{
            {"id", id},
            {"title", truncated(name, 500)},
            {"artist", truncated(artists.join('/'), 1000)},
            {"album", truncated(album.value("name").toString(), 500)},
            {"duration_ms", duration > 0 ? qMin<qint64>(qRound64(duration), 86'400'000) : 0},
            {"cover_url", cover}});
        seen.insert(id);
    }
    return tracks;
}

Client::Client(ext::HttpTransport &transport, std::function<Config()> config, std::function<qint64()> now,
               Encrypt encrypt)
    : m_transport(transport), m_config(std::move(config)),
      m_now(now ? std::move(now) : [] { return QDateTime::currentMSecsSinceEpoch(); }),
      m_encrypt(encrypt ? std::move(encrypt) : [](const QByteArray &json) {
          const auto body = weapi(json);
          return ext::formBody({{"params", body.params}, {"encSecKey", body.encSecKey}});
      }) {}

void Client::cancel() {
    ++m_generation;
    m_transport.cancelAll();
    const auto inflight = std::exchange(m_inflight, {});
    for (const auto &[key, waiters] : inflight)
        for (const auto &waiter : waiters)
            waiter(fail("cancelled"));
}
void Client::close() {
    m_closed = true;
    cancel();
    m_cache.clear();
}

void Client::request(const QString &operation, const QByteArray &json, std::function<void(Result<QJsonObject>)> done) {
    if (m_closed) {
        done(fail("cancelled"));
        return;
    }
    HttpRequest request;
    request.method = "POST";
    request.url = QUrl("https://music.163.com" + endpoints.value(operation));
    request.headers = {{"Referer", referer},
                       {"Cookie", "appver=8.2.30; os=iPhone OS; osver=15.0; EVNSM=1.0.0; buildver=2206; "
                                  "channel=distribution; machineid=iPhone13.3"},
                       {"User-Agent", userAgent},
                       {"Content-Type", "application/x-www-form-urlencoded"},
                       {"Accept", "application/json"},
                       {"Accept-Language", "zh-CN,zh;q=0.8,zh-TW;q=0.4"}};
    request.body = m_encrypt(json);
    m_transport.send(request, [done](Result<HttpResponse> result) {
        if (!result) {
            const auto &message = result.error().message;
            done(fail(message == "cancelled" ? "cancelled" : message == "response_too_large" ? message : "network_error"));
            return;
        }
        const auto &response = result.value();
        if (response.status == 404)
            return done(fail("unavailable"));
        if (response.status == 429)
            return done(fail("rate_limited"));
        if (response.status < 200 || response.status >= 300)
            return done(fail("service_error"));
        const auto data = ext::parseJson(response.body);
        if (!data || !data->isObject())
            return done(fail("invalid_response"));
        const auto object = data->toObject();
        const auto code = ext::integer(object.value("code"), -1);
        if (QList<qint64>{301, 401, 403, 404}.contains(code))
            return done(fail("unavailable"));
        if (code == 429 || code == 405)
            return done(fail("rate_limited"));
        if (code != 200)
            return done(fail("service_error"));
        done(object);
    });
}
void Client::cached(const QString &key, qint64 ttl, std::function<void(Reply)> load, Reply done) {
    if (m_closed) {
        done(fail("cancelled"));
        return;
    }
    for (auto it = m_cache.begin(); it != m_cache.end(); ++it)
        if (it->first == key && it->second.expires > m_now()) {
            done(it->second.value);
            return;
        }
    if (auto it = m_inflight.find(key); it != m_inflight.end()) {
        it->second.append(std::move(done));
        return;
    }
    m_inflight[key].append(std::move(done));
    const auto generation = m_generation;
    load([this, key, ttl, generation](Result<QVariantMap> result) {
        auto found = m_inflight.find(key);
        if (found == m_inflight.end())
            return; // cancelled; waiters already failed
        const auto waiters = std::move(found->second);
        m_inflight.erase(found);
        if (result && generation != m_generation)
            result = fail("cancelled");
        if (result) {
            const auto size = QJsonDocument::fromVariant(result.value()).toJson(QJsonDocument::Compact).size();
            if (size <= 4 * 1024 * 1024) {
                std::erase_if(m_cache, [&](const auto &entry) { return entry.first == key; });
                m_cache.push_back({key, {result.value(), size, m_now() + ttl}});
                qint64 total = 0;
                for (const auto &entry : m_cache)
                    total += entry.second.size;
                while (m_cache.size() > 32 || total > 8 * 1024 * 1024) {
                    total -= m_cache.front().second.size;
                    m_cache.pop_front();
                }
            }
        }
        for (const auto &waiter : waiters)
            waiter(result);
    });
}

void Client::search(const QString &query, const QString &cursor, Reply done) {
    const auto trimmed = query.trimmed();
    bool numeric = false;
    const int page = (cursor.isEmpty() ? QStringLiteral("1") : cursor).toInt(&numeric);
    if (trimmed.isEmpty() || codePoints(trimmed) > 64 || !numeric || page < 1 || page > 1000) {
        done(fail("invalid_query"));
        return;
    }
    const auto json = QJsonDocument(QJsonObject{{"s", trimmed},
                                                {"type", 1},
                                                {"limit", pageSize},
                                                {"offset", (page - 1) * pageSize},
                                                {"total", "true"}})
                          .toJson(QJsonDocument::Compact);
    request("search", json, [done, page](Result<QJsonObject> data) {
        if (!data)
            return done(data.error());
        const auto result = data.value().value("result");
        if (!result.isObject())
            return done(fail("invalid_response"));
        const auto object = result.toObject();
        auto songs = object.value("songs");
        if (songs.isUndefined() || songs.isNull())
            songs = ext::integer(object.value("songCount"), -1) == 0 ? QJsonValue(QJsonArray{}) : QJsonValue();
        bool ok = false;
        const auto tracks = parseTracks(songs, &ok);
        if (!ok)
            return done(fail("invalid_response"));
        const bool more = object.value("songCount").isDouble() ? object.value("songCount").toDouble() > page * pageSize
                                                                : songs.toArray().size() >= pageSize;
        QVariantMap reply{{"tracks", tracks}};
        if (more && page < 1000)
            reply.insert("cursor", QString::number(page + 1));
        done(reply);
    });
}
void Client::details(const QStringList &ids, std::function<void(Result<QVariantList>)> done) {
    QStringList items;
    for (const auto &id : ids) {
        if (!validTrackId(id)) {
            done(fail("invalid_id"));
            return;
        }
        items.append(QStringLiteral("{\"id\":%1,\"v\":0}").arg(id));
    }
    const auto json = QJsonDocument(QJsonObject{{"c", "[" + items.join(',') + "]"}}).toJson(QJsonDocument::Compact);
    request("song", json, [done](Result<QJsonObject> data) {
        if (!data)
            return done(data.error());
        bool ok = false;
        const auto tracks = parseTracks(data.value().value("songs"), &ok);
        if (!ok)
            return done(fail("invalid_response"));
        done(tracks);
    });
}
void Client::track(const QString &id, Reply done) {
    if (!validTrackId(id)) {
        done(fail("invalid_id"));
        return;
    }
    details({id}, [id, done](Result<QVariantList> tracks) {
        if (!tracks)
            return done(tracks.error());
        for (const auto &track : tracks.value())
            if (track.toMap().value("id") == id)
                return done(track.toMap());
        done(fail("unavailable"));
    });
}
void Client::playlist(const QString &id, Reply done) {
    if (!validTrackId(id)) {
        done(fail("invalid_id"));
        return;
    }
    cached("playlist:" + id, 600'000,
           [this, id](Reply loaded) {
               const auto json = QJsonDocument(QJsonObject{
                                                   {"id", id}, {"offset", "0"}, {"total", "True"}, {"limit", "1000"}, {"n", "1000"}})
                                     .toJson(QJsonDocument::Compact);
               auto budget = std::make_shared<QElapsedTimer>();
               budget->start();
               request("playlist", json, [this, loaded, budget](Result<QJsonObject> data) {
                   if (!data)
                       return loaded(data.error());
                   const auto rows = data.value().value("playlist").toObject().value("trackIds");
                   if (!rows.isArray())
                       return loaded(fail("invalid_response"));
                   QStringList ids;
                   for (const auto &row : rows.toArray()) {
                       const auto trackId = ext::stringId(row.toObject().value("id"));
                       if (!validTrackId(trackId))
                           return loaded(fail("invalid_id"));
                       ids.append(trackId);
                   }
                   // Bound work to the host's 30-second call deadline.
                   if (ids.size() > 10'000)
                       return loaded(fail("response_too_large"));
                   struct State {
                       QStringList ids;
                       QHash<QString, QVariant> found;
                       int pending = 0;
                       bool failed = false;
                   };
                   auto state = std::make_shared<State>();
                   state->ids = ids;
                   auto finish = [state, loaded] {
                       QVariantList tracks;
                       for (const auto &trackId : std::as_const(state->ids))
                           if (state->found.contains(trackId))
                               tracks.append(state->found.value(trackId));
                       loaded(QVariantMap{{"tracks", tracks}});
                   };
                   if (ids.isEmpty())
                       return finish();
                   // Up to four 500-id detail requests at a time, in 2000-id waves.
                   auto wave = std::make_shared<std::function<void(qsizetype)>>();
                   // The wave holds itself weakly; in-flight detail callbacks keep it alive.
                   *wave = [this, state, loaded, finish, budget, self = std::weak_ptr(wave)](qsizetype offset) {
                       if (offset >= state->ids.size()) {
                           finish();
                           return;
                       }
                       if (budget->elapsed() > 25'000) {
                           loaded(fail("cancelled"));
                           return;
                       }
                       const auto end = qMin(offset + 2000, state->ids.size());
                       // Count the whole wave first: a batch may complete synchronously.
                       state->pending = int((end - offset + 499) / 500);
                       const auto wave = self.lock();
                       for (auto start = offset; start < end; start += 500) {
                           details(state->ids.mid(start, qMin<qsizetype>(500, end - start)),
                                   [state, loaded, wave, end](Result<QVariantList> tracks) {
                                       if (state->failed)
                                           return;
                                       if (!tracks) {
                                           state->failed = true;
                                           loaded(tracks.error());
                                           return;
                                       }
                                       for (const auto &track : tracks.value())
                                           state->found.insert(track.toMap().value("id").toString(), track);
                                       if (--state->pending == 0)
                                           (*wave)(end);
                                   });
                       }
                   };
                   (*wave)(0);
               });
           },
           std::move(done));
}
void Client::resolve(const QString &id, Reply done) {
    if (!validTrackId(id)) {
        done(fail("invalid_id"));
        return;
    }
    const auto json = QStringLiteral("{\"ids\":[%1],\"br\":%2}").arg(id).arg(m_config().bitrate.toInt() * 1000).toUtf8();
    request("url", json, [this, id, done](Result<QJsonObject> data) {
        if (!data)
            return done(data.error());
        QJsonObject item;
        bool found = false;
        for (const auto &value : data.value().value("data").toArray())
            if (value.isObject() && ext::stringId(value.toObject().value("id")) == id) {
                item = value.toObject();
                found = true;
                break;
            }
        const auto direct = item.value("url").toString();
        const auto fallback = item.value("uf").toObject().value("url").toString();
        if (!found || (item.contains("code") && ext::integer(item.value("code")) != 200) ||
            (direct.isEmpty() && fallback.isEmpty()))
            return done(fail("unavailable"));
        const auto url = cdnUrl(direct.isEmpty() ? fallback : direct);
        if (url.isEmpty())
            return done(fail("invalid_audio"));
        const auto expiry = ext::integer(item.value("expi"));
        const auto type = item.value("type").toString();
        done(QVariantMap{
            {"url", url},
            {"headers", QVariantMap{{"Referer", QString::fromLatin1(referer)}, {"User-Agent", QString::fromLatin1(userAgent)}}},
            {"expiresAt", m_now() + qMin<qint64>(expiry > 0 ? expiry * 1000 : 300'000, 300'000)},
            {"extension", QStringList{"mp3", "flac", "m4a", "aac"}.contains(type) ? type : QStringLiteral("mp3")},
            {"maxBytes", 100LL * 1024 * 1024},
            {"allowedHosts", QStringList{"*.music.126.net", "*.music.163.com"}},
            {"contentTypes", QStringList{"audio/mpeg", "audio/mp3", "audio/flac", "audio/x-flac", "audio/mp4", "audio/aac",
                                         "application/octet-stream"}}});
    });
}
void Client::lyricData(const QString &id, Reply done) {
    if (!validTrackId(id)) {
        done(fail("invalid_id"));
        return;
    }
    cached("lyric:" + id, 604'800'000,
           [this, id](Reply loaded) {
               const auto json = QJsonDocument(QJsonObject{{"id", id},
                                                           {"os", "pc"},
                                                           {"lv", -1},
                                                           {"kv", -1},
                                                           {"tv", -1},
                                                           {"rv", -1},
                                                           {"yv", 1},
                                                           {"showRole", "False"},
                                                           {"cp", "False"},
                                                           {"e_r", "False"}})
                                     .toJson(QJsonDocument::Compact);
               request("lyric", json, [loaded](Result<QJsonObject> data) {
                   if (!data)
                       return loaded(data.error());
                   const auto text = [&](const char *field) {
                       return data.value().value(field).toObject().value("lyric").toString();
                   };
                   loaded(QVariantMap{{"lrc", text("lrc")},
                                      {"yrc", text("yrc")},
                                      {"tlyric", text("tlyric")},
                                      {"instrumental", data.value().value("nolyric").toBool() &&
                                                           data.value().value("nolyric").isBool()}});
               });
           },
           std::move(done));
}
void Client::lyric(const QString &id, bool dwrc, bool trlrc, Reply done) {
    lyricData(id, [dwrc, trlrc, done](Result<QVariantMap> data) {
        if (!data)
            return done(data.error());
        const auto &value = data.value();
        const bool yrc = dwrc && !value.value("yrc").toString().isEmpty();
        done(QVariantMap{{"lyric", value.value(yrc ? "yrc" : "lrc")},
                         {"tlyric", trlrc ? value.value("tlyric").toString() : QString()},
                         {"type", yrc ? "yrc" : "lrc"}});
    });
}
void Client::lyrics(const QString &id, Reply done) {
    lyricData(id, [this, done](Result<QVariantMap> data) {
        if (!data)
            return done(data.error());
        const auto config = m_config();
        done(lyricDocument({data.value().value("lrc").toString(), data.value().value("yrc").toString(),
                            data.value().value("tlyric").toString(), data.value().value("instrumental").toBool()},
                           config.wordLyrics, config.translation));
    });
}
} // namespace netease
