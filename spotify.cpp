#include <QtDebug>
#include <QJsonDocument>
#include <QJsonArray>

#include "spotify.h"
#include "../remote-software/sources/entities/mediaplayerinterface.h"

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

    getCurrentPlayer();
}

void SpotifyBase::disconnect()
{
    setState(DISCONNECTED);
}

void SpotifyBase::refreshAccessToken()
{
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QNetworkRequest request;

    QObject::connect(manager, &QNetworkAccessManager::finished, this, [=] (QNetworkReply* reply) {
        if (reply->error()) {
            qDebug() << reply->errorString();
        }

        QString answer = reply->readAll();

        // convert to json
        QJsonParseError parseerror;
        QJsonDocument doc = QJsonDocument::fromJson(answer.toUtf8(), &parseerror);
        if (parseerror.error != QJsonParseError::NoError) {
            qDebug() << "JSON error : " << parseerror.errorString();
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
        qDebug() << accessibility;
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
    query.replace(" ", "%20");

    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QNetworkRequest request;

    QObject::connect(manager, &QNetworkAccessManager::finished, this, [=] (QNetworkReply* reply) {
        if (reply->error()) {
            qDebug() << reply->errorString();
        }

        QString answer = reply->readAll();

        // convert to json
        QJsonParseError parseerror;
        QJsonDocument doc = QJsonDocument::fromJson(answer.toUtf8(), &parseerror);
        if (parseerror.error != QJsonParseError::NoError) {
            qDebug() << "JSON error : " << parseerror.errorString();
            return;
        }
        QVariantMap map = doc.toVariant().toMap();

        reply->deleteLater();
    });

    QObject::connect(manager, &QNetworkAccessManager::networkAccessibleChanged, this, [=](QNetworkAccessManager::NetworkAccessibility accessibility) {
        qDebug() << accessibility;
    });

    request.setRawHeader("Content-Type", "application/json");
    request.setRawHeader("Authorization", "Bearer " + m_access_token.toLocal8Bit());
    request.setUrl(m_apiURL + "/v1/search" + "?q=" + query + "&type=album,artist,playlist,track");

    manager->get(request);
}

void SpotifyBase::search(QString query, QString type)
{
    query.replace(" ", "%20");

    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QNetworkRequest request;

    QObject::connect(manager, &QNetworkAccessManager::finished, this, [=] (QNetworkReply* reply) {
        if (reply->error()) {
            qDebug() << reply->errorString();
        }

        QString answer = reply->readAll();

        // convert to json
        QJsonParseError parseerror;
        QJsonDocument doc = QJsonDocument::fromJson(answer.toUtf8(), &parseerror);
        if (parseerror.error != QJsonParseError::NoError) {
            qDebug() << "JSON error : " << parseerror.errorString();
            return;
        }
        QVariantMap map = doc.toVariant().toMap();

        reply->deleteLater();
    });

    QObject::connect(manager, &QNetworkAccessManager::networkAccessibleChanged, this, [=](QNetworkAccessManager::NetworkAccessibility accessibility) {
        qDebug() << accessibility;
    });

    request.setRawHeader("Content-Type", "application/json");
    request.setRawHeader("Authorization", "Bearer " + m_access_token.toLocal8Bit());
    request.setUrl(m_apiURL + "/v1/search" + "?q=" + query + "&type=" + type);

    manager->get(request);
}

void SpotifyBase::search(QString query, QString type, QString limit, QString offset)
{
    query.replace(" ", "%20");

    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QNetworkRequest request;

    QObject::connect(manager, &QNetworkAccessManager::finished, this, [=] (QNetworkReply* reply) {
        if (reply->error()) {
            qDebug() << reply->errorString();
        }

        QString answer = reply->readAll();

        // convert to json
        QJsonParseError parseerror;
        QJsonDocument doc = QJsonDocument::fromJson(answer.toUtf8(), &parseerror);
        if (parseerror.error != QJsonParseError::NoError) {
            qDebug() << "JSON error : " << parseerror.errorString();
            return;
        }
        QVariantMap map = doc.toVariant().toMap();

        reply->deleteLater();
    });

    QObject::connect(manager, &QNetworkAccessManager::networkAccessibleChanged, this, [=](QNetworkAccessManager::NetworkAccessibility accessibility) {
        qDebug() << accessibility;
    });

    request.setRawHeader("Content-Type", "application/json");
    request.setRawHeader("Authorization", "Bearer " + m_access_token.toLocal8Bit());
    request.setUrl(m_apiURL + "/v1/search" + "?q=" + query + "&type=" + type + "&limit=" + limit + "&offset=" + offset);

    manager->get(request);
}

void SpotifyBase::getCurrentPlayer()
{
    QNetworkAccessManager* manager = new QNetworkAccessManager(this);
    QNetworkRequest request;

    QObject::connect(manager, &QNetworkAccessManager::finished, this, [=] (QNetworkReply* reply) {
        if (reply->error()) {
            qDebug() << reply->errorString();
        }

        QString answer = reply->readAll();

        if (answer != "") {

            // convert to json
            QJsonParseError parseerror;
            QJsonDocument doc = QJsonDocument::fromJson(answer.toUtf8(), &parseerror);
            if (parseerror.error != QJsonParseError::NoError) {
                qDebug() << "JSON error : " << parseerror.errorString();
                return;
            }
            QVariantMap map = doc.toVariant().toMap();

            // get the image
            QVariantList images = map.value("item").toMap().value("album").toMap().value("images").toList();
            QString url = images[0].toMap().value("url").toString();

            // get the device
            QString device = map.value("device").toMap().value("name").toString();

            // get track title
            QString title =  map.value("item").toMap().value("name").toString();

            // get artist
            QString artist = map.value("item").toMap().value("artists").toList()[0].toMap().value("name").toString();

            // update the entity
            EntityInterface* entity = static_cast<EntityInterface*>(m_entities->getEntityInterface(m_entity_id));
            entity->updateAttrByIndex(static_cast<int>(MediaPlayerDef::Attributes::STATE), 3); // off
            entity->updateAttrByIndex(static_cast<int>(MediaPlayerDef::Attributes::SOURCE), device);
            entity->updateAttrByIndex(static_cast<int>(MediaPlayerDef::Attributes::MEDIATITLE), title);
            entity->updateAttrByIndex(static_cast<int>(MediaPlayerDef::Attributes::MEDIAARTIST), artist);
            entity->updateAttrByIndex(static_cast<int>(MediaPlayerDef::Attributes::MEDIAIMAGE), url);

        } else {
            EntityInterface* entity = static_cast<EntityInterface*>(m_entities->getEntityInterface(m_entity_id));
            entity->updateAttrByIndex(static_cast<int>(MediaPlayerDef::Attributes::STATE), 0); // off
            entity->updateAttrByIndex(static_cast<int>(MediaPlayerDef::Attributes::SOURCE), "");
            entity->updateAttrByIndex(static_cast<int>(MediaPlayerDef::Attributes::MEDIATITLE), "");
            entity->updateAttrByIndex(static_cast<int>(MediaPlayerDef::Attributes::MEDIAARTIST), "");
            entity->updateAttrByIndex(static_cast<int>(MediaPlayerDef::Attributes::MEDIAIMAGE), "");
        }

        reply->deleteLater();
    });

    QObject::connect(manager, &QNetworkAccessManager::networkAccessibleChanged, this, [=](QNetworkAccessManager::NetworkAccessibility accessibility) {
        qDebug() << accessibility;
    });

    request.setRawHeader("Content-Type", "application/json");
    request.setRawHeader("Authorization", "Bearer " + m_access_token.toLocal8Bit());
    request.setUrl(m_apiURL + "/v1/me/player");

    manager->get(request);
}

void SpotifyBase::sendCommand(const QString& type, const QString& entity_id, const QString& command, const QVariant& param)
{
    Q_UNUSED(type)
    Q_UNUSED(entity_id)
    Q_UNUSED(command)
    Q_UNUSED(param)
}

void SpotifyBase::updateEntity(const QString &entity_id, const QVariantMap &attr)
{
    EntityInterface* entity = m_entities->getEntityInterface(entity_id);
    if (entity) {
        // update the media player
    }
}

void SpotifyBase::onTokenTimeOut()
{
    // get a new access token
    refreshAccessToken();
}
