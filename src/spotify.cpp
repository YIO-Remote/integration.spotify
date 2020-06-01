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

#include "spotify.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

SpotifyPlugin::SpotifyPlugin() : Plugin("spotify", USE_WORKER_THREAD) {}

Integration* SpotifyPlugin::createIntegration(const QVariantMap& config, EntitiesInterface* entities,
                                              NotificationsInterface* notifications, YioAPIInterface* api,
                                              ConfigInterface* configObj) {
    qCInfo(m_logCategory) << "Creating Spotify integration plugin" << PLUGIN_VERSION;

    return new Spotify(config, entities, notifications, api, configObj, this);
}

Spotify::Spotify(const QVariantMap& config, EntitiesInterface* entities, NotificationsInterface* notifications,
                 YioAPIInterface* api, ConfigInterface* configObj, Plugin* plugin)
    : Integration(config, entities, notifications, api, configObj, plugin) {
    for (QVariantMap::const_iterator iter = config.begin(); iter != config.end(); ++iter) {
        if (iter.key() == Integration::OBJ_DATA) {
            QVariantMap map = iter.value().toMap();
            m_clientId      = map.value("client_id").toString();
            m_clientSecret  = map.value("client_secret").toString();
            //            m_accessToken   = map.value("access_token").toString();
            m_refreshToken = map.value("refresh_token").toString();
            m_entityId     = map.value("entity_id").toString();
        }
    }

    m_pollingTimer = new QTimer(this);
    m_pollingTimer->setInterval(4000);
    QObject::connect(m_pollingTimer, &QTimer::timeout, this, &Spotify::onPollingTimerTimeout);

    m_progressBarTimer = new QTimer(this);
    m_progressBarTimer->setInterval(1000);
    QObject::connect(m_progressBarTimer, &QTimer::timeout, this, &Spotify::onProgressBarTimerTimeout);

    // add available entity
    QStringList supportedFeatures;
    supportedFeatures << "SOURCE"
                      << "APP_NAME"
                      << "VOLUME"
                      << "VOLUME_UP"
                      << "VOLUME_DOWN"
                      << "VOLUME_SET"
                      << "MUTE"
                      << "MUTE_SET"
                      << "MEDIA_TYPE"
                      << "MEDIA_TITLE"
                      << "MEDIA_ARTIST"
                      << "MEDIA_ALBUM"
                      << "MEDIA_DURATION"
                      << "MEDIA_POSITION"
                      << "MEDIA_IMAGE"
                      << "PLAY"
                      << "PAUSE"
                      << "STOP"
                      << "PREVIOUS"
                      << "NEXT"
                      << "SEEK"
                      << "SHUFFLE"
                      << "SEARCH"
                      << "SPEAKER_CONTROL"
                      << "LIST";
    addAvailableEntity(m_entityId, "media_player", integrationId(), friendlyName(), supportedFeatures);
}

void Spotify::connect() {
    setState(CONNECTED);

    // get a new access token
    refreshAccessToken();

    qCDebug(m_logCategory) << "STARTING SPOTIFY";
}

void Spotify::disconnect() {
    setState(DISCONNECTED);
    m_pollingTimer->stop();
    m_progressBarTimer->stop();
}

void Spotify::enterStandby() { disconnect(); }

void Spotify::leaveStandby() { connect(); }

