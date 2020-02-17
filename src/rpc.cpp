// Copyright 2019-2020 The Hush Developers
// Released under the GPLv3
#include "rpc.h"

#include "addressbook.h"
#include "settings.h"
#include "senttxstore.h"
#include "version.h"
#include "websockets.h"

using json = nlohmann::json;

RPC::RPC(MainWindow* main) {
    auto cl = new ConnectionLoader(main, this);

    // Execute the load connection async, so we can set up the rest of RPC properly. 
    QTimer::singleShot(1, [=]() { cl->loadConnection(); });

    this->main = main;
    this->ui = main->ui;

    // Setup balances table model
    balancesTableModel = new BalancesTableModel(main->ui->balancesTable);
    main->ui->balancesTable->setModel(balancesTableModel);

    // Setup transactions table model
    transactionsTableModel = new TxTableModel(ui->transactionsTable);
    main->ui->transactionsTable->setModel(transactionsTableModel);
    main->ui->transactionsTable->horizontalHeader()->setSectionResizeMode(3, QHeaderView::Stretch);

    // Set up timer to refresh Price
    priceTimer = new QTimer(main);
    QObject::connect(priceTimer, &QTimer::timeout, [=]() {
        refreshPrice();
    });
    priceTimer->start(Settings::priceRefreshSpeed);

    // Set up a timer to refresh the UI every few seconds
    timer = new QTimer(main);
    QObject::connect(timer, &QTimer::timeout, [=]() {
        //qDebug() << "Refreshing main UI";
        refresh();
    });
    timer->start(Settings::updateSpeed);    

    // Set up the timer to watch for tx status
    txTimer = new QTimer(main);
    QObject::connect(txTimer, &QTimer::timeout, [=]() {
        //qDebug() << "Watching tx status";
        watchTxStatus();
    });
    // Start at every 10s. When an operation is pending, this will change to every second
    txTimer->start(Settings::updateSpeed);  

    usedAddresses = new QMap<QString, bool>();
}

RPC::~RPC() {
    delete timer;
    delete txTimer;

    delete transactionsTableModel;
    delete balancesTableModel;

    delete utxos;
    delete allBalances;
    delete usedAddresses;
    delete zaddresses;
    delete taddresses;

    delete conn;
}

void RPC::setEZcashd(std::shared_ptr<QProcess> p) {
    ezcashd = p;

    if (ezcashd && ui->tabWidget->widget(4) == nullptr) {
        ui->tabWidget->addTab(main->zcashdtab, "zcashd");
    }
}

// Called when a connection to hushd is available. 
void RPC::setConnection(Connection* c) {
    if (c == nullptr) return;

    delete conn;
    this->conn = c;

    ui->statusBar->showMessage("Ready! Thank you for helping secure the Hush network by running a full node.");

    // See if we need to remove the reindex/rescan flags from the zcash.conf file
    auto zcashConfLocation = Settings::getInstance()->getZcashdConfLocation();
    Settings::removeFromZcashConf(zcashConfLocation, "rescan");
    Settings::removeFromZcashConf(zcashConfLocation, "reindex");

    // Refresh the UI
    refreshPrice();
    checkForUpdate();

    // Force update, because this might be coming from a settings update
    // where we need to immediately refresh
    refresh(true);
}

json RPC::makePayload(std::string method, std::string params) {
    json payload = {
        {"jsonrpc", "1.0"},
        {"id", "42" },
        {"method", method },
        {"params", {params}}
    };
    return payload;
}

json RPC::makePayload(std::string method) {
    json payload = {
        {"jsonrpc", "1.0"},
        {"id", "42" },
        {"method", method },
    };
    return payload;
}

void RPC::getTAddresses(const std::function<void(json)>& cb) {
    std::string method = "getaddressesbyaccount";
    std::string params = "";
    conn->doRPCWithDefaultErrorHandling(makePayload(method, std::string("")), cb);
}

void RPC::getZAddresses(const std::function<void(json)>& cb) {
    std::string method = "z_listaddresses";
    conn->doRPCWithDefaultErrorHandling(makePayload(method), cb);
}

void RPC::getTransparentUnspent(const std::function<void(json)>& cb) {
    json payload = {
        {"jsonrpc", "1.0"},
        {"id", "someid"},
        {"method", "listunspent"},
        {"params", {0}}             // Get UTXOs with 0 confirmations as well.
    };

    conn->doRPCWithDefaultErrorHandling(payload, cb);
}

void RPC::getZUnspent(const std::function<void(json)>& cb) {
    json payload = {
        {"jsonrpc", "1.0"},
        {"id", "someid"},
        {"method", "z_listunspent"},
        {"params", {0}}             // Get UTXOs with 0 confirmations as well.
    };

    conn->doRPCWithDefaultErrorHandling(payload, cb);
}

void RPC::newZaddr(const std::function<void(json)>& cb) {
    json payload = {
        {"jsonrpc", "1.0"},
        {"id", "someid"},
        {"method", "z_getnewaddress"},
        {"params", { "sapling" }},
    };
    
    conn->doRPCWithDefaultErrorHandling(payload, cb);
}

void RPC::newTaddr(const std::function<void(json)>& cb) {

	std::string method = "getnewaddress";
    conn->doRPCWithDefaultErrorHandling(makePayload(method), cb);
}

void RPC::getZViewKey(QString addr, const std::function<void(json)>& cb) {
	std::string method = "z_exportviewingkey";
    conn->doRPCWithDefaultErrorHandling(makePayload(method, addr.toStdString()), cb);
}

void RPC::getZPrivKey(QString addr, const std::function<void(json)>& cb) {
	std::string method = "z_exportkey";
    conn->doRPCWithDefaultErrorHandling(makePayload(method, addr.toStdString()), cb);
}

void RPC::getTPrivKey(QString addr, const std::function<void(json)>& cb) {
	std::string method = "dumpprivkey";
    conn->doRPCWithDefaultErrorHandling(makePayload(method, addr.toStdString()), cb);
}

