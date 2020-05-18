/******************************************************************************
 *
 * Copyright (C) 2019 Marton Borzak <hello@martonborzak.com>
 *
 * This file is part of the YIO-Remote software project.
 *
 * YIO-Remote software is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * YIO-Remote software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with YIO-Remote software. If not, see <https://www.gnu.org/licenses/>.
 *
 * SPDX-License-Identifier: GPL-3.0-or-later
 *****************************************************************************/

#pragma once

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

#include "yio-interface/entities/mediaplayerinterface.h"
#include "yio-model/mediaplayer/albummodel_mediaplayer.h"
#include "yio-model/mediaplayer/searchmodel_mediaplayer.h"
#include "yio-plugin/integration.h"
#include "yio-plugin/plugin.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// SPOTIFY FACTORY
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

const bool USE_WORKER_THREAD = false;

class SpotifyPlugin : public Plugin {
    Q_OBJECT
    Q_INTERFACES(PluginInterface)
    Q_PLUGIN_METADATA(IID "YIO.PluginInterface" FILE "spotify.json")

 public:
    SpotifyPlugin();

    // Plugin interface
 protected:
    Integration* createIntegration(const QVariantMap& config, EntitiesInterface* entities,
                                   NotificationsInterface* notifications, YioAPIInterface* api,
                                   ConfigInterface* configObj) override;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// SPOTIFY CLASS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class Spotify : public Integration {
    Q_OBJECT

 public:
    explicit Spotify(const QVariantMap& config, EntitiesInterface* entities, NotificationsInterface* notifications,
                     YioAPIInterface* api, ConfigInterface* configObj, Plugin* plugin);

    void sendCommand(const QString& type, const QString& entitId, int command, const QVariant& param) override;

 public slots:  // NOLINT open issue: https://github.com/cpplint/cpplint/pull/99
    void connect() override;
    void disconnect() override;
    void enterStandby() override;
    void leaveStandby() override;

 signals:
    void requestReady(const QVariantMap& obj, const QString& url);

 private:
    // Spotify API calls
    void search(QString query);
    void search(QString query, QString type);
    void search(QString query, QString type, QString limit, QString offset);
    void getAlbum(QString id);
    void getPlaylist(QString id);
    void getUserPlaylists();

    // Spotify API authentication
    void refreshAccessToken();

    // Spotify Connect API calls
    void getCurrentPlayer();

    void updateEntity(const QString& entity_id, const QVariantMap& attr);

    // get and post requests
    void getRequest(const QString& url, const QString& params);
    void postRequest(const QString& url, const QString& params);
    void putRequest(const QString& url, const QString& params);  // TODO(marton): change param to QUrlQuery
                                                                 // QUrlQuery query;

    //    query.addQueryItem("username", "test");
    //    query.addQueryItem("password", "test");

    //    url.setQuery(query.query());

 private slots:  // NOLINT open issue: https://github.com/cpplint/cpplint/pull/99
    void onTokenTimeOut();
    void onPollingTimerTimeout();
    void onProgressBarTimerTimeout();

 private:
    bool    m_startup = true;
    QString m_entityId;

    // polling timer
    QTimer* m_pollingTimer;
    QTimer* m_progressBarTimer;

    int m_progressBarPosition = 0;

    // Spotify auth stuff
    QString m_clientId;
    QString m_clientSecret;
    QString m_accessToken;
    QString m_refreshToken;
    int     m_tokenExpire;  // in seconds
    QTimer* m_tokenTimeOutTimer;
    QString m_apiURL = "https://api.spotify.com";
};