void Spotify::refreshAccessToken() {
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QNetworkRequest        request;

    QObject* context = new QObject(this);

    QObject::connect(manager, &QNetworkAccessManager::finished, context, [=](QNetworkReply* reply) {
        if (reply->error()) {
            qCWarning(m_logCategory) << "Refresh token:" << reply->errorString();
            //            qCWarning(m_logCategory) << reply->readAll();
        }

        QString answer = reply->readAll();

        // convert to json
        QJsonParseError parseerror;
        QJsonDocument   doc = QJsonDocument::fromJson(answer.toUtf8(), &parseerror);
        if (parseerror.error != QJsonParseError::NoError) {
            qCWarning(m_logCategory) << "JSON error : " << parseerror.errorString();
            return;
        }
        QVariantMap map = doc.toVariant().toMap();

        // store the refresh and acccess tokens
        if (map.contains("access_token")) {
            if (m_accessToken != map.value("access_token").toString()) {
                qCDebug(m_logCategory) << "Got new access token";
            }
            m_accessToken = map.value("access_token").toString();
        }

        if (map.contains("expires_in")) {
            m_tokenExpire = map.value("expires_in").toInt();
        }

        if (map.contains("refresh_token")) {
            m_refreshToken = map.value("refresh_token").toString();
        }

        // start timer with the expire date on the access token
        m_tokenTimeOutTimer = new QTimer(this);
        m_tokenTimeOutTimer->setSingleShot(true);

        // connect the token timeout timer to the function that handles the timeout
        QObject::connect(m_tokenTimeOutTimer, &QTimer::timeout, this, &Spotify::onTokenTimeOut);

        // get the token 60 seconds before expiry
        qCDebug(m_logCategory) << "Token expiry time:" << m_tokenExpire;
        if (m_tokenExpire >= 60) {
            m_tokenExpire = m_tokenExpire - 60;
        }
        if (m_tokenExpire > 0) {
            m_tokenTimeOutTimer->start(m_tokenExpire * 1000);

            // start polling
            m_pollingTimer->start();
        }

        reply->deleteLater();
        context->deleteLater();
        manager->deleteLater();
    });

    QObject::connect(
        manager, &QNetworkAccessManager::networkAccessibleChanged, context,
        [=](QNetworkAccessManager::NetworkAccessibility accessibility) { qCDebug(m_logCategory) << accessibility; });

    QByteArray postData;
    postData.append("grant_type=refresh_token&");
    postData.append("refresh_token=");
    postData.append(m_refreshToken);

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QString header_auth;
    header_auth.append(m_clientId).append(":").append(m_clientSecret);

    request.setRawHeader("Authorization", "Basic " + header_auth.toUtf8().toBase64());
    request.setUrl(QUrl("https://accounts.spotify.com/api/token"));

    manager->post(request, postData);
}

void Spotify::search(QString query) { search(query, "album,artist,playlist,track", "20", "0"); }

void Spotify::search(QString query, QString type) { search(query, type, "20", "0"); }