void RPC::importZPrivKey(QString privkey, bool rescan, const std::function<void(json)>& cb) {
    json payload = {
        {"jsonrpc", "1.0"},
        {"id", "someid"},
        {"method", "z_importkey"},
        {"params", { privkey.toStdString(), (rescan? "yes" : "no") }},
    };
    
    conn->doRPCWithDefaultErrorHandling(payload, cb);
}


// TODO: support rescan height and prefix
void RPC::importTPrivKey(QString privkey, bool rescan, const std::function<void(json)>& cb) {
    json payload;

    // If privkey starts with 5, K or L, use old-style Hush params, same as BTC+ZEC
    if( privkey.startsWith("5") || privkey.startsWith("K") || privkey.startsWith("L") ) {
        qDebug() << "Detected old-style HUSH WIF";
        payload = {
            {"jsonrpc", "1.0"},
            {"id", "someid"},
            {"method", "importprivkey"},
            {"params", { privkey.toStdString(), "", "false", "0", "128" }},
        };
    } else {
        qDebug() << "Detected new-style HUSH WIF";
        payload = {
            {"jsonrpc", "1.0"},
            {"id", "someid"},
            {"method", "importprivkey"},
            {"params", { privkey.toStdString(), (rescan? "yes" : "no") }},
        };
    }

    qDebug() <<  "Importing WIF with rescan=" << rescan;

    conn->doRPCWithDefaultErrorHandling(payload, cb);
}

void RPC::validateAddress(QString address, const std::function<void(json)>& cb) {
    QString method = address.startsWith("z") ? "z_validateaddress" : "validateaddress";
    conn->doRPCWithDefaultErrorHandling(makePayload(method.toStdString(), address.toStdString()), cb);
}

void RPC::getBalance(const std::function<void(json)>& cb) {
    json payload = {
        {"jsonrpc", "1.0"},
        {"id", "someid"},
        {"method", "z_gettotalbalance"},
        {"params", {0}}             // Get Unconfirmed balance as well.
    };

    conn->doRPCWithDefaultErrorHandling(payload, cb);
}

void RPC::getTransactions(const std::function<void(json)>& cb) {
	std::string method = "listtransactions";
    conn->doRPCWithDefaultErrorHandling(makePayload(method), cb);
}

void RPC::sendZTransaction(json params, const std::function<void(json)>& cb, 
    const std::function<void(QString)>& err) {
    json payload = {
        {"jsonrpc", "1.0"},
        {"id", "someid"},
        {"method", "z_sendmany"},
        {"params", params}
    };

    conn->doRPC(payload, cb,  [=] (auto reply, auto parsed) {
        if (!parsed.is_discarded() && !parsed["error"]["message"].is_null()) {
            err(QString::fromStdString(parsed["error"]["message"]));    
        } else {
            err(reply->errorString());
        }
    });
}

/**
 * Method to get all the private keys for both z and t addresses. It will make 2 batch calls,
 * combine the result, and call the callback with a single list containing both the t-addr and z-addr
 * private keys
 */ 
void RPC::getAllPrivKeys(const std::function<void(QList<QPair<QString, QString>>)> cb) {
    if (conn == nullptr) {
        // No connection, just return
        return;
    }

    // A special function that will call the callback when two lists have been added
    auto holder = new QPair<int, QList<QPair<QString, QString>>>();
    holder->first = 0;  // This is the number of times the callback has been called, initialized to 0
    auto fnCombineTwoLists = [=] (QList<QPair<QString, QString>> list) {
        // Increment the callback counter
        holder->first++;    

        // Add all
        std::copy(list.begin(), list.end(), std::back_inserter(holder->second));
        
        // And if the caller has been called twice, do the parent callback with the 
        // collected list
        if (holder->first == 2) {
            // Sort so z addresses are on top
            std::sort(holder->second.begin(), holder->second.end(), 
                        [=] (auto a, auto b) { return a.first > b.first; });

            cb(holder->second);
            delete holder;
        }            
    };

    // A utility fn to do the batch calling
    auto fnDoBatchGetPrivKeys = [=](json getAddressPayload, std::string privKeyDumpMethodName) {
        conn->doRPCWithDefaultErrorHandling(getAddressPayload, [=] (json resp) {
            QList<QString> addrs;
            for (auto addr : resp.get<json::array_t>()) {   
                addrs.push_back(QString::fromStdString(addr.get<json::string_t>()));
            }

            // Then, do a batch request to get all the private keys
            conn->doBatchRPC<QString>(
                addrs, 
                [=] (auto addr) {
                    json payload = {
                        {"jsonrpc", "1.0"},
                        {"id", "someid"},
                        {"method", privKeyDumpMethodName},
                        {"params", { addr.toStdString() }},
                    };
                    return payload;
                },
                [=] (QMap<QString, json>* privkeys) {
                    QList<QPair<QString, QString>> allTKeys;
                    for (QString addr: privkeys->keys()) {
                        allTKeys.push_back(
                            QPair<QString, QString>(
                                addr, 
                                QString::fromStdString(privkeys->value(addr).get<json::string_t>())));
                    }

                    fnCombineTwoLists(allTKeys);
                    delete privkeys;
                }
            );
        });
    };

    // First get all the t and z addresses.
    json payloadT = {
        {"jsonrpc", "1.0"},
        {"id", "someid"},
        {"method", "getaddressesbyaccount"},
        {"params", {""} }
    };

    json payloadZ = {
        {"jsonrpc", "1.0"},
        {"id", "someid"},
        {"method", "z_listaddresses"}
    };

    fnDoBatchGetPrivKeys(payloadT, "dumpprivkey");
    fnDoBatchGetPrivKeys(payloadZ, "z_exportkey");
}


