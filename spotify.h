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

#ifndef SPOTIFY_H
#define SPOTIFY_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QtConcurrent/QtConcurrentRun>

#include "../remote-software/sources/integrations/integration.h"
#include "../remote-software/sources/integrations/integrationinterface.h"
#include "../remote-software/sources/entities/entitiesinterface.h"
#include "../remote-software/sources/entities/entityinterface.h"
#include "../remote-software/sources/notificationsinterface.h"
#include "../remote-software/sources/yioapiinterface.h"
#include "../remote-software/sources/configinterface.h"
#include "../remote-software/components/media_player/sources/searchmodel_mediaplayer.h"
#include "../remote-software/components/media_player/sources/albummodel_mediaplayer.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// SPOTIFY FACTORY
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Spotify : public IntegrationInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "YIO.IntegrationInterface" FILE "spotify.json")
    Q_INTERFACES(IntegrationInterface)

public:
    explicit Spotify() {}
    void create                         (const QVariantMap& config, QObject *entities, QObject *notifications, QObject* api, QObject *configObj) override;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// SPOTIFY CLASS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class SpotifyBase : public Integration
{
    Q_OBJECT

public:
    explicit SpotifyBase(QObject *parent);

    Q_INVOKABLE void setup              (const QVariantMap& config, QObject *entities, QObject *notifications, QObject* api, QObject *configObj);
    Q_INVOKABLE void connect            ();
    Q_INVOKABLE void disconnect         ();

    // Spotify API authentication
    void             refreshAccessToken ();

    // Spotify API calls
    Q_INVOKABLE void search             (QString query);
    Q_INVOKABLE void search             (QString query, QString type);
    Q_INVOKABLE void search             (QString query, QString type, QString limit, QString offset);
    void getAlbum                       (QString id);
    void getPlaylist                    (QString id);
    void getUserPlaylists               ();

    // Spotify Connect API calls
    Q_INVOKABLE void getCurrentPlayer   ();

signals:
    void requestReady(const QVariantMap& obj, const QString& url);

public slots:
    void sendCommand                    (const QString& type, const QString& entity_id, const QString& command, const QVariant& param);
    void onStandByOn                    ();
    void onStandByOff                   ();

private:
    void updateEntity                   (const QString& entity_id, const QVariantMap& attr);

    EntitiesInterface*                  m_entities;
    NotificationsInterface*             m_notifications;
    YioAPIInterface*                    m_api;
    ConfigInterface*                    m_config;

    bool                                m_startup = true;

    QString                             m_entity_id;

    // get and post requests
    void getRequest                     (const QString& url, const QString& params);
    void postRequest                    (const QString& url, const QString& params);
    void putRequest                     (const QString& url, const QString& params);

    // polling timer
    QTimer*                             m_polling_timer;

    // Spotify auth stuff
    QString                             m_client_id;
    QString                             m_client_secret;
    QString                             m_access_token;
    QString                             m_refresh_token;
    int                                 m_token_expire; // in seconds
    QTimer*                             m_tokenTimeOutTimer;

    QString                             m_apiURL = "https://api.spotify.com";

private slots:
    void                                onTokenTimeOut();
    void                                onPollingTimerTimeout();
};

#endif // SPOTIFY_H