void Spotify::search(QString query, QString type, QString limit, QString offset) {
    QString url = "/v1/search";

    query.replace(" ", "%20");

    QObject* context = new QObject(this);

    QObject::connect(this, &Spotify::requestReady, context, [=](const QVariantMap& map, const QString& rUrl) {
        if (rUrl == url) {
            // get the albums
            SearchModelList* albums = new SearchModelList();

            if (map.contains("albums")) {
                QVariantList map_albums = map.value("albums").toMap().value("items").toList();

                QStringList commands = {"PLAY", "ARTISTRADIO"};

                for (int i = 0; i < map_albums.length(); i++) {
                    QString id    = map_albums[i].toMap().value("id").toString();
                    QString title = map_albums[i].toMap().value("name").toString();
                    QString subtitle =
                        map_albums[i].toMap().value("artists").toList()[0].toMap().value("name").toString();
                    QString image = "";
                    if (map_albums[i].toMap().contains("images") &&
                        map_albums[i].toMap().value("images").toList().length() > 0) {
                        QVariantList images = map_albums[i].toMap().value("images").toList();
                        for (int k = 0; k < images.length(); k++) {
                            if (images[k].toMap().value("width").toInt() == 300) {
                                image = images[k].toMap().value("url").toString();
                            }
                        }
                        if (image == "") {
                            image = map_albums[i].toMap().value("images").toList()[0].toMap().value("url").toString();
                        }
                    }

                    SearchModelListItem item = SearchModelListItem(id, "album", title, subtitle, image, QVariant());
                    albums->append(item);
                }
            }

            // get the tracks
            SearchModelList* tracks = new SearchModelList();

            if (map.contains("tracks")) {
                QVariantList map_tracks = map.value("tracks").toMap().value("items").toList();

                QStringList commands = {"PLAY", "SONGRADIO"};

                for (int i = 0; i < map_tracks.length(); i++) {
                    QString id       = map_tracks[i].toMap().value("id").toString();
                    QString title    = map_tracks[i].toMap().value("name").toString();
                    QString subtitle = map_tracks[i].toMap().value("album").toMap().value("name").toString();
                    QString image    = "";
                    if (map_tracks[i].toMap().value("album").toMap().contains("images") &&
                        map_tracks[i].toMap().value("album").toMap().value("images").toList().length() > 0) {
                        QVariantList images = map_tracks[i].toMap().value("album").toMap().value("images").toList();
                        for (int k = 0; k < images.length(); k++) {
                            if (images[k].toMap().value("width").toInt() == 64) {
                                image = images[k].toMap().value("url").toString();
                            }
                        }
                        if (image == "") {
                            image = map_tracks[i]
                                        .toMap()
                                        .value("album")
                                        .toMap()
                                        .value("images")
                                        .toList()[0]
                                        .toMap()
                                        .value("url")
                                        .toString();
                        }
                    }

                    SearchModelListItem item = SearchModelListItem(id, "track", title, subtitle, image, commands);
                    tracks->append(item);
                }
            }

            // get the artists
            SearchModelList* artists = new SearchModelList();

            if (map.contains("artists")) {
                QVariantList map_artists = map.value("artists").toMap().value("items").toList();

                QStringList commands = {"ARTISTRADIO"};

                for (int i = 0; i < map_artists.length(); i++) {
                    QString id       = map_artists[i].toMap().value("id").toString();
                    QString title    = map_artists[i].toMap().value("name").toString();
                    QString subtitle = "";
                    QString image    = "";
                    if (map_artists[i].toMap().contains("images") &&
                        map_artists[i].toMap().value("images").toList().length() > 0) {
                        QVariantList images = map_artists[i].toMap().value("images").toList();
                        for (int k = 0; k < images.length(); k++) {
                            if (images[k].toMap().value("width").toInt() == 64) {
                                image = images[k].toMap().value("url").toString();
                            }
                        }
                        if (image == "") {
                            image = map_artists[i].toMap().value("images").toList()[0].toMap().value("url").toString();
                        }
                    }

                    SearchModelListItem item = SearchModelListItem(id, "artist", title, subtitle, image, commands);
                    artists->append(item);
                }
            }

            // get the playlists
            SearchModelList* playlists = new SearchModelList();

            if (map.contains("playlists")) {
                QVariantList map_playlists = map.value("playlists").toMap().value("items").toList();

                QStringList commands = {"PLAY", "PLAYLISTRADIO"};

                for (int i = 0; i < map_playlists.length(); i++) {
                    QString id       = map_playlists[i].toMap().value("id").toString();
                    QString title    = map_playlists[i].toMap().value("name").toString();
                    QString subtitle = map_playlists[i].toMap().value("owner").toMap().value("display_name").toString();
                    QString image    = "";
                    if (map_playlists[i].toMap().contains("images") &&
                        map_playlists[i].toMap().value("images").toList().length() > 0) {
                        QVariantList images = map_playlists[i].toMap().value("images").toList();
                        for (int k = 0; k < images.length(); k++) {
                            if (images[k].toMap().value("width").toInt() == 300) {
                                image = images[k].toMap().value("url").toString();
                            }
                        }
                        if (image == "") {
                            image =
                                map_playlists[i].toMap().value("images").toList()[0].toMap().value("url").toString();
                        }
                    }

                    SearchModelListItem item = SearchModelListItem(id, "playlist", title, subtitle, image, commands);
                    playlists->append(item);
                }
            }

            SearchModelItem* ialbums    = new SearchModelItem("albums", albums);
            SearchModelItem* itracks    = new SearchModelItem("tracks", tracks);
            SearchModelItem* iartists   = new SearchModelItem("artists", artists);
            SearchModelItem* iplaylists = new SearchModelItem("playlists", playlists);

            SearchModel* m_model = new SearchModel();

            m_model->append(ialbums);
            m_model->append(itracks);
            m_model->append(iartists);
            m_model->append(iplaylists);

            // update the entity
            EntityInterface* entity = static_cast<EntityInterface*>(m_entities->getEntityInterface(m_entityId));
            if (entity) {
                MediaPlayerInterface* me = static_cast<MediaPlayerInterface*>(entity->getSpecificInterface());
                me->setSearchModel(m_model);
            }
        }
        context->deleteLater();
    });

    getRequest(url, "?q=" + query + "&type=" + type + "&limit=" + limit + "&offset=" + offset);
}

