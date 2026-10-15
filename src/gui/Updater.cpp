// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.

#include "gui/Updater.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMessageBox>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProgressDialog>
#include <QStandardPaths>
#include <QUrl>

namespace lurker::gui {

namespace {
constexpr const char* kRepo = "JawshTheDark/mmmmmmmmmmmmIRC";

#if defined(Q_OS_WIN)
constexpr const char* kAssetSuffix = "windows-x64.zip";
#elif defined(Q_OS_LINUX)
constexpr const char* kAssetSuffix = "x86_64.AppImage";
#else
constexpr const char* kAssetSuffix = "";  // unsupported platform
#endif
}  // namespace

Updater::Updater(QWidget* parent) : QObject(parent), m_parent(parent), m_net(new QNetworkAccessManager(this)) {}

bool Updater::isNewer(const QString& tag, const QString& current) {
    auto parse = [](QString v) {
        v.remove(QLatin1Char('v'));
        QList<int> p;
        for (const QString& s : v.split(QLatin1Char('.'))) p << s.section(QLatin1Char('-'), 0, 0).toInt();
        while (p.size() < 3) p << 0;
        return p;
    };
    const QList<int> a = parse(tag), b = parse(current);
    for (int i = 0; i < 3; ++i)
        if (a[i] != b[i]) return a[i] > b[i];
    return false;
}

void Updater::checkForUpdates(bool silent) {
    if (QString::fromLatin1(kAssetSuffix).isEmpty()) {
        if (!silent) QMessageBox::information(m_parent, tr("Updates"), tr("Automatic updates aren't supported on this platform."));
        return;
    }
    QNetworkRequest req{QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(kRepo))};
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setRawHeader("User-Agent", "mmmmmmmmmmmmirc-updater");
    QNetworkReply* reply = m_net->get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, silent] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (!silent) QMessageBox::warning(m_parent, tr("Updates"), tr("Could not check for updates:\n%1").arg(reply->errorString()));
            return;
        }
        onLatestReceived(reply->readAll(), silent);
    });
}

void Updater::onLatestReceived(const QByteArray& json, bool silent) {
    const QJsonObject o = QJsonDocument::fromJson(json).object();
    const QString tag = o.value(QStringLiteral("tag_name")).toString();
    const QString current = QStringLiteral(APP_VERSION);
    if (tag.isEmpty()) {
        if (!silent) QMessageBox::warning(m_parent, tr("Updates"), tr("No release information was returned."));
        return;
    }
    if (!isNewer(tag, current)) {
        if (!silent)
            QMessageBox::information(m_parent, tr("Updates"),
                                    tr("You're up to date (version %1).").arg(current));
        return;
    }
    // Find this platform's asset.
    QString url, name;
    qint64 size = 0;
    for (const QJsonValue& v : o.value(QStringLiteral("assets")).toArray()) {
        const QJsonObject a = v.toObject();
        const QString an = a.value(QStringLiteral("name")).toString();
        if (an.endsWith(QString::fromLatin1(kAssetSuffix), Qt::CaseInsensitive)) {
            url = a.value(QStringLiteral("browser_download_url")).toString();
            name = an;
            size = a.value(QStringLiteral("size")).toVariant().toLongLong();
            break;
        }
    }
    if (url.isEmpty()) {
        if (!silent) QMessageBox::warning(m_parent, tr("Updates"), tr("%1 has no download for this platform.").arg(tag));
        return;
    }
    const auto choice = QMessageBox::question(
        m_parent, tr("Update available"),
        tr("A new version (%1) is available — you have %2.\n\nDownload (%3 MB), install and restart now?")
            .arg(tag, current)
            .arg(size / 1048576),
        QMessageBox::Yes | QMessageBox::No);
    if (choice == QMessageBox::Yes) downloadAsset(url, name, tag);
}