// Build the RPC JSON Parameters for this tx
void RPC::fillTxJsonParams(json& params, Tx tx) {   
    Q_ASSERT(params.is_array());
    // Get all the addresses and amounts
    json allRecepients = json::array();

    // For each addr/amt/memo, construct the JSON and also build the confirm dialog box    
    for (int i=0; i < tx.toAddrs.size(); i++) {
        auto toAddr = tx.toAddrs[i];

        // Construct the JSON params
        json rec = json::object();
        rec["address"]      = toAddr.addr.toStdString();
        // Force it through string for rounding. Without this, decimal points beyond 8 places
        // will appear, causing an "invalid amount" error
        rec["amount"]       = Settings::getDecimalString(toAddr.amount).toStdString(); //.toDouble(); 
        if (toAddr.addr.startsWith("z") && !toAddr.encodedMemo.trimmed().isEmpty())
            rec["memo"]     = toAddr.encodedMemo.toStdString();

        allRecepients.push_back(rec);
    }

    // Add sender    
    params.push_back(tx.fromAddr.toStdString());
    params.push_back(allRecepients);

    // Add fees if custom fees are allowed.
    if (Settings::getInstance()->getAllowCustomFees()) {
        params.push_back(1); // minconf
        params.push_back(tx.fee);
    }
}


void RPC::noConnection() {    
    QIcon i = QApplication::style()->standardIcon(QStyle::SP_MessageBoxCritical);
    main->statusIcon->setPixmap(i.pixmap(16, 16));
    main->statusIcon->setToolTip("");
    main->statusLabel->setText(QObject::tr("No Connection"));
    main->statusLabel->setToolTip("");
    main->ui->statusBar->showMessage(QObject::tr("No Connection"), 1000);

    // Clear balances table.
    QMap<QString, double> emptyBalances;
    QList<UnspentOutput>  emptyOutputs;
    balancesTableModel->setNewData(&emptyBalances, &emptyOutputs);

    // Clear Transactions table.
    QList<TransactionItem> emptyTxs;
    transactionsTableModel->addTData(emptyTxs);
    transactionsTableModel->addZRecvData(emptyTxs);
    transactionsTableModel->addZSentData(emptyTxs);

    // Clear balances
    ui->balSheilded->setText("");
    ui->balTransparent->setText("");
    ui->balTotal->setText("");
    ui->balUSDTotal->setText("");

    ui->balSheilded->setToolTip("");
    ui->balTransparent->setToolTip("");
    ui->balTotal->setToolTip("");

    // Clear send tab from address
    ui->inputsCombo->clear();
}

// Refresh received z txs by calling z_listreceivedbyaddress/gettransaction
void RPC::refreshReceivedZTrans(QList<QString> zaddrs) {
    if  (conn == nullptr) 
        return noConnection();

    // We'll only refresh the received Z txs if settings allows us.
    if (!Settings::getInstance()->getSaveZtxs()) {
        QList<TransactionItem> emptylist;
        transactionsTableModel->addZRecvData(emptylist);
        return;
    }
        
    // This method is complicated because z_listreceivedbyaddress only returns the txid, and 
    // we have to make a follow up call to gettransaction to get details of that transaction. 
    // Additionally, it has to be done in batches, because there are multiple z-Addresses, 
    // and each z-Addr can have multiple received txs. 

    // 1. For each z-Addr, get list of received txs    
    conn->doBatchRPC<QString>(zaddrs,
        [=] (QString zaddr) {
            json payload = {
                {"jsonrpc", "1.0"},
                {"id", "z_lrba"},
                {"method", "z_listreceivedbyaddress"},
                {"params", {zaddr.toStdString(), 0}}      // Accept 0 conf as well.
            };

            return payload;
        },          
        [=] (QMap<QString, json>* zaddrTxids) {
            // Process all txids, removing duplicates. This can happen if the same address
            // appears multiple times in a single tx's outputs.
            QSet<QString> txids;
            QMap<QString, QString> memos;
            for (auto it = zaddrTxids->constBegin(); it != zaddrTxids->constEnd(); it++) {
                auto zaddr = it.key();
                for (auto& i : it.value().get<json::array_t>()) {   
                    // Mark the address as used
                    usedAddresses->insert(zaddr, true);

                    // Filter out change txs
                    if (! i["change"].get<json::boolean_t>()) {
                        auto txid = QString::fromStdString(i["txid"].get<json::string_t>());
                        txids.insert(txid);    

                        // Check for Memos
                        QString memoBytes = QString::fromStdString(i["memo"].get<json::string_t>());
                        if (!memoBytes.startsWith("f600"))  {
                            QString memo(QByteArray::fromHex(
                                            QByteArray::fromStdString(i["memo"].get<json::string_t>())));
                            if (!memo.trimmed().isEmpty())
                                memos[zaddr + txid] = memo;
                        }
                    }
                }                        
            }

            // 2. For all txids, go and get the details of that txid.
            conn->doBatchRPC<QString>(txids.toList(),
                [=] (QString txid) {
                    json payload = {
                        {"jsonrpc", "1.0"},
                        {"id",  "gettx"},
                        {"method", "gettransaction"},
                        {"params", {txid.toStdString()}}
                    };

                    return payload;
                },
                [=] (QMap<QString, json>* txidDetails) {
                    QList<TransactionItem> txdata;

                    // Combine them both together. For every zAddr's txid, get the amount, fee, confirmations and time
                    for (auto it = zaddrTxids->constBegin(); it != zaddrTxids->constEnd(); it++) {                        
                        for (auto& i : it.value().get<json::array_t>()) {   
                            // Filter out change txs
                            if (i["change"].get<json::boolean_t>())
                                continue;
                            
                            auto zaddr = it.key();
                            auto txid  = QString::fromStdString(i["txid"].get<json::string_t>());

                            // Lookup txid in the map
                            auto txidInfo = txidDetails->value(txid);

                            qint64 timestamp;
                            if (txidInfo.find("time") != txidInfo.end()) {
                                timestamp = txidInfo["time"].get<json::number_unsigned_t>();
                            } else {
                                timestamp = txidInfo["blocktime"].get<json::number_unsigned_t>();
                            }
                            
                            auto amount        = i["amount"].get<json::number_float_t>();
                            auto confirmations = (unsigned long)txidInfo["confirmations"].get<json::number_unsigned_t>();                            

                            TransactionItem tx{ QString("receive"), timestamp, zaddr, txid, amount, 
                                                confirmations, "", memos.value(zaddr + txid, "") };
                            txdata.push_front(tx);
                        }
                    }

                    transactionsTableModel->addZRecvData(txdata);

                    // Cleanup both responses;
                    delete zaddrTxids;
                    delete txidDetails;
                }
            );
        }
    );
} 