void Spotify::getAlbum(QString id) {
    QString url = "/v1/albums/";

    QObject* context = new QObject(this);

    QObject::connect(this, &Spotify::requestReady, context, [=](const QVariantMap& map, const QString& rUrl) {
        if (rUrl == url) {
            qCDebug(m_logCategory) << "GET ALBUM";
            QString id       = map.value("id").toString();
            QString title    = map.value("name").toString();
            QString subtitle = map.value("artists").toList()[0].toMap().value("name").toString();
            QString type     = "album";
            QString image    = "";
            if (map.contains("images") && map.value("images").toList().length() > 0) {
                QVariantList images = map.value("images").toList();
                for (int k = 0; k < images.length(); k++) {
                    if (images[k].toMap().value("width").toInt() == 300) {
                        image = images[k].toMap().value("url").toString();
                    }
                }
                if (image == "") {
                    image = map.value("images").toList()[0].toMap().value("url").toString();
                }
            }

            QStringList commands = {"PLAY", "SONGRADIO"};

            BrowseModel* album = new BrowseModel(nullptr, id, title, subtitle, type, image, commands);

            // add tracks to album
            QVariantList tracks = map.value("tracks").toMap().value("items").toList();
            for (int i = 0; i < tracks.length(); i++) {
                album->addItem(tracks[i].toMap().value("id").toString(), tracks[i].toMap().value("name").toString(),
                               tracks[i].toMap().value("artists").toList()[0].toMap().value("name").toString(), "track",
                               "", commands);
            }

            // update the entity
            EntityInterface* entity = static_cast<EntityInterface*>(m_entities->getEntityInterface(m_entityId));
            if (entity) {
                MediaPlayerInterface* me = static_cast<MediaPlayerInterface*>(entity->getSpecificInterface());
                me->setBrowseModel(album);
            }
        }
        context->deleteLater();
    });
    getRequest(url, id);
}

void Spotify::getPlaylist(QString id) {
    QString url = "/v1/playlists/";

    QObject* context = new QObject(this);

    QObject::connect(this, &Spotify::requestReady, context, [=](const QVariantMap& map, const QString& rUrl) {
        if (rUrl == url) {
            qCDebug(m_logCategory) << "GET PLAYLIST";
            QString id       = map.value("id").toString();
            QString title    = map.value("name").toString();
            QString subtitle = map.value("owner").toMap().value("display_name").toString();
            QString type     = "playlist";
            QString image    = "";
            if (map.contains("images") && map.value("images").toList().length() > 0) {
                QVariantList images = map.value("images").toList();
                for (int k = 0; k < images.length(); k++) {
                    if (images[k].toMap().value("width").toInt() == 300) {
                        image = images[k].toMap().value("url").toString();
                    }
                }
                if (image == "") {
                    image = map.value("images").toList()[0].toMap().value("url").toString();
                }
            }

            QStringList commands = {"PLAY", "SONGRADIO"};

            BrowseModel* album = new BrowseModel(nullptr, id, title, subtitle, type, image, commands);

            // add tracks to playlist
            QVariantList tracks = map.value("tracks").toMap().value("items").toList();
            for (int i = 0; i < tracks.length(); i++) {
                album->addItem(tracks[i].toMap().value("track").toMap().value("id").toString(),
                               tracks[i].toMap().value("track").toMap().value("name").toString(),
                               tracks[i]
                                   .toMap()
                                   .value("track")
                                   .toMap()
                                   .value("artists")
                                   .toList()[0]
                                   .toMap()
                                   .value("name")
                                   .toString(),
                               "track", "", commands);
            }

            // update the entity
            EntityInterface* entity = static_cast<EntityInterface*>(m_entities->getEntityInterface(m_entityId));
            if (entity) {
                MediaPlayerInterface* me = static_cast<MediaPlayerInterface*>(entity->getSpecificInterface());
                me->setBrowseModel(album);
            }
        }
        context->deleteLater();
    });
    getRequest(url, id);
}

