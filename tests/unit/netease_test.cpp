#include "common/json.h"
#include "fake_transport.h"
#include "netease/client.h"
#include "netease/lyrics.h"
#include "netease/weapi.h"
#include <QJsonArray>
#include <QUrlQuery>
#include <QtTest>
#include <optional>

using namespace nekotune;
using namespace netease;

namespace {
const QString secret = "B3v3kH4vRPWRJFfH";
const QString lrc = "[00:01.000]Hello world\n[00:03.000]Goodbye";
const QString yrc = "[1000,2000](1000,1000,0)Hello (2000,1000,0)world\n[3000,1000](3000,1000,0)Goodbye";
const QString translated = "[00:01.000]你好世界\n[00:03.000]再见";
QJsonObject song() {
    return QJsonObject{{"id", 2155422573LL},
                       {"name", "使一颗心免于哀伤"},
                       {"ar", QJsonArray{QJsonObject{{"name", "知更鸟"}}, QJsonObject{{"name", "HOYO-MiX"}}}},
                       {"al", QJsonObject{{"name", "INSIDE"}, {"picUrl", "http://p3.music.126.net/cover.jpg"}}},
                       {"dt", 202163}};
}
QJsonObject ok(QJsonObject data) {
    data.insert("code", 200);
    return data;
}
QJsonValue payload(const ext::HttpRequest &request) {
    const QUrlQuery form(QString::fromLatin1(request.body));
    const auto params = form.queryItemValue("params", QUrl::FullyDecoded).toLatin1();
    return *ext::parseJson(aesDecrypt(aesDecrypt(params, secret.toLatin1()), "0CoJUm6Qyw8W8jud"));
}
template <class T> std::optional<Result<T>> wait(std::function<void(std::function<void(Result<T>)>)> start) {
    std::optional<Result<T>> result;
    start([&](Result<T> value) { result = std::move(value); });
    return result;
}
struct Fixture {
    FakeTransport transport;
    qint64 clock = 0;
    Config config;
    Client client{transport, [this] { return config; }, [this] { return clock; }, [](const QByteArray &json) {
                      const auto body = weapi(json, secret);
                      return ext::formBody({{"params", body.params}, {"encSecKey", body.encSecKey}});
                  }};
    Result<QVariantMap> run(std::function<void(Reply)> call) {
        std::optional<Result<QVariantMap>> result;
        call([&](Result<QVariantMap> value) { result = std::move(value); });
        if (!result)
            return failure("pending");
        return *result;
    }
};
QString error(const Result<QVariantMap> &result) { return result ? QString() : result.error().message; }
} // namespace

class NeteaseTest final : public QObject {
    Q_OBJECT
  private slots:
    void weapiMatchesUpstreamVector();
    void settingsIdsTracksAndUrls();
    void searchAndDetailSendEncryptedPosts();
    void playlistBatchesPreserveOrderAndCache();
    void audioUsesBitrateAndCdnUrls();
    void lyricsConvertAndCache();
    void upstreamErrorsAndCancellation();
};