/// This will refresh all the balance data from hushd
void RPC::refresh(bool force) {
    if  (conn == nullptr) 
        return noConnection();

    getInfoThenRefresh(force);
}


void RPC::getInfoThenRefresh(bool force) {
    //qDebug() << "getinfo";
    if  (conn == nullptr) 
        return noConnection();

    static bool prevCallSucceeded = false;
	std::string method = "getinfo";
    conn->doRPC(makePayload(method), [=] (const json& reply) {   
        prevCallSucceeded = true;
        // Testnet?
        if (!reply["testnet"].is_null()) {
            Settings::getInstance()->setTestnet(reply["testnet"].get<json::boolean_t>());
        };

        // TODO: checkmark only when getinfo.synced == true!
        // Connected, so display checkmark.
        QIcon i(":/icons/res/connected.gif");
        main->statusIcon->setPixmap(i.pixmap(16, 16));

        static int lastBlock    = 0;
        int curBlock            = reply["blocks"].get<json::number_integer_t>();
        int longestchain        = reply["longestchain"].get<json::number_integer_t>();
        int version             = reply["version"].get<json::number_integer_t>();
        int p2pport             = reply["p2pport"].get<json::number_integer_t>();
        int rpcport             = reply["rpcport"].get<json::number_integer_t>();
        int notarized           = reply["notarized"].get<json::number_integer_t>();
        int protocolversion     = reply["protocolversion"].get<json::number_integer_t>();
        int lag                 = curBlock - notarized;
        int blocks_until_halving= 340000 - curBlock;
        char halving_days[8];
        sprintf(halving_days, "%.2f", (double) (blocks_until_halving * 150) / (60*60*24) );
        QString ntzhash         = QString::fromStdString( reply["notarizedhash"].get<json::string_t>() );
        QString ntztxid         = QString::fromStdString( reply["notarizedtxid"].get<json::string_t>() );
        QString kmdver          = QString::fromStdString( reply["KMDversion"].get<json::string_t>() );

        Settings::getInstance()->setZcashdVersion(version);

        ui->longestchain->setText(QString::number(longestchain));
        ui->notarizedhashvalue->setText( ntzhash );
        ui->notarizedtxidvalue->setText( ntztxid );
        ui->lagvalue->setText( QString::number(lag) );
        ui->version->setText( QString::number(version) );
        ui->kmdversion->setText( kmdver );
        ui->protocolversion->setText( QString::number(protocolversion) );
        ui->p2pport->setText( QString::number(p2pport) );
        ui->rpcport->setText( QString::number(rpcport) );
        ui->halving->setText( QString::number(blocks_until_halving) % " blocks, " % QString::fromStdString(halving_days)  % " days" );

        if ( force || (curBlock != lastBlock) ) {
            // Something changed, so refresh everything.
            lastBlock = curBlock;

            refreshBalances();        
            refreshAddresses(); // This calls refreshZSentTransactions() and refreshReceivedZTrans()
            refreshTransactions();
        }

        int connections = reply["connections"].get<json::number_integer_t>();
        Settings::getInstance()->setPeers(connections);

        if (connections == 0) {
            // If there are no peers connected, then the internet is probably off or something else is wrong. 
            QIcon i = QApplication::style()->standardIcon(QStyle::SP_MessageBoxWarning);
            main->statusIcon->setPixmap(i.pixmap(16, 16));
        }

        // Get network sol/s
        json payload = {
            {"jsonrpc", "1.0"},
            {"id", "someid"},
            {"method", "getnetworksolps"}
        };

        std::string method = "getnetworksolps";
        conn->doRPCIgnoreError(makePayload(method), [=](const json& reply) {
            qint64 solrate = reply.get<json::number_unsigned_t>();

            ui->numconnections->setText(QString::number(connections));
            ui->solrate->setText(QString::number(solrate) % " Sol/s");
        });

        // Get network info
        payload = {
            {"jsonrpc", "1.0"},
            {"id", "someid"},
            {"method", "getnetworkinfo"}
        };

        conn->doRPCIgnoreError(payload, [=](const json& reply) {
            QString clientname    = QString::fromStdString( reply["subversion"].get<json::string_t>() );
            QString localservices = QString::fromStdString( reply["localservices"].get<json::string_t>() );

            ui->clientname->setText(clientname);
            ui->localservices->setText(localservices);
        });


        conn->doRPCIgnoreError(makePayload("getwalletinfo"), [=](const json& reply) {
            int  txcount = reply["txcount"].get<json::number_integer_t>();
            ui->txcount->setText(QString::number(txcount));
        });

        //TODO: If -zindex is enabled, show stats
        conn->doRPCIgnoreError(makePayload("getchaintxstats"), [=](const json& reply) {
            int  txcount = reply["txcount"].get<json::number_integer_t>();
            ui->chaintxcount->setText(QString::number(txcount));
        });

        // Call to see if the blockchain is syncing. 
        conn->doRPCIgnoreError(makePayload("getblockchaininfo"), [=](const json& reply) {
            auto progress    = reply["verificationprogress"].get<double>();
            // TODO: use getinfo.synced
            bool isSyncing   = progress < 0.9999; // 99.99%
            int  blockNumber = reply["blocks"].get<json::number_unsigned_t>();

            int estimatedheight = 0;
            if (reply.find("estimatedheight") != reply.end()) {
                estimatedheight = reply["estimatedheight"].get<json::number_unsigned_t>();
            }

            auto s = Settings::getInstance();
            s->setSyncing(isSyncing);
            s->setBlockNumber(blockNumber);
            std::string ticker = s->get_currency_name();

            // Update hushd tab
            if (isSyncing) {
                QString txt = QString::number(blockNumber);
                if (estimatedheight > 0) {
                    txt = txt % " / ~" % QString::number(estimatedheight);
                    // If estimated height is available, then use the download blocks 
                    // as the progress instead of verification progress.
                    progress = (double)blockNumber / (double)estimatedheight;
                }
                txt = txt %  " ( " % QString::number(progress * 100, 'f', 2) % "% )";
                ui->blockheight->setText(txt);
                ui->heightLabel->setText(QObject::tr("Downloading blocks"));
            } else {
                ui->blockheight->setText(QString::number(blockNumber));
                ui->heightLabel->setText(QObject::tr("Block height"));
            }

            auto ticker_price = s->get_price(ticker);
            QString extra = "";
            if(ticker_price > 0 && ticker != "BTC") {
                extra = QString::number( s->getBTCPrice() ) % "sat";
            }
            QString price = "";
            if (ticker_price > 0) {
                price = QString(", ") % "HUSH" % "=" % QString::number( (double)ticker_price,'f',8) % " " % QString::fromStdString(ticker) % " " % extra;
            }

            // Update the status bar
            QString statusText = QString() %
                (isSyncing ? QObject::tr("Syncing") : QObject::tr("Connected")) %
                " (" %
                (s->isTestnet() ? QObject::tr("testnet:") : "") %
                QString::number(blockNumber) %
                (isSyncing ? ("/" % QString::number(progress*100, 'f', 2) % "%") : QString()) %
                ") " %
                " Lag: " % QString::number(blockNumber - notarized) % price;
            main->statusLabel->setText(statusText);

            auto hushPrice = Settings::getUSDFormat(1);
            QString tooltip;
            if (connections > 0) {
                tooltip = QObject::tr("Connected to hushd");
            }
            else {
                tooltip = QObject::tr("hushd has no peer connections! Network issues?");
            }
            tooltip = tooltip % "(v" % QString::number(Settings::getInstance()->getZcashdVersion()) % ")";

            if (!hushPrice.isEmpty()) {
                tooltip = "1 HUSH = " % hushPrice % "\n" % tooltip;
            }
            main->statusLabel->setToolTip(tooltip);
            main->statusIcon->setToolTip(tooltip);
        });

    }, [=](QNetworkReply* reply, const json&) {
        // hushd has probably disappeared.
        this->noConnection();

        // Prevent multiple dialog boxes, because these are called async
        static bool shown = false;
        if (!shown && prevCallSucceeded) { // show error only first time
            shown = true;
            QMessageBox::critical(main, QObject::tr("Connection Error"), QObject::tr("There was an error connecting to hushd. The error was") + ": \n\n"
                + reply->errorString(), QMessageBox::StandardButton::Ok);
            shown = false;
        }

        prevCallSucceeded = false;
    });
}