void Spotify::getUserPlaylists() {
    QString url = "/v1/me/playlists/";

    QObject* context = new QObject(this);

    QObject::connect(this, &Spotify::requestReady, context, [=](const QVariantMap& map, const QString& rUrl) {
        if (rUrl == url) {
            qCDebug(m_logCategory) << "GET USERS PLAYLIST";
            QString     id       = "";
            QString     title    = "";
            QString     subtitle = "";
            QString     type     = "playlist";
            QString     image    = "";
            QStringList commands = {};

            BrowseModel* album = new BrowseModel(nullptr, id, title, subtitle, type, image, commands);

            // add playlists to model
            QVariantList playlists = map.value("items").toList();

            for (int i = 0; i < playlists.length(); i++) {
                if (playlists[i].toMap().contains("images") &&
                    playlists[i].toMap().value("images").toList().length() > 0) {
                    image               = "";
                    QVariantList images = playlists[i].toMap().value("images").toList();
                    for (int k = 0; k < images.length(); k++) {
                        if (images[k].toMap().value("width").toInt() == 300) {
                            image = images[k].toMap().value("url").toString();
                        }
                    }
                    if (image == "") {
                        image = playlists[i].toMap().value("images").toList()[0].toMap().value("url").toString();
                    }
                }

                QStringList commands = {"PLAY", "PLAYLISTRADIO"};
                album->addItem(playlists[i].toMap().value("id").toString(),
                               playlists[i].toMap().value("name").toString(), "", type, image, commands);
            }

            // update the entity
            EntityInterface* entity = static_cast<EntityInterface*>(m_entities->getEntityInterface(m_entityId));
            if (entity) {
                MediaPlayerInterface* me = static_cast<MediaPlayerInterface*>(entity->getSpecificInterface());
                me->setBrowseModel(album);
            }
        }
        context->deleteLater();
    });
    getRequest(url, "");
}

void Spotify::getCurrentPlayer() {
    QString url = "/v1/me/player";

    QObject* context = new QObject(this);

    QObject::connect(this, &Spotify::requestReady, context, [=](const QVariantMap& map, const QString& rUrl) {
        if (rUrl == url) {
            EntityInterface* entity = static_cast<EntityInterface*>(m_entities->getEntityInterface(m_entityId));
            if (entity) {
                if (map.contains("item")) {
                    // get the image
                    //                attr.insert("image",
                    //                map.value("item").toMap().value("album").toMap().value("images").toList()[0].toMap().value("url").toString());
                    // get the image
                    entity->updateAttrByIndex(MediaPlayerDef::MEDIAIMAGE, map.value("item")
                                                                              .toMap()
                                                                              .value("album")
                                                                              .toMap()
                                                                              .value("images")
                                                                              .toList()[0]
                                                                              .toMap()
                                                                              .value("url")
                                                                              .toString());

                    // get the device
                    entity->updateAttrByIndex(MediaPlayerDef::SOURCE,
                                              map.value("device").toMap().value("name").toString());

                    // get the volume
                    entity->updateAttrByIndex(MediaPlayerDef::VOLUME,
                                              map.value("device").toMap().value("volume_percent").toInt());

                    // get the track title
                    entity->updateAttrByIndex(MediaPlayerDef::MEDIATITLE,
                                              map.value("item").toMap().value("name").toString());

                    // get the artist
                    entity->updateAttrByIndex(MediaPlayerDef::MEDIAARTIST,
                                              map.value("item").toMap().value("name").toString());

                    // get the state
                    if (map.value("is_playing").toBool()) {
                        entity->updateAttrByIndex(MediaPlayerDef::STATE, MediaPlayerDef::PLAYING);
                        m_progressBarTimer->start();
                    } else {
                        entity->updateAttrByIndex(MediaPlayerDef::STATE, MediaPlayerDef::IDLE);
                        m_progressBarTimer->stop();
                    }

                    // update progress
                    entity->updateAttrByIndex(
                        MediaPlayerDef::MEDIADURATION,
                        static_cast<int>(map.value("item").toMap().value("duration_ms").toInt() / 1000));
                    //                    entity->updateAttrByIndex(MediaPlayerDef::MEDIAPROGRESS,
                    //                                              static_cast<int>(map.value("progress_ms").toInt() /
                    //                                              1000));
                    m_progressBarPosition = map.value("progress_ms").toInt() / 1000;

                } else {
                    entity->updateAttrByIndex(MediaPlayerDef::MEDIAIMAGE, "");
                    entity->updateAttrByIndex(MediaPlayerDef::SOURCE, "");
                    entity->updateAttrByIndex(MediaPlayerDef::MEDIATITLE, "");
                    entity->updateAttrByIndex(MediaPlayerDef::MEDIAARTIST, "");
                    entity->updateAttrByIndex(MediaPlayerDef::MEDIADURATION, 0);
                    entity->updateAttrByIndex(MediaPlayerDef::MEDIAPROGRESS, 0);
                    entity->updateAttrByIndex(MediaPlayerDef::STATE, MediaPlayerDef::OFF);
                }
            }
        }
        context->deleteLater();
    });

    getRequest(url, "");
}

