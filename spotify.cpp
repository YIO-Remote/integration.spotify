#include <QtDebug>
#include <QJsonDocument>
#include <QJsonArray>
#include <QJsonObject>
#include <QLoggingCategory>

#include "spotify.h"
#include "../remote-software/sources/entities/mediaplayerinterface.h"

Q_LOGGING_CATEGORY(LC, "SPOTIFY INTEGRATION");

void Spotify::create(const QVariantMap &config, QObject *entities, QObject *notifications, QObject *api, QObject *configObj)
{
    QMap<QObject *, QVariant> returnData;

    QVariantList data;
    QString mdns;

    for (QVariantMap::const_iterator iter = config.begin(); iter != config.end(); ++iter) {
        if (iter.key() == "mdns") {
            mdns = iter.value().toString();
        } else if (iter.key() == "data") {
            data = iter.value().toList();
        }
    }

    for (int i=0; i<data.length(); i++)
    {
        SpotifyBase* spotifyObj = new SpotifyBase(this);
        spotifyObj->setup(data[i].toMap(), entities, notifications, api, configObj);

        QVariantMap d = data[i].toMap();
        d.insert("mdns", mdns);
        d.insert("type", config.value("type").toString());
        returnData.insert(spotifyObj, d);
    }

    emit createDone(returnData);
}

SpotifyBase::SpotifyBase(QObject* parent)
{
    this->setParent(parent);

    m_polling_timer = new QTimer(this);
    m_polling_timer->setInterval(2000);
    QObject::connect(m_polling_timer, &QTimer::timeout, this, &SpotifyBase::onPollingTimerTimeout);
}

void SpotifyBase::setup(const QVariantMap& config, QObject* entities, QObject* notifications, QObject* api, QObject* configObj)
{
    for (QVariantMap::const_iterator iter = config.begin(); iter != config.end(); ++iter) {
        if (iter.key() == "friendly_name")
            setFriendlyName(iter.value().toString());
        else if (iter.key() == "id")
            setIntegrationId(iter.value().toString());
        else if (iter.key() == "data") {
            QVariantMap map = iter.value().toMap();
            m_client_id = map.value("client_id").toString();
            m_client_secret = map.value("client_secret").toString();
            m_access_token = map.value("access_token").toString();
            m_refresh_token = map.value("refresh_token").toString();
            m_entity_id = map.value("entity_id").toString();
        }
    }

    m_entities = qobject_cast<EntitiesInterface *>(entities);
    m_notifications = qobject_cast<NotificationsInterface *>(notifications);
    m_api = qobject_cast<YioAPIInterface *>(api);
    m_config = qobject_cast<ConfigInterface *>(configObj);
}

void SpotifyBase::connect()
{
    setState(CONNECTED);
    m_polling_timer->start();
}

void SpotifyBase::disconnect()
{
    setState(DISCONNECTED);
    m_polling_timer->stop();
}

void SpotifyBase::refreshAccessToken()
{
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QNetworkRequest request;

    QObject::connect(manager, &QNetworkAccessManager::finished, this, [=] (QNetworkReply* reply) {
        if (reply->error()) {
            qDebug(LC) << reply->errorString();
        }

        QString answer = reply->readAll();

        // convert to json
        QJsonParseError parseerror;
        QJsonDocument doc = QJsonDocument::fromJson(answer.toUtf8(), &parseerror);
        if (parseerror.error != QJsonParseError::NoError) {
            qDebug(LC) << "JSON error : " << parseerror.errorString();
            return;
        }
        QVariantMap map = doc.toVariant().toMap();

        // store the refresh and acccess tokens
        if (map.contains("access_token"))
            m_access_token = map.value("access_token").toString();

        if (map.contains("expires_in"))
            m_token_expire = map.value("expires_in").toInt();

        if (map.contains("refresh_token"))
            m_refresh_token = map.value("refresh_token").toString();

        // start timer with the expire date on the access token
        m_tokenTimeOutTimer = new QTimer(this);
        m_tokenTimeOutTimer->setSingleShot(true);

        // connect the token timeout timer to the function that handles the timeout
        QObject::connect(m_tokenTimeOutTimer, &QTimer::timeout, this, &SpotifyBase::onTokenTimeOut);

        // get the token 60 seconds before expiry
        m_tokenTimeOutTimer->start((m_token_expire-60)*1000);

        reply->deleteLater();
    });

    QObject::connect(manager, &QNetworkAccessManager::networkAccessibleChanged, this, [=](QNetworkAccessManager::NetworkAccessibility accessibility) {
        qDebug(LC) << accessibility;
    });

    QByteArray postData;
    postData.append("grant_type=refresh_token&");
    postData.append("refresh_token=");
    postData.append(m_refresh_token);

    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/x-www-form-urlencoded");

    QString header_auth;
    header_auth.append(m_client_id).append(":").append(m_client_secret);

    request.setRawHeader("Authorization", "Basic " + header_auth.toUtf8().toBase64());
    request.setUrl(m_apiURL + "/api/token");

    manager->post(request, postData);
}