void NeteaseTest::weapiMatchesUpstreamVector() {
    const QByteArray data = R"({"s":"晴天","type":1})";
    const auto encrypted = weapi(data, secret);
    QCOMPARE(encrypted.encSecKey,
             "85302b818aea19b68db899c25dac229412d9bba9b3fcfe4f714dc016bc1686fc446a08844b1f8327fd9cb623cc189be00c5a3"
             "65ac835e93d4858ee66f43fdc59e32aaed3ef24f0675d70172ef688d376a4807228c55583fe5bac647d10ecef15220feef614"
             "77c28cae8406f6f9896ed329d6db9f88757e31848a6c2ce2f94308");
    QCOMPARE(encrypted.params, "iGMUl8woV7J/SYWoXdz1JYpmNQQo3pcfNh9OhL8Cpf7wGvHpmJSf+qO1686nxDNH");
    QCOMPARE(aesDecrypt(aesDecrypt(encrypted.params.toLatin1(), secret.toLatin1()), "0CoJUm6Qyw8W8jud"), data);
    QVERIFY(weapi(data).encSecKey != weapi(data).encSecKey);
}
void NeteaseTest::settingsIdsTracksAndUrls() {
    QCOMPARE(configuration({}).value().toMap(),
             (QVariantMap{{"bitrate", "320"}, {"word_lyrics", true}, {"translation", true}}));
    QCOMPARE(configuration({{"enabled", false}, {"service_url", "https://obsolete.example"}, {"bitrate", "128"}})
                 .value()
                 .bitrate,
             "128");
    for (const auto &bad : {QVariantMap{{"bitrate", "flac"}}, QVariantMap{{"translation", "false"}},
                            QVariantMap{{"word_lyrics", 1}}})
        QVERIFY(!configuration(bad));
    for (const auto &bad : {"0", "-1", "1e9", "../song", "123&br=320"})
        QVERIFY(!validTrackId(bad));
    auto second = song();
    second.insert("id", "2");
    second.insert("dt", "bad");
    second.insert("al", QJsonObject{{"pic_str", "109951165566227"}});
    const auto tracks = parseTracks(QJsonArray{song(), song(), second, QJsonValue()});
    QCOMPARE(tracks.size(), 2);
    const auto first = tracks[0].toMap();
    QCOMPARE(first.value("duration_ms").toLongLong(), 202163);
    QCOMPARE(first.value("artist").toString(), "知更鸟/HOYO-MiX");
    QCOMPARE(first.value("cover_url").toString(), "https://p3.music.126.net/cover.jpg");
    QCOMPARE(tracks[1].toMap().value("cover_url").toString(), pictureUrl("109951165566227"));
    QCOMPARE(tracks[1].toMap().value("duration_ms").toLongLong(), 0);
    const auto precise = ext::parseJson(R"({"id":9007199254740993,"al":{"pic":109951173569626660},"dt":202163})")->toObject();
    QCOMPARE(precise.value("id").toString(), "9007199254740993");
    QCOMPARE(precise.value("al").toObject().value("pic").toString(), "109951173569626660");
    QCOMPARE(precise.value("dt").toInt(), 202163);
    for (const auto &bad : {"https://music.126.net.evil.example/a", "https://evil.example/a",
                            "https://x.music.126.net:444/a", "https://secret@x.music.126.net/a", "file:///tmp/audio"})
        QVERIFY2(cdnUrl(bad).isEmpty(), bad);
}
void NeteaseTest::searchAndDetailSendEncryptedPosts() {
    Fixture f;
    f.transport.handler = [](const ext::HttpRequest &request) -> std::optional<Result<ext::HttpResponse>> {
        if (request.url.path().endsWith("/weapi/cloudsearch/pc"))
            return jsonResponse(ok({{"result", QJsonObject{{"songs", QJsonArray{song()}}, {"songCount", 65}}}}));
        auto big = song();
        big.insert("id", "9007199254740993");
        return jsonResponse(ok({{"songs", QJsonArray{big}}}));
    };
    const auto page = f.run([&](Reply done) { f.client.search("晴天 & music", "2", done); });
    QVERIFY2(page, qPrintable(error(page)));
    QCOMPARE(page.value().value("cursor").toString(), "3");
    const auto &search = f.transport.requests[0];
    QCOMPARE(search.url.toString(), "https://music.163.com/weapi/cloudsearch/pc");
    QCOMPARE(payload(search).toObject(), (QJsonObject{{"s", "晴天 & music"}, {"type", 1}, {"limit", 30}, {"offset", 30}, {"total", "true"}}));
    QVERIFY(!search.body.contains("晴天"));
    const auto track = f.run([&](Reply done) { f.client.track("9007199254740993", done); });
    QCOMPARE(track.value().value("id").toString(), "9007199254740993");
    QCOMPARE(payload(f.transport.requests[1]).toObject().value("c").toString(), R"([{"id":9007199254740993,"v":0}])");
    for (const auto &request : f.transport.requests) {
        QCOMPARE(request.method, "POST");
        QVERIFY(!request.followRedirects);
    }
    QCOMPARE(error(f.run([&](Reply done) { f.client.search(QString(65, 'x'), {}, done); })), "netease_invalid_query");
    QCOMPARE(error(f.run([&](Reply done) { f.client.search("x", "0", done); })), "netease_invalid_query");
    Fixture empty;
    empty.transport.handler = [](const ext::HttpRequest &) -> std::optional<Result<ext::HttpResponse>> {
        return jsonResponse(ok({{"result", QJsonObject{{"songCount", 0}}}}));
    };
    QCOMPARE(empty.run([&](Reply done) { empty.client.search("x", {}, done); }).value(),
             (QVariantMap{{"tracks", QVariantList{}}}));
}
void NeteaseTest::playlistBatchesPreserveOrderAndCache() {
    Fixture f;
    QStringList ids;
    for (int i = 1; i <= 1001; ++i)
        ids.append(QString::number(i));
    f.transport.handler = [ids](const ext::HttpRequest &request) -> std::optional<Result<ext::HttpResponse>> {
        if (request.url.path().endsWith("/playlist/detail")) {
            QJsonArray rows;
            for (const auto &id : ids)
                rows.append(QJsonObject{{"id", id}});
            return jsonResponse(ok({{"playlist", QJsonObject{{"trackIds", rows}}}}));
        }
        const auto batch = ext::parseJson(payload(request).toObject().value("c").toString().toUtf8())->toArray();
        QJsonArray songs;
        for (qsizetype i = batch.size(); i-- > 0;) {
            auto item = song();
            item.insert("id", ext::stringId(batch[i].toObject().value("id")));
            songs.append(item);
        }
        return jsonResponse(ok({{"songs", songs}}));
    };
    const auto first = f.run([&](Reply done) { f.client.playlist("9", done); });
    QVERIFY2(first, qPrintable(error(first)));
    QStringList order;
    for (const auto &track : first.value().value("tracks").toList())
        order.append(track.toMap().value("id").toString());
    QCOMPARE(order, ids);
    QCOMPARE(f.transport.requests.size(), 4);
    QList<qsizetype> sizes;
    for (qsizetype i = 1; i < 4; ++i)
        sizes.append(ext::parseJson(payload(f.transport.requests[i]).toObject().value("c").toString().toUtf8())->toArray().size());
    QCOMPARE(sizes, (QList<qsizetype>{500, 500, 1}));
    QCOMPARE(f.run([&](Reply done) { f.client.playlist("9", done); }).value().value("tracks").toList().size(), 1001);
    QCOMPARE(f.transport.requests.size(), 4);
    f.clock = 600'001;
    f.run([&](Reply done) { f.client.playlist("9", done); });
    QCOMPARE(f.transport.requests.size(), 8);
}
void NeteaseTest::audioUsesBitrateAndCdnUrls() {
    Fixture f;
    f.clock = 1000;
    f.config.bitrate = "128";
    f.transport.handler = [](const ext::HttpRequest &) -> std::optional<Result<ext::HttpResponse>> {
        return rawResponse(R"({"code":200,"data":[{"id":2155422573,"url":null,"uf":{"url":"http://m701.music.126.net/audio.mp3"},"expi":60,"type":"mp3"}]})");
    };
    const auto audio = f.run([&](Reply done) { f.client.resolve("2155422573", done); });
    QVERIFY2(audio, qPrintable(error(audio)));
    QCOMPARE(payload(f.transport.requests[0]).toObject(), (QJsonObject{{"ids", QJsonArray{2155422573LL}}, {"br", 128000}}));
    QCOMPARE(audio.value().value("url").toString(), "https://m701.music.126.net/audio.mp3");
    QCOMPARE(audio.value().value("expiresAt").toLongLong(), 61000);
    QVERIFY(!audio.value().value("headers").toMap().contains("Cookie"));
    for (const auto &[body, expected] :
         {std::pair{QByteArray(R"({"code":200,"data":[{"id":2155422573,"url":null,"code":404}]})"), "netease_unavailable"},
          {R"({"code":200,"data":[{"id":2155422573,"url":"https://evil.example/a"}]})", "netease_invalid_audio"},
          {R"({"code":200,"data":[{"id":7,"url":"https://m701.music.126.net/a"}]})", "netease_unavailable"}}) {
        Fixture other;
        other.transport.handler = [body](const ext::HttpRequest &) -> std::optional<Result<ext::HttpResponse>> {
            return rawResponse(body);
        };
        QCOMPARE(error(other.run([&](Reply done) { other.client.resolve("2155422573", done); })), expected);
    }
}
void NeteaseTest::lyricsConvertAndCache() {
    Fixture f;
    int calls = 0;
    f.transport.handler = [&](const ext::HttpRequest &request) -> std::optional<Result<ext::HttpResponse>> {
        ++calls;
        [&] { QCOMPARE(payload(request).toObject().value("yv").toInt(), 1); }();
        return jsonResponse(ok({{"lrc", QJsonObject{{"lyric", lrc}}},
                                {"yrc", QJsonObject{{"lyric", yrc}}},
                                {"tlyric", QJsonObject{{"lyric", translated}}}}));
    };
    const auto raw = f.run([&](Reply done) { f.client.lyric("1", true, true, done); });
    QCOMPARE(raw.value().value("type").toString(), "yrc");
    QCOMPARE(raw.value().value("lyric").toString(), yrc);
    QCOMPARE(raw.value().value("tlyric").toString(), translated);
    const auto document = f.run([&](Reply done) { f.client.lyrics("1", done); });
    QCOMPARE(calls, 1);
    QVERIFY(document.value().value("krc_lyrics").toString().contains("[1000,2000]<0,1000,0>Hello <1000,1000,0>world / 你好世界"));
    QVERIFY(document.value().value("synced_lyrics").toString().contains("Hello world / 你好世界"));
    QCOMPARE(f.run([&](Reply done) { f.client.lyric("1", false, false, done); }).value().value("tlyric").toString(), "");
    f.clock = 604'800'001;
    f.run([&](Reply done) { f.client.lyrics("1", done); });
    QCOMPARE(calls, 2);
    QCOMPARE(mergeTranslation("[offset:500]\n[00:01.000]Hello", "[00:01.500]你好"), "[00:01.500]Hello / 你好");
    const auto fallback = lyricDocument({lrc, "malformed", {}, false}, true, true);
    QCOMPARE(fallback.value().value("krc_lyrics").toString(), "");
    QCOMPARE(fallback.value().value("synced_lyrics").toString(), lrc);
    QCOMPARE(yrcToKrc("[1000,1](1,1,0)bad"), "");
    QCOMPARE(mergeTranslation("[00:01.00][00:03.00]Hello", translated), "[00:01.000]Hello / 你好世界\n[00:03.000]Hello / 再见");
    QCOMPARE(lyricDocument({lrc, yrc, translated, false}, false, false).value().value("synced_lyrics").toString(), lrc);
    QCOMPARE(lyricDocument({}, true, true).error().message, "netease_no_lyrics");
    QVERIFY(lyricDocument({{}, {}, {}, true}, true, true).value().value("instrumental").toBool());
}
void NeteaseTest::upstreamErrorsAndCancellation() {
    const QList<std::pair<ext::HttpResponse, QString>> cases{
        {rawResponse({}, 302, {{"location", "https://evil.example"}}), "netease_service_error"},
        {rawResponse({}, 429), "netease_rate_limited"},
        {rawResponse("invalid"), "netease_invalid_response"},
        {jsonResponse({{"code", 301}, {"msg", "private"}}), "netease_unavailable"},
        {jsonResponse({{"code", 500}, {"msg", "private"}}), "netease_service_error"},
        {jsonResponse(ok({})), "netease_invalid_response"},
    };
    for (const auto &[response, expected] : cases) {
        Fixture f;
        f.transport.handler = [response](const ext::HttpRequest &) -> std::optional<Result<ext::HttpResponse>> {
            return response;
        };
        QCOMPARE(error(f.run([&](Reply done) { f.client.search("x", {}, done); })), expected);
    }
    Fixture large;
    large.transport.handler = [](const ext::HttpRequest &) -> std::optional<Result<ext::HttpResponse>> {
        return failure("response_too_large");
    };
    QCOMPARE(error(large.run([&](Reply done) { large.client.search("x", {}, done); })), "netease_response_too_large");
    Fixture network;
    network.transport.handler = [](const ext::HttpRequest &) -> std::optional<Result<ext::HttpResponse>> {
        return failure("timeout");
    };
    QCOMPARE(error(network.run([&](Reply done) { network.client.search("x", {}, done); })), "netease_network_error");
    Fixture pending;
    std::optional<Result<QVariantMap>> result;
    pending.client.search("x", {}, [&](Result<QVariantMap> value) { result = value; });
    QVERIFY(!result);
    pending.client.close();
    QVERIFY(result && !*result);
    QCOMPARE(result->error().message, "netease_cancelled");
    QCOMPARE(error(pending.run([&](Reply done) { pending.client.search("x", {}, done); })), "netease_cancelled");
}

QTEST_GUILESS_MAIN(NeteaseTest)
#include "netease_test.moc"