void Spotify::sendCommand(const QString& type, const QString& entityId, int command, const QVariant& param) {
    if (!(type == "media_player" && entityId == m_entityId)) {
        return;
    }

    if (command == MediaPlayerDef::C_PLAY) {
        putRequest("/v1/me/player/play", "");  // normal play without browsing
    } else if (command == MediaPlayerDef::C_PLAY_ITEM) {
        if (param == "") {
            putRequest("/v1/me/player/play", "");
        } else {
            if (param.toMap().contains("type")) {
                if (param.toMap().value("type").toString() == "track") {
                    QString  url     = "/v1/tracks/";
                    QObject* context = new QObject(this);
                    QObject::connect(this, &Spotify::requestReady, context,
                                     [=](const QVariantMap& map, const QString& rUrl) {
                                         if (rUrl == url) {
                                             qCDebug(m_logCategory) << "PLAY MEDIA" << map.value("uri").toString();
                                             QVariantMap rMap;
                                             QStringList rList;
                                             rList.append(map.value("uri").toString());
                                             rMap.insert("uris", rList);
                                             QJsonDocument doc     = QJsonDocument::fromVariant(rMap);
                                             QString       message = doc.toJson(QJsonDocument::JsonFormat::Compact);
                                             qCDebug(m_logCategory) << message;
                                             putRequest("/v1/me/player/play", message);
                                         }
                                         context->deleteLater();
                                     });
                    getRequest(url, param.toMap().value("id").toString());
                } else if (param.toMap().value("type").toString() == "album") {
                    QString  url     = "/v1/albums/";
                    QObject* context = new QObject(this);
                    QObject::connect(this, &Spotify::requestReady, context,
                                     [=](const QVariantMap& map, const QString& rUrl) {
                                         if (rUrl == url) {
                                             QString url = "/v1/me/player/play";
                                             qCDebug(m_logCategory) << "PLAY MEDIA" << map.value("uri").toString();
                                             QVariantMap rMap;
                                             rMap.insert("context_uri", map.value("uri").toString());
                                             QJsonDocument doc     = QJsonDocument::fromVariant(rMap);
                                             QString       message = doc.toJson(QJsonDocument::JsonFormat::Compact);
                                             qCDebug(m_logCategory) << message;
                                             putRequest("/v1/me/player/play", message);
                                         }
                                         context->deleteLater();
                                     });
                    getRequest(url, param.toMap().value("id").toString());
                } else if (param.toMap().value("type").toString() == "artist") {
                    QString  url     = "/v1/artists/";
                    QObject* context = new QObject(this);
                    QObject::connect(this, &Spotify::requestReady, context,
                                     [=](const QVariantMap& map, const QString& rUrl) {
                                         if (rUrl == url) {
                                             QString url = "/v1/me/player/play";
                                             qCDebug(m_logCategory) << "PLAY MEDIA" << map.value("uri").toString();
                                             QVariantMap rMap;
                                             rMap.insert("context_uri", map.value("uri").toString());
                                             QJsonDocument doc     = QJsonDocument::fromVariant(rMap);
                                             QString       message = doc.toJson(QJsonDocument::JsonFormat::Compact);
                                             qCDebug(m_logCategory) << message;
                                             putRequest("/v1/me/player/play", message);
                                         }
                                         context->deleteLater();
                                     });
                    getRequest(url, param.toMap().value("id").toString());
                } else if (param.toMap().value("type").toString() == "playlist") {
                    QString  url     = "/v1/playlists/";
                    QObject* context = new QObject(this);
                    QObject::connect(this, &Spotify::requestReady, context,
                                     [=](const QVariantMap& map, const QString& rUrl) {
                                         if (rUrl == url) {
                                             QString url = "/v1/me/player/play";
                                             qCDebug(m_logCategory) << "PLAY MEDIA" << map.value("uri").toString();
                                             QVariantMap rMap;
                                             rMap.insert("context_uri", map.value("uri").toString());
                                             QJsonDocument doc     = QJsonDocument::fromVariant(rMap);
                                             QString       message = doc.toJson(QJsonDocument::JsonFormat::Compact);
                                             qCDebug(m_logCategory) << message;
                                             putRequest("/v1/me/player/play", message);
                                         }
                                         context->deleteLater();
                                     });
                    getRequest(url, param.toMap().value("id").toString());
                }
            }
        }
    } else if (command == MediaPlayerDef::C_PAUSE) {
        putRequest("/v1/me/player/pause", "");
    } else if (command == MediaPlayerDef::C_NEXT) {
        postRequest("/v1/me/player/next", "");
    } else if (command == MediaPlayerDef::C_PREVIOUS) {
        postRequest("/v1/me/player/previous", "");
    } else if (command == MediaPlayerDef::C_VOLUME_SET) {
        putRequest("/v1/me/player/volume?volume_percent=" + param.toString(), "");
    } else if (command == MediaPlayerDef::C_SEARCH) {
        search(param.toString());
    } else if (command == MediaPlayerDef::C_GETALBUM) {
        getAlbum(param.toString());
    } else if (command == MediaPlayerDef::C_GETPLAYLIST) {
        if (param.toString() == "user") {
            getUserPlaylists();
        } else {
            getPlaylist(param.toString());
        }
    }
}