void RPC::refreshAddresses() {
    if  (conn == nullptr) 
        return noConnection();
    
    auto newzaddresses = new QList<QString>();

    getZAddresses([=] (json reply) {
        for (auto& it : reply.get<json::array_t>()) {   
            auto addr = QString::fromStdString(it.get<json::string_t>());
            newzaddresses->push_back(addr);
        }

        delete zaddresses;
        zaddresses = newzaddresses;

        // Refresh the sent and received txs from all these z-addresses
        refreshSentZTrans();
        refreshReceivedZTrans(*zaddresses);
    });

    
    auto newtaddresses = new QList<QString>();
    getTAddresses([=] (json reply) {
        for (auto& it : reply.get<json::array_t>()) {   
            auto addr = QString::fromStdString(it.get<json::string_t>());
            if (Settings::isTAddress(addr))
                newtaddresses->push_back(addr);
        }

        delete taddresses;
        taddresses = newtaddresses;
    });
}

// Function to create the data model and update the views, used below.
void RPC::updateUI(bool anyUnconfirmed) {    
    ui->unconfirmedWarning->setVisible(anyUnconfirmed);

    // Update balances model data, which will update the table too
    balancesTableModel->setNewData(allBalances, utxos);

    // Update from address
    main->updateFromCombo();
};

// Function to process reply of the listunspent and z_listunspent API calls, used below.
bool RPC::processUnspent(const json& reply, QMap<QString, double>* balancesMap, QList<UnspentOutput>* newUtxos) {
    bool anyUnconfirmed = false;
    for (auto& it : reply.get<json::array_t>()) {
        QString qsAddr = QString::fromStdString(it["address"]);
        auto confirmations = it["confirmations"].get<json::number_unsigned_t>();
        if (confirmations == 0) {
            anyUnconfirmed = true;
        }

        newUtxos->push_back(
            UnspentOutput{ qsAddr, QString::fromStdString(it["txid"]),
                            Settings::getDecimalString(it["amount"].get<json::number_float_t>()),
                            (int)confirmations, it["spendable"].get<json::boolean_t>() });

        (*balancesMap)[qsAddr] = (*balancesMap)[qsAddr] + it["amount"].get<json::number_float_t>();
    }
    return anyUnconfirmed;
};