void SpotifyBase::search(QString query)
{
    search(query, "album,artist,playlist,track", "20", "0");
}

void SpotifyBase::search(QString query, QString type)
{
    search(query, type, "20", "0");
}

void SpotifyBase::search(QString query, QString type, QString limit, QString offset)
{
    query.replace(" ", "%20");

    QObject::connect(this, &SpotifyBase::requestReady, this, [=] (const QVariantMap& map) {
        qDebug(LC) << map;
    });

    getRequest("/v1/search", "?q=" + query + "&type=" + type + "&limit=" + limit + "&offset=" + offset);
}

void SpotifyBase::getCurrentPlayer()
{
    QObject::connect(this, &SpotifyBase::requestReady, this, [=] (const QVariantMap& map) {
        QVariantMap attr;

        if (map.contains("item")) {
            // get the image
            attr.insert("image", map.value("item").toMap().value("album").toMap().value("images").toList()[0].toMap().value("url").toString());

            // get the device
            attr.insert("device", map.value("device").toMap().value("name").toString());

            // get the volume
            attr.insert("volume", map.value("device").toMap().value("volume_percent").toInt());
            qDebug() << map.value("device").toMap().value("volume_percent").toInt();

            // get track title
            attr.insert("title", map.value("item").toMap().value("name").toString());

            // get artist
            attr.insert("artist", map.value("item").toMap().value("artists").toList()[0].toMap().value("name").toString());

            // state
            if (map.value("is_playing").toBool()) {
                attr.insert("state", 3); // playing
            } else {
                attr.insert("state", 2); // idle
            }

        } else {
            attr.insert("image", "");
            attr.insert("device", "");
            attr.insert("title", "");
            attr.insert("artist", "");
            attr.insert("state", 0); // off
        }

        // update the entity
        updateEntity(m_entity_id, attr);
    });

    getRequest("/v1/me/player", "");
}

void SpotifyBase::sendCommand(const QString& type, const QString& entity_id, const QString& command, const QVariant& param)
{
    if (type == "media_player" && entity_id == m_entity_id) {
        if (command == "PLAY")
            putRequest("/v1/me/player/play", "");
        else if (command == "PAUSE")
            putRequest("/v1/me/player/pause", "");
        else if (command == "NEXT")
            postRequest("/v1/me/player/next", "");
        else if (command == "PREVIOUS")
            postRequest("/v1/me/player/previous", "");
        else if (command == "VOLUME") {
            putRequest("/v1/me/player/volume", "?=volume_percent=" + param.toString());
        }
    }
}

void SpotifyBase::updateEntity(const QString &entity_id, const QVariantMap &attr)
{
    EntityInterface* entity = static_cast<EntityInterface*>(m_entities->getEntityInterface(entity_id));
    if (entity) {
        // update the media player
        entity->updateAttrByIndex(static_cast<int>(MediaPlayerDef::Attributes::STATE), attr.value("state").toInt());
        entity->updateAttrByIndex(static_cast<int>(MediaPlayerDef::Attributes::SOURCE), attr.value("device").toString());
        entity->updateAttrByIndex(static_cast<int>(MediaPlayerDef::Attributes::VOLUME), attr.value("volume").toInt());
        entity->updateAttrByIndex(static_cast<int>(MediaPlayerDef::Attributes::MEDIATITLE), attr.value("title").toString());
        entity->updateAttrByIndex(static_cast<int>(MediaPlayerDef::Attributes::MEDIAARTIST), attr.value("artist").toString());
        entity->updateAttrByIndex(static_cast<int>(MediaPlayerDef::Attributes::MEDIAIMAGE), attr.value("image").toString());
    }
}