void Spotify::updateEntity(const QString& entity_id, const QVariantMap& attr) {
    EntityInterface* entity = static_cast<EntityInterface*>(m_entities->getEntityInterface(entity_id));
    if (entity) {
        // update the media player
        entity->updateAttrByIndex(MediaPlayerDef::Attributes::STATE, attr.value("state").toInt());
        entity->updateAttrByIndex(MediaPlayerDef::Attributes::SOURCE, attr.value("device").toString());
        entity->updateAttrByIndex(MediaPlayerDef::Attributes::VOLUME, attr.value("volume").toInt());
        entity->updateAttrByIndex(MediaPlayerDef::Attributes::MEDIATITLE, attr.value("title").toString());
        entity->updateAttrByIndex(MediaPlayerDef::Attributes::MEDIAARTIST, attr.value("artist").toString());
        entity->updateAttrByIndex(MediaPlayerDef::Attributes::MEDIAIMAGE, attr.value("image").toString());
    }
}

void Spotify::getRequest(const QString& url, const QString& params) {
    if (m_accessToken.isNull() || m_accessToken.isEmpty()) {
        qCWarning(m_logCategory) << "No access token available";
        return;
    }

    // create new networkacces manager and request
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QNetworkRequest        request;

    QObject* context = new QObject(this);

    // connect to finish signal
    QObject::connect(manager, &QNetworkAccessManager::finished, context, [=](QNetworkReply* reply) {
        if (reply->error()) {
            QString errorString = reply->errorString();
            qCWarning(m_logCategory) << errorString;
            if (errorString == "Host requires authentication") {
                refreshAccessToken();
            }
        }

        QString     answer = reply->readAll();
        QVariantMap map;
        if (answer != "") {
            // convert to json
            QJsonParseError parseerror;
            QJsonDocument   doc = QJsonDocument::fromJson(answer.toUtf8(), &parseerror);
            if (parseerror.error != QJsonParseError::NoError) {
                qCWarning(m_logCategory) << "JSON error : " << parseerror.errorString();
                return;
            }

            // createa a map object
            map = doc.toVariant().toMap();
            emit requestReady(map, url);
        }

        reply->deleteLater();
        context->deleteLater();
        manager->deleteLater();
    });

    QObject::connect(
        manager, &QNetworkAccessManager::networkAccessibleChanged, context,
        [=](QNetworkAccessManager::NetworkAccessibility accessibility) { qCDebug(m_logCategory) << accessibility; });

    // set headers
    request.setRawHeader("Content-Type", "application/json");
    request.setRawHeader("Authorization", "Bearer " + m_accessToken.toLocal8Bit());

    // set the URL
    // url = "/v1/me/player"
    // params = "?q=stringquery&limit=20"
    request.setUrl(QUrl(m_apiURL + url + params));

    // send the get request
    manager->get(request);
}

