#include "client.h"
#include <nekotune/sdk/extension_plugin.h>
#include <QObject>
#include <memory>
#include <vector>

using namespace nekotune;

namespace {
sdk::Track toTrack(const QVariantMap &map) {
    return {map.value("id").toString(),       map.value("title").toString(), map.value("artist").toString(),
            map.value("album").toString(),    map.value("cover_url").toString(), map.value("duration_ms").toLongLong()};
}
sdk::ResolvedAudio toAudio(const QVariantMap &map) {
    sdk::ResolvedAudio audio;
    audio.url = QUrl(map.value("url").toString());
    const auto headers = map.value("headers").toMap();
    for (auto it = headers.cbegin(); it != headers.cend(); ++it)
        audio.headers.insert(it.key(), it.value().toString());
    audio.expiresAt = map.value("expiresAt").toLongLong();
    audio.extension = map.value("extension").toString();
    audio.maxBytes = map.value("maxBytes").toLongLong();
    audio.allowedHosts = map.value("allowedHosts").toStringList();
    audio.contentTypes = map.value("contentTypes").toStringList();
    return audio;
}

class Music final : public sdk::IMusicProvider {
  public:
    explicit Music(netease::Client &client) : m_client(client) {}
    void search(const QString &query, const QString &cursor, std::function<void(Result<sdk::SearchPage>)> done) override {
        m_client.search(query, cursor, [done](Result<QVariantMap> result) {
            if (!result)
                return done(result.error());
            sdk::SearchPage page;
            for (const auto &track : result.value().value("tracks").toList())
                page.tracks.append(toTrack(track.toMap()));
            page.cursor = result.value().value("cursor").toString();
            done(page);
        });
    }
    void track(const QString &id, std::function<void(Result<sdk::Track>)> done) override {
        m_client.track(id, [done](Result<QVariantMap> result) {
            done(result ? Result<sdk::Track>(toTrack(result.value())) : Result<sdk::Track>(result.error()));
        });
    }
    void resolve(const QString &id, const QString &, std::function<void(Result<sdk::ResolvedAudio>)> done) override {
        m_client.resolve(id, [done](Result<QVariantMap> result) {
            done(result ? Result<sdk::ResolvedAudio>(toAudio(result.value())) : Result<sdk::ResolvedAudio>(result.error()));
        });
    }

  private:
    netease::Client &m_client;
};

class Lyrics final : public sdk::ILyricsProvider {
  public:
    explicit Lyrics(netease::Client &client) : m_client(client) {}
    void search(const sdk::Params &query, sdk::Reply done) override {
        const auto text = (query.value("title").toString() + " " + query.value("artist").toString()).trimmed();
        const auto points = text.toUcs4();
        m_client.search(QString::fromUcs4(reinterpret_cast<const char32_t *>(points.constData()), qMin<qsizetype>(points.size(), 64)), {},
                        [done](Result<QVariantMap> result) {
                            if (!result)
                                return done(result.error());
                            QVariantList candidates;
                            for (const auto &track : result.value().value("tracks").toList()) {
                                auto candidate = track.toMap();
                                candidate.insert("score", 0);
                                candidates.append(candidate);
                            }
                            done(sdk::Params{{"candidates", candidates}});
                        });
    }
    void resolve(const sdk::Params &candidate, sdk::Reply done) override {
        const auto cover = candidate.value("cover_url").toString();
        m_client.lyrics(candidate.value("id").toString(), [cover, done](Result<QVariantMap> result) {
            if (result && !cover.isEmpty())
                result.value().insert("cover_url", cover);
            done(result);
        });
    }

  private:
    netease::Client &m_client;
};
} // namespace

class NeteasePlugin final : public QObject, public sdk::IExtensionPlugin {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NEKOTUNE_EXTENSION_IID)
    Q_INTERFACES(nekotune::sdk::IExtensionPlugin)
  public:
    void activate(sdk::IExtensionContext &context, std::function<void(Result<void>)> done) override {
        const auto config = netease::configuration(context.config());
        if (!config) {
            done(config.error());
            return;
        }
        m_context = &context;
        m_transport = std::make_unique<ext::NetworkTransport>();
        m_client = std::make_unique<netease::Client>(*m_transport, [this] {
            const auto current = netease::configuration(m_context->config());
            return current ? current.value() : netease::Config{};
        });
        m_music = std::make_unique<Music>(*m_client);
        m_lyrics = std::make_unique<Lyrics>(*m_client);
        // Remove the earlier adapter's unused server settings when upgrading to the direct plugin.
        if (config.value().toMap() != context.config())
            context.updateConfig(config.value().toMap(), [](Result<void>) {});
        add(context.registerMusicProvider("music", m_music.get(),
                                          {{"name", "网易云 / NetEase"}, {"download", true}, {"page", "music"}}));
        add(context.registerLyricsProvider("lyrics", m_lyrics.get(), {{"name", "网易云 / NetEase"}}));
        service("status", [this](const sdk::Params &, sdk::Reply reply) { reply(status()); });
        service("config.set", [this](const sdk::Params &params, sdk::Reply reply) {
            const auto next = netease::configuration(params);
            if (!next)
                return reply(next.error());
            const auto map = next.value().toMap();
            m_context->updateConfig(map, [reply, map](Result<void> saved) {
                reply(saved ? Result<sdk::Params>(map) : Result<sdk::Params>(saved.error()));
            });
        });
        service("search", [this](const sdk::Params &params, sdk::Reply reply) {
            m_client->search(params.value("query").toString(), params.value("cursor").toString(), reply);
        });
        service("song", [this](const sdk::Params &params, sdk::Reply reply) {
            m_client->track(params.value("id").toString(), [reply](Result<QVariantMap> track) {
                reply(track ? Result<sdk::Params>(sdk::Params{{"tracks", QVariantList{track.value()}}})
                            : Result<sdk::Params>(track.error()));
            });
        });
        service("playlist", [this](const sdk::Params &params, sdk::Reply reply) {
            m_client->playlist(params.value("id").toString(), reply);
        });
        service("lyric", [this](const sdk::Params &params, sdk::Reply reply) {
            const auto dwrc = params.value("dwrc", false), trlrc = params.value("trlrc", false);
            if (dwrc.typeId() != QMetaType::Bool || trlrc.typeId() != QMetaType::Bool)
                return reply(failure("netease_invalid_query"));
            m_client->lyric(params.value("id").toString(), dwrc.toBool(), trlrc.toBool(), reply);
        });
        add(context.on("extension.config_changed", [this](const sdk::Params &) {
            m_client->cancel();
            m_context->publish("status", status());
        }));
        done({});
    }
    void deactivate() override {
        m_registrations.clear();
        if (m_client)
            m_client->close();
        m_lyrics.reset();
        m_music.reset();
        m_client.reset();
        m_transport.reset();
        m_context = nullptr;
    }

  private:
    sdk::Params status() const {
        const auto current = netease::configuration(m_context->config());
        return current ? current.value().toMap() : netease::Config{}.toMap();
    }
    void add(sdk::RegistrationPtr registration) { m_registrations.push_back(std::move(registration)); }
    void service(const QString &id, sdk::Handler handler) { add(m_context->registerService(id, std::move(handler))); }

    sdk::IExtensionContext *m_context = nullptr;
    std::unique_ptr<ext::NetworkTransport> m_transport;
    std::unique_ptr<netease::Client> m_client;
    std::unique_ptr<Music> m_music;
    std::unique_ptr<Lyrics> m_lyrics;
    std::vector<sdk::RegistrationPtr> m_registrations;
};

#include "plugin.moc"
