#ifndef SPOTIFY_H
#define SPOTIFY_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>

#include "../remote-software/sources/integrations/integration.h"
#include "../remote-software/sources/integrations/integrationinterface.h"
#include "../remote-software/sources/entities/entitiesinterface.h"
#include "../remote-software/sources/entities/entityinterface.h"
#include "../remote-software/sources/notificationsinterface.h"
#include "../remote-software/sources/yioapiinterface.h"
#include "../remote-software/sources/configinterface.h"

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
    void create                     (const QVariantMap& config, QObject *entities, QObject *notifications, QObject* api, QObject *configObj) override;
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

    // Spotify API calls
    void             refreshAccessToken ();
    Q_INVOKABLE void search             (QString query);
    Q_INVOKABLE void search             (QString query, QString type);
    Q_INVOKABLE void search             (QString query, QString type, QString limit, QString offset);

    // Spotify Connect
    Q_INVOKABLE void getCurrentPlayer   ();

public slots:
    void sendCommand                    (const QString& type, const QString& entity_id, const QString& command, const QVariant& param);

private:
    void updateEntity                   (const QString& entity_id, const QVariantMap& attr);

    EntitiesInterface*                  m_entities;
    NotificationsInterface*             m_notifications;
    YioAPIInterface*                    m_api;
    ConfigInterface*                    m_config;

    QString                             m_entity_id;

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
};

#endif // SPOTIFY_H