void SpotifyBase::getRequest(const QString &url, const QString &params)
{
    // create new networkacces manager and request
    m_manager = new QNetworkAccessManager(this);
    QNetworkRequest request;

    // connect to finish signal
    QObject::connect(m_manager, &QNetworkAccessManager::finished, this, [=] (QNetworkReply* reply) {
        if (reply->error()) {
            qDebug(LC) << reply->errorString();
        }

        QString answer = reply->readAll();
        QVariantMap map;

        if (answer != "") {
            // convert to json
            QJsonParseError parseerror;
            QJsonDocument doc = QJsonDocument::fromJson(answer.toUtf8(), &parseerror);
            if (parseerror.error != QJsonParseError::NoError) {
                qDebug(LC) << "JSON error : " << parseerror.errorString();
                return;
            }

            // createa a map object
            map = doc.toVariant().toMap();
        }

        emit requestReady(map);

        reply->deleteLater();
    });

    QObject::connect(m_manager, &QNetworkAccessManager::networkAccessibleChanged, this, [=](QNetworkAccessManager::NetworkAccessibility accessibility) {
        qDebug(LC) << accessibility;
    });

    // set headers
    request.setRawHeader("Content-Type", "application/json");
    request.setRawHeader("Authorization", "Bearer " + m_access_token.toLocal8Bit());

    // set the URL
    // url = "/v1/me/player"
    // params = "?q=stringquery&limit=20"
    request.setUrl(QUrl(m_apiURL + url + params));

    // send the get request
    m_manager->get(request);
}

void SpotifyBase::postRequest(const QString &url, const QString &params)
{
    // create new networkacces manager and request
    m_manager = new QNetworkAccessManager(this);
    QNetworkRequest request;

    // connect to finish signal
    QObject::connect(m_manager, &QNetworkAccessManager::finished, this, [=] (QNetworkReply* reply) {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode != 204) {
            qDebug(LC) << "ERROR WITH POST REQUEST " << statusCode;
        }
        reply->deleteLater();
    });

    QObject::connect(m_manager, &QNetworkAccessManager::networkAccessibleChanged, this, [=](QNetworkAccessManager::NetworkAccessibility accessibility) {
        qDebug(LC) << accessibility;
    });

    // set headers
    request.setRawHeader("Content-Type", "application/json");
    request.setRawHeader("Authorization", "Bearer " + m_access_token.toLocal8Bit());

    // set the URL
    // url = "/v1/me/player"
    // params = "?q=stringquery&limit=20"
    request.setUrl(QUrl(m_apiURL + url + params));

    // send the get request
    m_manager->post(request, "");
}

void SpotifyBase::putRequest(const QString &url, const QString &params)
{
    // create new networkacces manager and request
    m_manager = new QNetworkAccessManager(this);
    QNetworkRequest request;

    // connect to finish signal
    QObject::connect(m_manager, &QNetworkAccessManager::finished, this, [=] (QNetworkReply* reply) {
        int statusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (statusCode != 204) {
            qDebug(LC) << "ERROR WITH PUT REQUEST " << statusCode;
        }
        reply->deleteLater();
    });

    QObject::connect(m_manager, &QNetworkAccessManager::networkAccessibleChanged, this, [=](QNetworkAccessManager::NetworkAccessibility accessibility) {
        qDebug(LC) << accessibility;
    });

    // set headers
    request.setRawHeader("Content-Type", "application/json");
    request.setRawHeader("Authorization", "Bearer " + m_access_token.toLocal8Bit());

    // set the URL
    // url = "/v1/me/player"
    // params = "?q=stringquery&limit=20"
    request.setUrl(QUrl(m_apiURL + url + params));

    // send the get request
    m_manager->put(request, "");
}

void SpotifyBase::onTokenTimeOut()
{
    // get a new access token
    refreshAccessToken();
}

void SpotifyBase::onPollingTimerTimeout()
{
    getCurrentPlayer();
}