void Updater::downloadAsset(const QString& url, const QString& assetName, const QString& version) {
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + QStringLiteral("/updates");
    QDir().mkpath(dir);
    const QString outPath = dir + QLatin1Char('/') + assetName;

    auto* progress = new QProgressDialog(tr("Downloading %1...").arg(version), tr("Cancel"), 0, 100, m_parent);
    progress->setWindowModality(Qt::WindowModal);
    progress->setMinimumDuration(0);

    QNetworkRequest req{QUrl(url)};
    req.setRawHeader("User-Agent", "mmmmmmmmmmmmirc-updater");
    req.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    QNetworkReply* reply = m_net->get(req);

    connect(reply, &QNetworkReply::downloadProgress, progress, [progress](qint64 got, qint64 total) {
        if (total > 0) progress->setValue(static_cast<int>(100 * got / total));
    });
    connect(progress, &QProgressDialog::canceled, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::finished, this, [this, reply, outPath, version, progress] {
        progress->deleteLater();
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            if (reply->error() != QNetworkReply::OperationCanceledError)
                QMessageBox::warning(m_parent, tr("Updates"), tr("Download failed:\n%1").arg(reply->errorString()));
            return;
        }
        QFile f(outPath);
        if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            QMessageBox::warning(m_parent, tr("Updates"), tr("Could not save the download to %1").arg(outPath));
            return;
        }
        f.write(reply->readAll());
        f.close();
        applyUpdate(outPath, version);
    });
}

void Updater::applyUpdate(const QString& downloadedPath, const QString& version) {
#if defined(Q_OS_WIN)
    const QString installDir = QCoreApplication::applicationDirPath();
    const QString exeName = QFileInfo(QCoreApplication::applicationFilePath()).fileName();
    const qint64 pid = QCoreApplication::applicationPid();
    // Extract the zip, then a helper .bat waits for us to exit, copies the new
    // files over the install dir, and relaunches.
    const QString staging = QFileInfo(downloadedPath).absolutePath() + QStringLiteral("/extract");
    QDir(staging).removeRecursively();
    QDir().mkpath(staging);
    const int rc = QProcess::execute(
        QStringLiteral("powershell"),
        {QStringLiteral("-NoProfile"), QStringLiteral("-Command"),
         QStringLiteral("Expand-Archive -Force -LiteralPath '%1' -DestinationPath '%2'").arg(downloadedPath, staging)});
    if (rc != 0) {
        QMessageBox::warning(m_parent, tr("Updates"), tr("Could not extract the update."));
        return;
    }
    const QString bat = QFileInfo(downloadedPath).absolutePath() + QStringLiteral("/apply_update.bat");
    QFile f(bat);
    if (f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        QString s;
        s += QStringLiteral("@echo off\r\n");
        s += QStringLiteral(":w\r\n");
        s += QStringLiteral("tasklist /FI \"PID eq %1\" 2>NUL | find \"%1\" >NUL\r\n").arg(pid);
        s += QStringLiteral("if not errorlevel 1 ( timeout /t 1 /nobreak >NUL & goto w )\r\n");
        s += QStringLiteral("xcopy /E /Y /I \"%1\\*\" \"%2\\\" >NUL\r\n").arg(QDir::toNativeSeparators(staging), QDir::toNativeSeparators(installDir));
        s += QStringLiteral("start \"\" \"%1\\%2\"\r\n").arg(QDir::toNativeSeparators(installDir), exeName);
        s += QStringLiteral("del \"%~f0\"\r\n");
        f.write(s.toUtf8());
        f.close();
    }
    QProcess::startDetached(QStringLiteral("cmd.exe"), {QStringLiteral("/c"), QDir::toNativeSeparators(bat)});
    qApp->quit();
#elif defined(Q_OS_LINUX)
    (void)version;
    // The AppImage is a single file. Replace the running one (its path is in
    // $APPIMAGE when launched as an AppImage, else the executable), relaunch.
    QString target = qEnvironmentVariable("APPIMAGE");
    if (target.isEmpty()) target = QCoreApplication::applicationFilePath();
    QFile::remove(target);
    if (!QFile::copy(downloadedPath, target)) {
        QMessageBox::warning(m_parent, tr("Updates"),
                             tr("Downloaded to %1, but couldn't replace %2 — run it manually.").arg(downloadedPath, target));
        return;
    }
    QFile::setPermissions(target, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner |
                                      QFileDevice::ReadGroup | QFileDevice::ExeGroup | QFileDevice::ReadOther |
                                      QFileDevice::ExeOther);
    QProcess::startDetached(target, {});
    qApp->quit();
#else
    (void)version;
    QMessageBox::information(m_parent, tr("Updates"), tr("Downloaded to %1.").arg(downloadedPath));
#endif
}

}  // namespace lurker::gui
