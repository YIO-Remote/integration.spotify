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

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// SPOTIFY FACTORY
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class SpotifyPlugin : public PluginInterface {
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "YIO.PluginInterface" FILE "spotify.json")
    Q_INTERFACES(PluginInterface)

 public:
    SpotifyPlugin() : m_log("spotify") {}
    void create(const QVariantMap& config, QObject* entities, QObject* notifications, QObject* api,
                QObject* configObj) override;
    void setLogEnabled(QtMsgType msgType, bool enable) override { m_log.setEnabled(msgType, enable); }

 private:
    QLoggingCategory m_log;
};

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// SPOTIFY CLASS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class SpotifyBase : public Integration {
    Q_OBJECT

 public:
    explicit SpotifyBase(QLoggingCategory& log, QObject* parent);  // NOLINT we need a non-const reference

    Q_INVOKABLE void setup(const QVariantMap& config, QObject* entities, QObject* notifications, QObject* api,
                           QObject* configObj);
    void             connect();
    void             disconnect();
    void             enterStandby();
    void             leaveStandby();

    // Spotify API authentication
    void refreshAccessToken();

    // Spotify API calls
    Q_INVOKABLE void search(QString query);
    Q_INVOKABLE void search(QString query, QString type);
    Q_INVOKABLE void search(QString query, QString type, QString limit, QString offset);
    void             getAlbum(QString id);
    void             getPlaylist(QString id);
    void             getUserPlaylists();

    // Spotify Connect API calls
    Q_INVOKABLE void getCurrentPlayer();

 signals:
    void requestReady(const QVariantMap& obj, const QString& url);

 public slots:  // NOLINT open issue: https://github.com/cpplint/cpplint/pull/99
    void sendCommand(const QString& type, const QString& entity_id, int command, const QVariant& param);
    void onStandByOn();  // can be removed when enterLeaveStandby is called
    void onStandByOff();

 private:
    void updateEntity(const QString& entity_id, const QVariantMap& attr);

    NotificationsInterface* m_notifications;
    YioAPIInterface*        m_api;
    ConfigInterface*        m_config;
    QLoggingCategory&       m_log;  // controlled by Logger Feature

    bool m_startup = true;

    QString m_entity_id;

    // get and post requests
    void getRequest(const QString& url, const QString& params);
    void postRequest(const QString& url, const QString& params);
    void putRequest(const QString& url, const QString& params);

    // polling timer
    QTimer* m_polling_timer;

    // Spotify auth stuff
    QString m_client_id;
    QString m_client_secret;
    QString m_access_token;
    QString m_refresh_token;
    int     m_token_expire;  // in seconds
    QTimer* m_tokenTimeOutTimer;

    QString m_apiURL = "https://api.spotify.com";

 private slots:  // NOLINT open issue: https://github.com/cpplint/cpplint/pull/99
    void onTokenTimeOut();
    void onPollingTimerTimeout();
};
