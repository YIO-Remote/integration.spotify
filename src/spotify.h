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

#include <QLoggingCategory>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QTimer>
#include <QVariant>
#include <QtConcurrent/QtConcurrentRun>

#include "yio-interface/configinterface.h"
#include "yio-interface/entities/entitiesinterface.h"
#include "yio-interface/entities/entityinterface.h"
#include "yio-interface/notificationsinterface.h"
#include "yio-interface/plugininterface.h"
#include "yio-interface/yioapiinterface.h"
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

    Q_INVOKABLE void connect() override;
    Q_INVOKABLE void disconnect() override;
    Q_INVOKABLE void enterStandby() override;
    Q_INVOKABLE void leaveStandby() override;
    Q_INVOKABLE void sendCommand(const QString& type, const QString& entity_id, int command,
                                 const QVariant& param) override;

 signals:
    void requestReady(const QVariantMap& obj, const QString& url);

    // TODO(marton) are these slots going to be used for anything or can they be removed?
 public slots:           // NOLINT open issue: https://github.com/cpplint/cpplint/pull/99
    void onStandByOn();  // can be removed when enterLeaveStandby is called
    void onStandByOff();

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
    void putRequest(const QString& url, const QString& params);

 private slots:  // NOLINT open issue: https://github.com/cpplint/cpplint/pull/99
    void onTokenTimeOut();
    void onPollingTimerTimeout();

 private:
    bool    m_startup = true;
    QString m_entityId;

    // polling timer
    QTimer* m_pollingTimer;

    // Spotify auth stuff
    QString m_clientId;
    QString m_clientSecret;
    QString m_accessToken;
    QString m_refreshToken;
    int     m_tokenExpire;  // in seconds
    QTimer* m_tokenTimeOutTimer;
    QString m_apiURL = "https://api.spotify.com";
};
