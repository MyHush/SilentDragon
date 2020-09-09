// Copyright 2019-2020 The Hush developers
// Released under the GPLv3
#ifndef CONNECTION_H
#define CONNECTION_H

#include "mainwindow.h"
#include "ui_connection.h"
#include "precompiled.h"

class RPC;

enum ConnectionType {
    DetectedConfExternalZcashD = 1,
    UISettingsZCashD,
    InternalZcashD
};

struct ConnectionConfig {
    QString host;
    QString port;
    QString rpcuser;
    QString rpcpassword;
    bool    usingHushConf;
    bool    zcashDaemon;
    QString zcashDir;
    QString proxy;
    QString consolidation;
    QString deletetx;
    QString zindex;

    ConnectionType connType;
};

class Connection;

class ConnectionLoader {

public:
    ConnectionLoader(MainWindow* main, RPC* rpc);
    ~ConnectionLoader();

    void loadConnection();

private:
    std::shared_ptr<ConnectionConfig> autoDetectHushConf();
    std::shared_ptr<ConnectionConfig> loadFromSettings();

    Connection* makeConnection(std::shared_ptr<ConnectionConfig> config);

    void doAutoConnect(bool tryEhushdStart = true);
    void doManualConnect();

    void createHushConf();
    QString locateHushConfFile();
    QString zcashConfWritableLocation();
    QString zcashParamsDir();

    bool verifyParams();
    void downloadParams(std::function<void(void)> cb);
    void doNextDownload(std::function<void(void)> cb);
    bool startEmbeddedZcashd();

    void refreshHushdState(Connection* connection, std::function<void(void)> refused);

    void showError(QString explanation);
    void showInformation(QString info, QString detail = "");

    void doRPCSetConnection(Connection* conn);

    std::shared_ptr<QProcess> ehushd;

    QDialog*                d;
    Ui_ConnectionDialog*    connD;

    MainWindow*             main;
    RPC*                    rpc;

    QNetworkReply* currentDownload = nullptr;
    QFile*         currentOutput   = nullptr;
    QQueue<QUrl>*  downloadQueue   = nullptr;

    QNetworkAccessManager* client  = nullptr; 
    QTime downloadTime;
};

/**
 * Represents a connection to a hushd. It may even start a new hushd if needed.
 * This is also a UI class, so it may show a dialog waiting for the connection.
*/
class Connection {
public:
    Connection(MainWindow* m, QNetworkAccessManager* c, QNetworkRequest* r, std::shared_ptr<ConnectionConfig> conf);
    ~Connection();

    QNetworkAccessManager*              restclient;
    QNetworkRequest*                    request;
    std::shared_ptr<ConnectionConfig>   config;
    MainWindow*                         main;

    void shutdown();

    void doRPC(const QJsonValue& payload, const std::function<void(QJsonValue)>& cb,
               const std::function<void(QNetworkReply*, const QJsonValue&)>& ne);
    void doRPCWithDefaultErrorHandling(const QJsonValue& payload, const std::function<void(QJsonValue)>& cb);
    void doRPCIgnoreError(const QJsonValue& payload, const std::function<void(QJsonValue)>& cb) ;

    void showTxError(const QString& error);

    // Batch method. Note: Because of the template, it has to be in the header file. 
    template<class T>
    void doBatchRPC(const QList<T>& payloads,
                     std::function<QJsonValue(T)> payloadGenerator,
                     std::function<void(QMap<T, QJsonValue>*)> cb) {
        auto responses = new QMap<T, QJsonValue>(); // zAddr -> list of responses for each call.
        int totalSize = payloads.size();
        if (totalSize == 0)
            return;

        // Keep track of all pending method calls, so as to prevent 
        // any overlapping calls
        static QMap<QString, bool> inProgress;

        QString method = payloadGenerator(payloads[0])["method"].toString();
        qDebug() << __func__ << "(" << method << ") totalSize=" << totalSize << "  at " << QDateTime::currentSecsSinceEpoch();

        //if (inProgress.value(method, false)) {
        //    qDebug() << "In progress batch, skipping";
        //    return;
        //}

        for (auto item: payloads) {
            qDebug() << __func__ << ": payload " << item;
            QJsonValue payload = payloadGenerator(item);
            inProgress[method] = true;
            
            QJsonDocument jd_rpc_call(payload.toObject());
            QByteArray ba_rpc_call = jd_rpc_call.toJson();
            QNetworkReply *reply   = restclient->post(*request, ba_rpc_call);

            QObject::connect(reply, &QNetworkReply::finished, [=] {
                reply->deleteLater();
                if (shutdownInProgress) {
                    qDebug() << __func__ << ": Ignoring callback because shutdown in progress";
                    return;
                }
                
                auto all = reply->readAll();            
                auto parsed = QJsonDocument::fromJson(all);

                if (reply->error() != QNetworkReply::NoError) {            
                    qDebug() << "Error JSON response: " << parsed.toJson();
                    qDebug() << reply->errorString();

                    (*responses)[item] = {};    // Empty object
                } else {
                    if (parsed.isEmpty()) {
                        qDebug() << __func__ << ": got empty response";
                        (*responses)[item] = {};    // Empty object
                    } else {
                        qDebug() << "Got network reply from RPC " << method << "(" << payload << ")" << " of " << parsed["result"].toString().size() << " bytes";
                        (*responses)[item] = parsed["result"];
                    }
                }
            });
        }

        auto waitTimer = new QTimer(main);
        QObject::connect(waitTimer, &QTimer::timeout, [=]() {
            if (shutdownInProgress) {
                qDebug() << "Shutdown in progress, aborting";
                waitTimer->stop();
                waitTimer->deleteLater();  
                return;
            }

            //qDebug() << ".";

            // If all responses have arrived, return
            if (responses->size() == totalSize) {

                qDebug() << __func__ << ": stopping timer";
                waitTimer->stop();
                
                cb(responses);
                inProgress[method] = false;

                waitTimer->deleteLater();            
            }
        });
        qDebug() << __func__ << ": starting timer"<< QDateTime::currentSecsSinceEpoch();
        waitTimer->start(100);    
        qDebug() << __func__ << ": finished at " << QDateTime::currentSecsSinceEpoch();
    }

private:
    bool shutdownInProgress = false;    
};

#endif
