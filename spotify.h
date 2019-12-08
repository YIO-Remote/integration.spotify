#ifndef SPOTIFY_H
#define SPOTIFY_H

#include <QObject>
#include <QString>
#include <QVariant>
#include <QTimer>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QtConcurrent/QtConcurrentRun>
#include <QLoggingCategory>

#include "../remote-software/sources/integrations/integration.h"
#include "../remote-software/sources/integrations/plugininterface.h"
#include "../remote-software/sources/entities/entitiesinterface.h"
#include "../remote-software/sources/entities/entityinterface.h"
#include "../remote-software/sources/notificationsinterface.h"
#include "../remote-software/sources/yioapiinterface.h"
#include "../remote-software/sources/configinterface.h"
#include "../remote-software/components/media_player/sources/searchmodel_mediaplayer.h"

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// SPOTIFY FACTORY
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
class Spotify : public PluginInterface
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID "YIO.PluginInterface" FILE "spotify.json")
    Q_INTERFACES(PluginInterface)

public:
    explicit Spotify() :
        m_log("spotify")
    {}
    void create                         (const QVariantMap& config, QObject *entities, QObject *notifications, QObject* api, QObject *configObj) override;

    void setLogEnabled                  (QtMsgType msgType, bool enable) override
    {
        m_log.setEnabled(msgType, enable);
    }
private:
    QLoggingCategory    m_log;
};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////
//// SPOTIFY CLASS
////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

class SpotifyBase : public Integration
{
    Q_OBJECT

public:
    explicit SpotifyBase(QLoggingCategory& log, QObject *parent);

    Q_INVOKABLE void setup              (const QVariantMap& config, QObject *entities, QObject *notifications, QObject* api, QObject *configObj);
    Q_INVOKABLE void connect            ();
    Q_INVOKABLE void disconnect         ();
    Q_INVOKABLE void sendCommand        (const QString& type, const QString& entity_id, const QString& command, const QVariant& param);

    // Spotify API authentication
    void             refreshAccessToken ();

    // Spotify API calls
    Q_INVOKABLE void search             (QString query);
    Q_INVOKABLE void search             (QString query, QString type);
    Q_INVOKABLE void search             (QString query, QString type, QString limit, QString offset);

    // Spotify Connect API calls
    Q_INVOKABLE void getCurrentPlayer   ();

signals:
    void requestReady(const QVariantMap& obj, const QString& url);

public slots:
    void onStandByOn                    ();
    void onStandByOff                   ();

private:
    void updateEntity                   (const QString& entity_id, const QVariantMap& attr);

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

    QLoggingCategory&                   m_log;

private slots:
    void                                onTokenTimeOut();
    void                                onPollingTimerTimeout();
};

#endif // SPOTIFY_H