void RPC::refreshBalances() {    
    if  (conn == nullptr) 
        return noConnection();

    // 1. Get the Balances
    getBalance([=] (json reply) {    
        auto balT      = QString::fromStdString(reply["transparent"]).toDouble();
        auto balZ      = QString::fromStdString(reply["private"]).toDouble();
        auto balTotal  = QString::fromStdString(reply["total"]).toDouble();

        AppDataModel::getInstance()->setBalances(balT, balZ);

        ui->balSheilded   ->setText(Settings::getDisplayFormat(balZ));
        ui->balTransparent->setText(Settings::getDisplayFormat(balT));
        ui->balTotal      ->setText(Settings::getDisplayFormat(balTotal));

        ui->balSheilded   ->setToolTip(Settings::getUSDFormat(balZ));
        ui->balTransparent->setToolTip(Settings::getUSDFormat(balT));
        ui->balTotal      ->setToolTip(Settings::getUSDFormat(balTotal));

        ui->balUSDTotal   ->setText(Settings::getUSDFormat(balTotal));
        ui->balUSDTotal   ->setToolTip(Settings::getUSDFormat(balTotal));
    });

    // 2. Get the UTXOs
    // First, create a new UTXO list. It will be replacing the existing list when everything is processed.
    auto newUtxos = new QList<UnspentOutput>();
    auto newBalances = new QMap<QString, double>();

    // Call the Transparent and Z unspent APIs serially and then, once they're done, update the UI
    getTransparentUnspent([=] (json reply) {
        auto anyTUnconfirmed = processUnspent(reply, newBalances, newUtxos);

        getZUnspent([=] (json reply) {
            auto anyZUnconfirmed = processUnspent(reply, newBalances, newUtxos);

            // Swap out the balances and UTXOs
            delete allBalances;
            delete utxos;

            allBalances = newBalances;
            utxos       = newUtxos;

            updateUI(anyTUnconfirmed || anyZUnconfirmed);

            main->balancesReady();
        });        
    });
}

void RPC::refreshTransactions() {    
    if  (conn == nullptr) 
        return noConnection();

    getTransactions([=] (json reply) {
        QList<TransactionItem> txdata;

        for (auto& it : reply.get<json::array_t>()) {  
            double fee = 0;
            if (!it["fee"].is_null()) {
                fee = it["fee"].get<json::number_float_t>();
            }

            QString address = (it["address"].is_null() ? "" : QString::fromStdString(it["address"]));

            TransactionItem tx{
                QString::fromStdString(it["category"]),
                (qint64)it["time"].get<json::number_unsigned_t>(),
                address,
                QString::fromStdString(it["txid"]),
                it["amount"].get<json::number_float_t>() + fee,
                (unsigned long)it["confirmations"].get<json::number_unsigned_t>(),
                "", "" };

            txdata.push_back(tx);
            if (!address.isEmpty())
                usedAddresses->insert(address, true);
        }

        // Update model data, which updates the table view
        transactionsTableModel->addTData(txdata);        
    });
}

// Read sent Z transactions from the file.
void RPC::refreshSentZTrans() {
    if  (conn == nullptr) 
        return noConnection();

    auto sentZTxs = SentTxStore::readSentTxFile();

    // If there are no sent z txs, then empty the table. 
    // This happens when you clear history.
    if (sentZTxs.isEmpty()) {
        transactionsTableModel->addZSentData(sentZTxs);
        return;
    }

    QList<QString> txids;

    for (auto sentTx: sentZTxs) {
        txids.push_back(sentTx.txid);
    }

    // Look up all the txids to get the confirmation count for them. 
    conn->doBatchRPC<QString>(txids,
        [=] (QString txid) {
            json payload = {
                {"jsonrpc", "1.0"},
                {"id", "senttxid"},
                {"method", "gettransaction"},
                {"params", {txid.toStdString()}} 
            };

            return payload;
        },          
        [=] (QMap<QString, json>* txidList) {
            auto newSentZTxs = sentZTxs;
            // Update the original sent list with the confirmation count
            // TODO: This whole thing is kinda inefficient. We should probably just update the file
            // with the confirmed block number, so we don't have to keep calling gettransaction for the
            // sent items.
            for (TransactionItem& sentTx: newSentZTxs) {
                auto j = txidList->value(sentTx.txid);
                if (j.is_null())
                    continue;
                auto error = j["confirmations"].is_null();
                if (!error)
                    sentTx.confirmations = j["confirmations"].get<json::number_unsigned_t>();
            }
            
            transactionsTableModel->addZSentData(newSentZTxs);
            delete txidList;
        }
     );
}

void RPC::addNewTxToWatch(const QString& newOpid, WatchedTx wtx) {    
    watchingOps.insert(newOpid, wtx);

    watchTxStatus();
}


// Execute a transaction!
void RPC::executeTransaction(Tx tx, 
        const std::function<void(QString opid)> submitted,
        const std::function<void(QString opid, QString txid)> computed,
        const std::function<void(QString opid, QString errStr)> error) {
    // First, create the json params
    json params = json::array();
    fillTxJsonParams(params, tx);
    std::cout << std::setw(2) << params << std::endl;

    sendZTransaction(params, [=](const json& reply) {
        QString opid = QString::fromStdString(reply.get<json::string_t>());

        // And then start monitoring the transaction
        addNewTxToWatch( opid, WatchedTx { opid, tx, computed, error} );
        submitted(opid);
    },
    [=](QString errStr) {
        error("", errStr);
    });
}