void Spotify::postRequest(const QString& url, const QString& params) {
    if (m_accessToken.isNull() || m_accessToken.isEmpty()) {
        qCWarning(m_logCategory) << "No access token available";
        return;
    }

    // create new networkacces manager and request
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QNetworkRequest        request;

    QObject* context = new QObject(this);

    // connect to finish signal
    QObject::connect(manager, &QNetworkAccessManager::finished, context, [=](QNetworkReply* reply) {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode != 204) {
            qCWarning(m_logCategory) << "ERROR WITH POST REQUEST " << statusCode;
        }
        reply->deleteLater();
        context->deleteLater();
        manager->deleteLater();
    });

    QObject::connect(
        manager, &QNetworkAccessManager::networkAccessibleChanged, context,
        [=](QNetworkAccessManager::NetworkAccessibility accessibility) { qCDebug(m_logCategory) << accessibility; });

    // set headers
    request.setRawHeader("Content-Type", "application/json");
    request.setRawHeader("Authorization", "Bearer " + m_accessToken.toLocal8Bit());

    // set the URL
    // url = "/v1/me/player"
    // params = "?q=stringquery&limit=20"
    request.setUrl(QUrl(m_apiURL + url + params));

    // send the get request
    manager->post(request, "");
}

void Spotify::putRequest(const QString& url, const QString& params) {
    if (m_accessToken.isNull() || m_accessToken.isEmpty()) {
        qCWarning(m_logCategory) << "No access token available";
        return;
    }

    // create new networkacces manager and request
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QNetworkRequest        request;

    QObject* context = new QObject(this);

    // connect to finish signal
    QObject::connect(manager, &QNetworkAccessManager::finished, context, [=](QNetworkReply* reply) {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode != 204) {
            qCWarning(m_logCategory) << "ERROR WITH PUT REQUEST " << statusCode << reply->readAll();
        }
        reply->deleteLater();
        context->deleteLater();
        manager->deleteLater();
    });

    QObject::connect(
        manager, &QNetworkAccessManager::networkAccessibleChanged, context,
        [=](QNetworkAccessManager::NetworkAccessibility accessibility) { qCDebug(m_logCategory) << accessibility; });

    // set headers
    request.setRawHeader("Content-Type", "application/json");
    request.setRawHeader("Authorization", "Bearer " + m_accessToken.toLocal8Bit());

    // set the URL
    // url = "/v1/me/player"
    request.setUrl(QUrl(m_apiURL + url));

    QByteArray data = params.toUtf8();

    // send the get request
    manager->put(request, data);
}

void Spotify::onTokenTimeOut() {
    // get a new access token
    refreshAccessToken();
}

void Spotify::onPollingTimerTimeout() { getCurrentPlayer(); }

void Spotify::onProgressBarTimerTimeout() {
    EntityInterface* entity = static_cast<EntityInterface*>(m_entities->getEntityInterface(m_entityId));
    if (entity) {
        m_progressBarPosition++;
        entity->updateAttrByIndex(MediaPlayerDef::MEDIAPROGRESS, m_progressBarPosition);
    }
}
