// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// Link/media previews through Lurker's previews proxy (the `lurker-previews`
// decoder, gated by the server's `linkPreviews` feature). We POST the URLs in a
// message to /api/link-preview/resolve and get back descriptors; image bytes and
// video posters come from /api/link-preview/{media,poster}/:token. Nothing is
// fetched from the origin site directly — that is the whole point of the proxy
// (the reader's IP never reaches a stranger's server for an unsolicited card).
// Per the server's media policy, video/audio bytes are deliberately NOT relayed,
// so those render as a poster card that opens the source on click.
#pragma once

#include <QByteArray>
#include <QHash>
#include <QImage>
#include <QObject>
#include <QString>
#include <functional>

class QNetworkAccessManager;

namespace lurker::gui {

// One resolved preview (mirrors the server's PreviewDescriptor JSON).
struct PreviewDescriptor {
    QString url;          // the original link
    QString status;       // "ok" | "unavailable"
    QString kind;         // image | video | audio | page | video-embed
    QString title;
    QString description;
    QString siteName;
    QString author;
    QString src;          // image bytes URL (kind=image), proxy-relative or absolute
    QString thumb;        // card/poster thumbnail URL (page/video/audio)
    QString embedUrl;     // iframe URL (video-embed)
    QString mime;
    int thumbWidth = 0;
    int thumbHeight = 0;
    bool ok() const { return status == QLatin1String("ok"); }
    // The image to display for this descriptor: the content for an image, the
    // poster/thumbnail otherwise. Empty when there is nothing to show.
    QString imagePath() const { return kind == QLatin1String("image") ? src : thumb; }
};

class PreviewService : public QObject {
    Q_OBJECT
public:
    explicit PreviewService(QObject* parent = nullptr);

    // Point at the connected Lurker instance. `base` is the REST origin
    // (https://host); `token` is the Bearer. `enabled` reflects the user option.
    void configure(const QString& base, const QString& token, bool enabled);
    void setEnabled(bool on) { m_enabled = on; }
    bool enabled() const { return m_enabled && !m_base.isEmpty() && m_available; }

    using Callback = std::function<void(const PreviewDescriptor&, const QImage&)>;
    // Resolve `url` (cached) then fetch its image (cached), invoking `cb` once on
    // the GUI thread. `cb` may run synchronously if everything is cached. If the
    // instance has no previews feature, the first resolve 404s and the service
    // disables itself; `cb` is not called in that case.
    void request(const QString& url, Callback cb);

    // Dev/demo only: pre-seed the caches so `request` resolves synchronously
    // without the network (used by --previewdemo to screenshot the rendering).
    void seedForTest(const PreviewDescriptor& d, const QImage& img);
    // Dev/demo only: delay every resolution by `ms`, to simulate network latency
    // and verify a preview still lands under its own message (not the backlog end).
    void setTestDelay(int ms) { m_testDelayMs = ms; }

private:
    void requestNow(const QString& url, Callback cb);  // request() without the dev delay
    QString absolute(const QString& path) const;  // proxy-relative → base+path
    bool sameOrigin(const QString& absUrl) const;  // may we attach the Bearer?
    void fetchImage(const PreviewDescriptor& d, Callback cb);

    QNetworkAccessManager* m_net = nullptr;
    QString m_base;                      // https://host (no trailing slash)
    QString m_token;
    bool m_enabled = true;               // user option
    bool m_available = true;             // cleared on a 404 (feature off)
    int m_testDelayMs = 0;               // dev: simulate latency (see setTestDelay)
    QHash<QString, PreviewDescriptor> m_descCache;  // url -> descriptor
    QHash<QString, QImage> m_imgCache;              // image path -> pixels
    QHash<QString, QList<Callback>> m_pendingDesc;  // url -> waiters (coalesce)
    QHash<QString, QList<Callback>> m_pendingImg;   // image path -> waiters
};

}  // namespace lurker::gui
