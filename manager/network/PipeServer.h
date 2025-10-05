#ifndef PIPESERVER_H
#define PIPESERVER_H

#include <QObject>
#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonObject>
#include <QJsonDocument>
#include <QMap>
#include <QByteArray>
#include <QDataStream>
#include "protocol.qpb.h"
#include "bot/BotManager.h"

class LogManager;

class PipeServer : public QObject
{
    Q_OBJECT

public:
    static PipeServer& instance();

    // Delete copy constructor and assignment operator
    PipeServer(const PipeServer&) = delete;
    PipeServer& operator=(const PipeServer&) = delete;

    static bool start(const QString &pipeName = "minecraft_manager");
    static void stop();

    static void sendToClient(int connectionId, const QByteArray &data);
    static void broadcastToAll(const QByteArray &data);

    static int getConnectionId(const QString &botName);
    static QList<int> getAllConnectionIds();

signals:
    void clientConnected(int connectionId, const QString &botName);
    void clientDisconnected(int connectionId);

private slots:
    void handleNewConnection();
    void handleClientData();
    void handleClientDisconnection();

private:
    explicit PipeServer();
    ~PipeServer();

    bool startImpl(const QString &pipeName);
    void stopImpl();
    void sendToClientImpl(int connectionId, const QByteArray &data);
    void broadcastToAllImpl(const QByteArray &data);
    int getConnectionIdImpl(const QString &botName) const;
    QList<int> getAllConnectionIdsImpl() const;

    void processMessage(int connectionId, const QByteArray &data);
    QLocalServer *server;
    QMap<int, QLocalSocket*> connections;
    QMap<int, QString> connectionBotNames;
    QMap<int, QDataStream*> connectionStreams;  // Data streams for each connection
    int nextConnectionId;
    QString serverPipeName;
};

#endif // PIPESERVER_H