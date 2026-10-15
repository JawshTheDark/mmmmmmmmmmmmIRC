// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/PreviewService.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

namespace lurker::gui {

namespace {
PreviewDescriptor parseDescriptor(const QJsonObject& o) {
    PreviewDescriptor d;
    d.url = o.value(QStringLiteral("url")).toString();
    d.status = o.value(QStringLiteral("status")).toString();
    d.kind = o.value(QStringLiteral("kind")).toString();
    d.title = o.value(QStringLiteral("title")).toString();
    d.description = o.value(QStringLiteral("description")).toString();
    d.siteName = o.value(QStringLiteral("siteName")).toString();
    d.author = o.value(QStringLiteral("author")).toString();
    d.src = o.value(QStringLiteral("src")).toString();
    d.thumb = o.value(QStringLiteral("thumb")).toString();
    d.embedUrl = o.value(QStringLiteral("embedUrl")).toString();
    d.mime = o.value(QStringLiteral("mime")).toString();
    d.thumbWidth = o.value(QStringLiteral("thumbWidth")).toInt();
    d.thumbHeight = o.value(QStringLiteral("thumbHeight")).toInt();
    return d;
}
}  // namespace

PreviewService::PreviewService(QObject* parent) : QObject(parent), m_net(new QNetworkAccessManager(this)) {}

void PreviewService::configure(const QString& base, const QString& token, bool enabled) {
    QString b = base;
    while (b.endsWith(QLatin1Char('/'))) b.chop(1);
    if (b != m_base) {  // a new instance — drop caches from the old one
        m_descCache.clear();
        m_imgCache.clear();
        m_available = true;
    }
    m_base = b;
    m_token = token;
    m_enabled = enabled;
}

QString PreviewService::absolute(const QString& path) const {
    if (path.isEmpty()) return {};
    if (path.startsWith(QLatin1String("http://")) || path.startsWith(QLatin1String("https://"))) return path;
    return m_base + (path.startsWith(QLatin1Char('/')) ? path : QLatin1Char('/') + path);
}

bool PreviewService::sameOrigin(const QString& absUrl) const {
    // Attach the Bearer only to our own instance — never to a third-party CDN.
    return absUrl.startsWith(m_base + QLatin1Char('/')) || absUrl == m_base;
}

void PreviewService::request(const QString& url, Callback cb) {
    if (!enabled() || url.isEmpty() || !cb) return;
    if (m_testDelayMs > 0) {  // dev: simulate latency so preview ordering can be verified
        QTimer::singleShot(m_testDelayMs, this, [this, url, cb] { requestNow(url, cb); });
        return;
    }
    requestNow(url, cb);
}

void PreviewService::requestNow(const QString& url, Callback cb) {
    const auto cached = m_descCache.constFind(url);
    if (cached != m_descCache.constEnd()) {
        fetchImage(*cached, std::move(cb));
        return;
    }
    // Coalesce concurrent resolves of the same URL.
    if (m_pendingDesc.contains(url)) {
        m_pendingDesc[url].append(std::move(cb));
        return;
    }
    m_pendingDesc[url].append(std::move(cb));

    QNetworkRequest req{QUrl(m_base + QStringLiteral("/api/link-preview/resolve"))};
    req.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
    req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    const QJsonObject body{{QStringLiteral("urls"), QJsonArray{url}}};
    QNetworkReply* reply = m_net->post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        reply->deleteLater();
        const QList<Callback> waiters = m_pendingDesc.take(url);
        const int http = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (http == 404) {  // the instance has no previews feature — stop trying
            m_available = false;
            return;
        }
        if (reply->error() != QNetworkReply::NoError) return;
        const QJsonArray previews =
            QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("previews")).toArray();
        if (previews.isEmpty()) return;
        const PreviewDescriptor d = parseDescriptor(previews.first().toObject());
        m_descCache.insert(url, d);
        for (const Callback& cb : waiters) fetchImage(d, cb);
    });
}

void PreviewService::seedForTest(const PreviewDescriptor& d, const QImage& img) {
    m_available = true;
    m_descCache.insert(d.url, d);
    if (!d.imagePath().isEmpty() && !img.isNull()) m_imgCache.insert(d.imagePath(), img);
}

void PreviewService::fetchImage(const PreviewDescriptor& d, Callback cb) {
    if (!d.ok()) return;  // "unavailable" → no card
    const QString path = d.imagePath();
    if (path.isEmpty()) {  // a card with no image (e.g. a bare link, or a posterless clip)
        cb(d, QImage());
        return;
    }
    const auto cached = m_imgCache.constFind(path);
    if (cached != m_imgCache.constEnd()) {
        cb(d, *cached);
        return;
    }
    if (m_pendingImg.contains(path)) {
        m_pendingImg[path].append([d, cb](const PreviewDescriptor&, const QImage& img) { cb(d, img); });
        return;
    }
    m_pendingImg[path].append([d, cb](const PreviewDescriptor&, const QImage& img) { cb(d, img); });

    const QString abs = absolute(path);
    QNetworkRequest req{QUrl(abs)};
    if (sameOrigin(abs)) req.setRawHeader("Authorization", "Bearer " + m_token.toUtf8());
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, d] {
        reply->deleteLater();
        const QList<Callback> waiters = m_pendingImg.take(path);
        QImage img;
        if (reply->error() == QNetworkReply::NoError) img.loadFromData(reply->readAll());
        if (!img.isNull()) m_imgCache.insert(path, img);
        // Deliver even a null image: the card still renders (text only).
        for (const Callback& w : waiters) w(d, img);
    });
}

}  // namespace lurker::gui
