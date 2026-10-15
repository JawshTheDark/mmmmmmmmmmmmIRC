// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this
// file, You can obtain one at http://mozilla.org/MPL/2.0/.
//
// In-app updater: checks the GitHub "latest" release, and if it's newer than the
// running build, downloads this platform's asset (Windows zip / Linux AppImage),
// installs it over the current install, relaunches, and quits. The user confirms
// before anything is downloaded or run.
#pragma once

#include <QObject>
#include <QString>

class QWidget;
class QNetworkAccessManager;

namespace lurker::gui {

class Updater : public QObject {
    Q_OBJECT
public:
    explicit Updater(QWidget* parent);

    // `silent` suppresses the "you're up to date" / error popups (for auto-check
    // on startup); an available update always prompts.
    void checkForUpdates(bool silent);

private:
    void onLatestReceived(const QByteArray& json, bool silent);
    void downloadAsset(const QString& url, const QString& assetName, const QString& version);
    void applyUpdate(const QString& downloadedPath, const QString& version);
    static bool isNewer(const QString& tag, const QString& current);

    QWidget* m_parent = nullptr;
    QNetworkAccessManager* m_net = nullptr;
};

}  // namespace lurker::gui