void RPC::watchTxStatus() {
    if  (conn == nullptr) 
        return noConnection();

    // Make an RPC to load pending operation statues
    conn->doRPCIgnoreError(makePayload("z_getoperationstatus"), [=] (const json& reply) {
        // conn->doRPCIgnoreError(payload, [=] (const json& reply) {
        // There's an array for each item in the status
        for (auto& it : reply.get<json::array_t>()) {  
            // If we were watching this Tx and its status became "success", then we'll show a status bar alert
            QString id = QString::fromStdString(it["id"]);
            if (watchingOps.contains(id)) {
                // log any txs we are watching
                //   "creation_time": 1515969376,
                // "execution_secs": 50.416337,
                // And if it ended up successful
                QString status = QString::fromStdString(it["status"]);
                main->loadingLabel->setVisible(false);

                if (status == "success") {
                    auto txid = QString::fromStdString(it["result"]["txid"]);
                    SentTxStore::addToSentTx(watchingOps[id].tx, txid);

                    auto wtx = watchingOps[id];
                    watchingOps.remove(id);
                    wtx.completed(id, txid);

                    qDebug() << "opid "<< id << " started at "<<QString::number((unsigned int)it["creation_time"])<<" took " << QString::number((double)it["execution_secs"]) << " seconds";

                    // Refresh balances to show unconfirmed balances
                    refresh(true);
                } else if (status == "failed") {
                    // If it failed, then we'll actually show a warning.
                    auto errorMsg = QString::fromStdString(it["error"]["message"]);

                    auto wtx = watchingOps[id];
                    watchingOps.remove(id);
                    wtx.error(id, errorMsg);
                } 
            }

            if (watchingOps.isEmpty()) {
                txTimer->start(Settings::updateSpeed);
            } else {
                txTimer->start(Settings::quickUpdateSpeed);
            }
        }

        // If there is some op that we are watching, then show the loading bar, otherwise hide it
        if (watchingOps.empty()) {
            main->loadingLabel->setVisible(false);
        } else {
            main->loadingLabel->setVisible(true);
            main->loadingLabel->setToolTip(QString::number(watchingOps.size()) + QObject::tr(" transaction computing."));
        }
    });
}

void RPC::checkForUpdate(bool silent) {
    qDebug() << "checking for updates";

    if  (conn == nullptr) 
        return noConnection();

    QUrl cmcURL("https://api.github.com/repos/MyHush/SilentDragon/releases");

    QNetworkRequest req;
    req.setUrl(cmcURL);
    QNetworkReply *reply = conn->restclient->get(req);

    QObject::connect(reply, &QNetworkReply::finished, [=] {
        reply->deleteLater();

        try {
            if (reply->error() == QNetworkReply::NoError) {

                auto releases = QJsonDocument::fromJson(reply->readAll()).array();
                QVersionNumber maxVersion(0, 0, 0);

                for (QJsonValue rel : releases) {
                    if (!rel.toObject().contains("tag_name"))
                        continue;

                    QString tag = rel.toObject()["tag_name"].toString();
                    if (tag.startsWith("v"))
                        tag = tag.right(tag.length() - 1);

                    if (!tag.isEmpty()) {
                        auto v = QVersionNumber::fromString(tag);
                        if (v > maxVersion)
                            maxVersion = v;
                    }
                }

                auto currentVersion = QVersionNumber::fromString(APP_VERSION);
                // Get the max version that the user has hidden updates for
                QSettings s;
                auto maxHiddenVersion = QVersionNumber::fromString(s.value("update/lastversion", "0.0.0").toString());

                qDebug() << "Version check: Current " << currentVersion << ", Available " << maxVersion;

                if (maxVersion > currentVersion && (!silent || maxVersion > maxHiddenVersion)) {
                    auto ans = QMessageBox::information(main, QObject::tr("Update Available"), 
                        QObject::tr("A new release v%1 is available! You have v%2.\n\nWould you like to visit the releases page?")
                            .arg(maxVersion.toString())
                            .arg(currentVersion.toString()),
                        QMessageBox::Yes, QMessageBox::Cancel);
                    if (ans == QMessageBox::Yes) {
                        QDesktopServices::openUrl(QUrl("https://github.com/MyHush/SilentDragon/releases"));
                    } else {
                        // If the user selects cancel, don't bother them again for this version
                        s.setValue("update/lastversion", maxVersion.toString());
                    }
                } else {
                    if (!silent) {
                        QMessageBox::information(main, QObject::tr("No updates available"), 
                            QObject::tr("You already have the latest release v%1")
                                .arg(currentVersion.toString()));
                    }
                } 
            }
        } catch (const std::exception& e) {
            // If anything at all goes wrong, just set the price to 0 and move on.
            qDebug() << QString("Caught something nasty: ") << e.what();
        }
    });
}

// Get the HUSH prices
void RPC::refreshPrice() {
    if  (conn == nullptr)
        return noConnection();

    QString price_feed = "https://api.coingecko.com/api/v3/simple/price?ids=hush&vs_currencies=btc%2Cusd%2Ceur%2Ceth%2Cgbp%2Ccny%2Cjpy%2Cidr%2Crub%2Ccad%2Csgd%2Cchf%2Cinr%2Caud%2Cinr%2Ckrw%2Cthb%2Cnzd%2Czar%2Cvef%2Cxau%2Cxag%2Cvnd%2Csar%2Ctwd%2Caed%2Cars%2Cbdt%2Cbhd%2Cbmd%2Cbrl%2Cclp%2Cczk%2Cdkk%2Chuf%2Cils%2Ckwd%2Clkr%2Cpkr%2Cnok%2Ctry%2Csek%2Cmxn%2Cuah%2Chkd&include_market_cap=true&include_24hr_vol=true&include_24hr_change=true";
    QUrl cmcURL(price_feed);
    QNetworkRequest req;
    req.setUrl(cmcURL);
    QNetworkReply *reply = conn->restclient->get(req);
    auto s = Settings::getInstance();

    QObject::connect(reply, &QNetworkReply::finished, [=] {
        reply->deleteLater();

        try {
            if (reply->error() != QNetworkReply::NoError) {
                auto parsed = json::parse(reply->readAll(), nullptr, false);
                if (!parsed.is_discarded() && !parsed["error"]["message"].is_null()) {
                    qDebug() << QString::fromStdString(parsed["error"]["message"]);
                } else {
                    qDebug() << reply->errorString();
                }
                s->setZECPrice(0);
                s->setBTCPrice(0);
                return;
            }

            qDebug() << "No network errors";
            auto all = reply->readAll();
            auto parsed = json::parse(all, nullptr, false);
            if (parsed.is_discarded()) {
                s->setZECPrice(0);
                s->setBTCPrice(0);
                return;
            }

            qDebug() << "Parsed JSON";

            const json& item  = parsed.get<json::object_t>();
            const json& hush  = item["hush"].get<json::object_t>();
            std::string  ticker    = s->get_currency_name();
            std::for_each(ticker.begin(), ticker.end(), [](char & c){ c = ::tolower(c); });
            fprintf(stderr,"ticker=%s\n", ticker.c_str());
            //qDebug() << "Ticker = " + ticker;

            if (hush[ticker] >= 0) {
                qDebug() << "Found hush key in price json";
                //QString price = QString::fromStdString(hush["usd"].get<json::string_t>());
                qDebug() << "HUSH = $" << QString::number((double)hush["usd"]) << " USD";
                qDebug() << "HUSH = " << QString::number((double)hush["eur"]) << " EUR";
                qDebug() << "HUSH = " << QString::number((int) 100000000 * (double) hush["btc"]) << " sat ";

                s->setZECPrice( hush[ticker] );
                s->setBTCPrice( (unsigned int) 100000000 * (double)hush["btc"] );

                std::for_each(ticker.begin(), ticker.end(), [](char & c){ c = ::tolower(c); });
                qDebug() << "ticker=" << QString::fromStdString(ticker);
                // TODO: work harder to prevent coredumps!
                auto price = hush[ticker];
                auto vol   = hush[ticker + "_24h_vol"];
                auto mcap  = hush[ticker + "_market_cap"];

                auto btcprice = hush["btc"];
                auto btcvol   = hush["btc_24h_vol"];
                auto btcmcap  = hush["btc_market_cap"];
                s->set_price(ticker, price);
                s->set_volume(ticker, vol);
                s->set_volume("BTC", btcvol);
                s->set_marketcap(ticker, mcap);

                qDebug() << "Volume = " << (double) vol;
                std::for_each(ticker.begin(), ticker.end(), [](char & c){ c = ::toupper(c); });
                ui->volume->setText( QString::number((double) vol, 'f', 2) + " " + QString::fromStdString(ticker) );
                ui->volumeBTC->setText( QString::number((double) btcvol, 'f', 2) + " BTC" );
                std::for_each(ticker.begin(), ticker.end(), [](char & c){ c = ::toupper(c); });
                // We don't get an actual HUSH volume stat, so we calculate it
                if (price > 0)
                    ui->volumeLocal->setText( QString::number((double) vol / (double) price) + " HUSH");

                qDebug() << "Mcap = " << (double) mcap;
                ui->marketcap->setText(  QString::number( (double) mcap, 'f', 2) + " " + QString::fromStdString(ticker) );
                ui->marketcapBTC->setText( QString::number((double) btcmcap, 'f', 2) + " BTC" );
                //ui->marketcapLocal->setText( QString::number((double) mcap * (double) price) + " " + QString::fromStdString(ticker) );

                refresh(true);
                return;
            } else {
                qDebug() << "No hush key found in JSON! API might be down or we are rate-limited\n";
            }
        } catch (const std::exception& e) {
            // If anything at all goes wrong, just set the price to 0 and move on.
            qDebug() << QString("Caught something nasty: ") << e.what();
        }

        // If nothing, then set the price to 0;
        Settings::getInstance()->setZECPrice(0);
        Settings::getInstance()->setBTCPrice(0);
    });
}

void RPC::shutdownZcashd() {
    // Shutdown embedded hushd if it was started
    if (ezcashd == nullptr || ezcashd->processId() == 0 || conn == nullptr) {
        // No hushd running internally, just return
        return;
    }

    std::string method = "stop";
    conn->doRPCWithDefaultErrorHandling(makePayload(method), [=](auto) {});
    conn->shutdown();

    QDialog d(main);
    d.setWindowFlags(d.windowFlags() & ~(Qt::WindowCloseButtonHint | Qt::WindowContextHelpButtonHint));
    Ui_ConnectionDialog connD;
    connD.setupUi(&d);
    connD.topIcon->setBasePixmap(QIcon(":/icons/res/icon.ico").pixmap(256, 256));
    connD.status->setText(QObject::tr("Please enhance your calm and wait for SilentDragon to exit"));
    connD.statusDetail->setText(QObject::tr("Waiting for hushd to exit, y'all"));

    QTimer waiter(main);

    // We capture by reference all the local variables because of the d.exec() 
    // below, which blocks this function until we exit. 
    int waitCount = 0;
    QObject::connect(&waiter, &QTimer::timeout, [&] () {
        waitCount++;

        if ((ezcashd->atEnd() && ezcashd->processId() == 0) ||
            ezcashd->state() == QProcess::NotRunning ||
            waitCount > 30 ||
            conn->config->zcashDaemon)  {   // If hushd is daemon, then we don't have to do anything else
            qDebug() << "Ended";
            waiter.stop();
            QTimer::singleShot(1000, [&]() { d.accept(); });
        } else {
            qDebug() << "Not ended, continuing to wait...";
        }
    });
    waiter.start(1000);

    // Wait for the hush process to exit.
    if (!Settings::getInstance()->isHeadless()) {
        d.exec(); 
    } else {
        while (waiter.isActive()) {
            QCoreApplication::processEvents();

            QThread::sleep(1);
        }
    }
}


/** 
 * Get a Sapling address from the user's wallet
 */ 
QString RPC::getDefaultSaplingAddress() {
    for (QString addr: *zaddresses) {
        if (Settings::getInstance()->isSaplingAddress(addr))
            return addr;
    }

    return QString();
}

QString RPC::getDefaultTAddress() {
    if (getAllTAddresses()->length() > 0)
        return getAllTAddresses()->at(0);
    else 
        return QString();
}
