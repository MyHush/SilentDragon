// Copyright 2019-2020 The Hush Developers
// Released under the GPLv3
#include "mainwindow.h"
#include "addressbook.h"
#include "viewalladdresses.h"
#include "validateaddress.h"
#include "ui_mainwindow.h"
#include "ui_mobileappconnector.h"
#include "ui_addressbook.h"
#include "ui_privkey.h"
#include "ui_viewkey.h"
#include "ui_about.h"
#include "ui_settings.h"
#include "ui_viewalladdresses.h"
#include "ui_validateaddress.h"
#include "rpc.h"
#include "balancestablemodel.h"
#include "settings.h"
#include "version.h"
#include "senttxstore.h"
#include "connection.h"
#include "requestdialog.h"
#include "websockets.h"

using json = nlohmann::json;

QString MainWindow::getZaddrForContact(QString contact) {
    QList<QList<QString>> addressLabels = AddressBook::getInstance()->getAllAddressLabels();
    for (int i = 0; i < addressLabels.size(); ++i) {
        QList<QString> thisContact = addressLabels.at(i);
        if (thisContact[0] == contact) {
            qDebug() << "Found contact " << thisContact[0] << " " << thisContact[1];
            return thisContact[1];
        }
    }
    return "";
}

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    // Include css
    QString theme_name;
    try
    {
       theme_name = Settings::getInstance()->get_theme_name();
    } catch (...)
    {
        theme_name = "default";
    }

    this->slot_change_theme(theme_name);

    ui->setupUi(this);
    logger = new Logger(this, QDir(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)).filePath("SilentDragon.log"));

    // Status Bar
    setupStatusBar();

    // Settings editor
    setupSettingsModal();

    // Set up actions
    QObject::connect(ui->actionExit, &QAction::triggered, this, &MainWindow::close);
    QObject::connect(ui->actionDonate, &QAction::triggered, this, &MainWindow::donate);
    QObject::connect(ui->actionDiscord, &QAction::triggered, this, &MainWindow::discord);
    QObject::connect(ui->actionReportBug, &QAction::triggered, this, &MainWindow::reportbug);
    QObject::connect(ui->actionWebsite, &QAction::triggered, this, &MainWindow::website);

    // Send button
    QObject::connect(ui->sendMemo, &QPushButton::clicked, this, &MainWindow::sendMemo);

    // Request hush
    QObject::connect(ui->actionRequest_zcash, &QAction::triggered, [=]() {
        RequestDialog::showRequestZcash(this);
    });

    // Pay Hush URI
    QObject::connect(ui->actionPay_URI, &QAction::triggered, [=] () {
        payZcashURI();
    });

    // Import Private Key
    QObject::connect(ui->actionImport_Private_Key, &QAction::triggered, this, &MainWindow::importPrivKey);

    // Export All Private Keys
    QObject::connect(ui->actionExport_All_Private_Keys, &QAction::triggered, this, &MainWindow::exportAllKeys);

    // Backup wallet.dat
    QObject::connect(ui->actionBackup_wallet_dat, &QAction::triggered, this, &MainWindow::backupWalletDat);

    // Export transactions
    QObject::connect(ui->actionExport_transactions, &QAction::triggered, this, &MainWindow::exportTransactions);

    // Validate Address
    QObject::connect(ui->actionValidate_Address, &QAction::triggered, this, &MainWindow::validateAddress);

    // Connect mobile app
    QObject::connect(ui->actionConnect_Mobile_App, &QAction::triggered, this, [=] () {
        if (rpc->getConnection() == nullptr)
            return;

        AppDataServer::getInstance()->connectAppDialog(this);
    });

    // Address Book
    QObject::connect(ui->action_Address_Book, &QAction::triggered, this, &MainWindow::addressBook);

    // Set up about action
    QObject::connect(ui->actionAbout, &QAction::triggered, [=] () {
        QDialog aboutDialog(this);
        Ui_about about;
        about.setupUi(&aboutDialog);
        Settings::saveRestore(&aboutDialog);

        QString version    = QString("Version ") % QString(APP_VERSION) % " (" % QString(__DATE__) % ")";
        about.versionLabel->setText(version);

        aboutDialog.exec();
    });

    // Initialize to the balances tab
    ui->tabWidget->setCurrentIndex(0);

    if (AppDataServer::getInstance()->isAppConnected()) {
        auto ads = AppDataServer::getInstance();

        QString wormholecode = "";
        if (ads->getAllowInternetConnection())
            wormholecode = ads->getWormholeCode(ads->getSecretHex());

        qDebug() << "MainWindow: createWebsocket with wormholecode=" << wormholecode;
        createWebsocket(wormholecode);
    }

    //TODO: allow user to set this
    ui->textEdit->setTextColor( QColor("red") );

    QItemSelectionModel* qsm = ui->chatView->selectionModel();
    QObject::connect(qsm, SIGNAL(itemSelectionChanged(const QItemSelection&, const QItemSelection&)),this, SLOT(itemSelectionChanged()));

    // Contacts and chat views should not be editable
    ui->chatView->setEditTriggers(QAbstractItemView::NoEditTriggers);
    ui->contactsView->setEditTriggers(QAbstractItemView::NoEditTriggers);

    ui->contactsView->setViewMode(QListView::ListMode);

    // This works but doesn't take into account dark theme and is unreadable
    //ui->contactsView->setAlternatingRowColors(true);

    setupSendTab();
    setupTransactionsTab();
    setupReceiveTab();
    setupBalancesTab();
    setupMarketTab();
    setupChatTab();
    setupHushTab();

    // Set up check for updates action
    QObject::connect(ui->actionCheck_for_Updates, &QAction::triggered, [=] () {
        // Silent is false, so show notification even if no update was found
        rpc->checkForUpdate(false);
    });


    rpc = new RPC(this);
    qDebug() << "Created RPC";

    restoreSavedStates();

}

QString MainWindow::createHeaderMemo(QString cid, QString zaddr, int version=0, int headerNumber=1)
{
    QString header="";
    QJsonDocument j;
    QJsonObject h;
    version  = -1; // This is unstable v=-1 until we launch with v=0
    // We use short keynames to use less space for metadata and so allow
    // the user to send more actual data in memos
    h["h"]   = headerNumber;    // header number
    h["v"]   = version;         // HushChat version
    h["z"]   = zaddr;           // zaddr to respond to
    h["cid"] = cid;             // conversation id

    j.setObject(h);
    //TODO: how do we remove newlines?
    header = j.toJson();
    qDebug() << "made header=" << header;

    return header;
}

QString MainWindow::getZaddrForCurrentContact() {
    QString zaddr;
    QModelIndex qmi = ui->contactsView->currentIndex();
    if (qmi.isValid()) {
        qDebug() << "Current (row,col) index: " << qmi.row() << "," << qmi.column();
        // we seem to get duplicates due to QT internals shenanigans, just pick the first
        QMap <int, QVariant> currentContacts = ui->contactsView->model()->itemData(qmi);
        QString contact = currentContacts[0].toString();
        qDebug() << "Current HushContact: " << contact;
        zaddr = getZaddrForContact(contact);
    } else {
        qDebug() << "Invalid current index, no contacts selected";
    }
    return zaddr;
}

QString MainWindow::getNameForCurrentContact() {
    QString name;
    QModelIndex qmi = ui->contactsView->currentIndex();
    if (qmi.isValid()) {
        qDebug() << "Current (row,col) index: " << qmi.row() << "," << qmi.column();
        // we seem to get duplicates due to QT internals shenanigans, just pick the first
        QMap <int, QVariant> currentContacts = ui->contactsView->model()->itemData(qmi);
        QString name = currentContacts[0].toString();
    } else {
        qDebug() << "Invalid current index, no contacts selected";
    }
    return name;
}

// Send button clicked
void MainWindow::sendMemo() {
    HushChat thisChat   = MainWindow::getHushChat();

    // Either we made a custom zaddr for this contact in the past, or we make a new one now
    if(thisChat.getContact().getMyZaddr().isEmpty()) {
        QString newzaddr;
        rpc->newZaddr( [=] (json reply) {
            Tx tx;
            tx.fee = Settings::getMinerFee();
            //TODO: verify we currently own the private key to this zaddr via z_validateaddress
            HushChat chat       = MainWindow::getHushChat();
            HushContact contact = chat.getContact();
            QString myZaddr     = QString::fromStdString(reply.get<json::string_t>());
            QString addr        = getZaddrForCurrentContact();
            QString name        = getNameForCurrentContact();
            qDebug() << "created new myZaddr="<< myZaddr << " for " << name;
            contact.setName(name);
            contact.setMyZaddr(myZaddr);
            contact.setZaddr(addr);


            AddressBook::getInstance()->addAddressLabel(contact.getName(), contact.getZaddr(), contact.getMyZaddr());
            qDebug() << "Wrote new myZaddr for " << contact.getName() << " to storage";

            qDebug() << "Using " << myZaddr << " as from address for " << contact.getName();
            double amount = 0;
            QString cid   = QUuid::createUuid().toString(QUuid::WithoutBraces);
            QString hmemo = createHeaderMemo(cid,myZaddr);
            QString memo  = ui->textEdit->toPlainText();

            // we send a header memo plus actual memo
            tx.toAddrs.push_back( ToFields{addr, amount, hmemo, hmemo.toUtf8().toHex()} );
            tx.toAddrs.push_back( ToFields{addr, amount, memo, memo.toUtf8().toHex()} );
            tx.fromAddr         = contact.getMyZaddr();

            qDebug() << "Sending "<< name << "(" << addr << ") a memo: " << memo;

            QString error = doSendTxValidations(tx);
            if (!error.isEmpty()) {
                // Something went wrong, so show an error and exit
                QMessageBox msg(QMessageBox::Critical, tr("Transaction Error"), error,
                                QMessageBox::Ok, this);

                msg.exec();

                // abort the Tx
                return;
            }

            // Show a dialog to confirm the Tx
            if (confirmTx(tx)) {
                // And send the Tx
                rpc->executeTransaction(tx,
                    [=] (QString opid) {
                        ui->statusBar->showMessage(tr("Computing transaction: ") % opid);
                        qDebug() << "Computing opid: " << opid;
                    },
                    [=] (QString, QString txid) { 
                        ui->statusBar->showMessage(Settings::txidStatusMessage + " " + txid);
                    },
                    [=] (QString opid, QString errStr) {
                        ui->statusBar->showMessage(QObject::tr(" Transaction ") % opid % QObject::tr(" failed"), 15 * 1000);

                        if (!opid.isEmpty())
                            errStr = QObject::tr("The transaction with id ") % opid % QObject::tr(" failed. The error was") + ":\n\n" + errStr; 

                        QMessageBox::critical(this, QObject::tr("Transaction Error"), errStr, QMessageBox::Ok);
                    }
                );
            }
     }); // newZaddr
 } else {
    // this contact already has a myZaddr
 }
}

void MainWindow::createWebsocket(QString wormholecode) {
    // Create the websocket server, for listening to direct connections
    int wsport = 8777;
    // TODO: env var
    bool msgDebug = true;
    wsserver = new WSServer(wsport, msgDebug, this);
    qDebug() << "createWebsocket: Listening for app connections on port " << wsport;

    if (!wormholecode.isEmpty()) {
        // Connect to the wormhole service
        qDebug() << "Creating WormholeClient";
        wormhole = new WormholeClient(this, wormholecode);
    }
}

void MainWindow::stopWebsocket() {
    delete wsserver;
    wsserver = nullptr;

    delete wormhole;
    wormhole = nullptr;

    qDebug() << "Websockets for app connections shut down";
}

bool MainWindow::isWebsocketListening() {
    return wsserver != nullptr;
}

void MainWindow::replaceWormholeClient(WormholeClient* newClient) {
    qDebug() << "replacing WormholeClient";
    delete wormhole;
    wormhole = newClient;
}

void MainWindow::restoreSavedStates() {
    QSettings s;
    restoreGeometry(s.value("geometry").toByteArray());

    ui->balancesTable->horizontalHeader()->restoreState(s.value("baltablegeometry").toByteArray());
    ui->transactionsTable->horizontalHeader()->restoreState(s.value("tratablegeometry").toByteArray());
}

void MainWindow::doClose() {
    closeEvent(nullptr);
}

void MainWindow::closeEvent(QCloseEvent* event) {
    QSettings s;

    s.setValue("geometry", saveGeometry());
    s.setValue("baltablegeometry", ui->balancesTable->horizontalHeader()->saveState());
    s.setValue("tratablegeometry", ui->transactionsTable->horizontalHeader()->saveState());

    s.sync();

    // Let the RPC know to shut down any running service.
    rpc->shutdownZcashd();

    // Bubble up
    if (event)
        QMainWindow::closeEvent(event);
}

void MainWindow::setupStatusBar() {
    // Status Bar
    loadingLabel = new QLabel();
    loadingMovie = new QMovie(":/icons/res/loading.gif");
    loadingMovie->setScaledSize(QSize(32, 16));
    loadingMovie->start();
    loadingLabel->setAttribute(Qt::WA_NoSystemBackground);
    loadingLabel->setMovie(loadingMovie);

    ui->statusBar->addPermanentWidget(loadingLabel);
    loadingLabel->setVisible(false);

    // Custom status bar menu
    ui->statusBar->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(ui->statusBar, &QStatusBar::customContextMenuRequested, [=](QPoint pos) {
        auto msg = ui->statusBar->currentMessage();
        QMenu menu(this);

        if (!msg.isEmpty() && msg.startsWith(Settings::txidStatusMessage)) {
            auto txid = msg.split(":")[1].trimmed();
            menu.addAction("Copy txid", [=]() {
                QGuiApplication::clipboard()->setText(txid);
            });
            menu.addAction("Copy block explorer link", [=]() {
                QString url;
                auto explorer = Settings::getInstance()->getExplorer();
                if (Settings::getInstance()->isTestnet()) {
                    url = explorer.testnetTxExplorerUrl + txid;
                } else {
                    url = explorer.txExplorerUrl + txid;
                }
                QGuiApplication::clipboard()->setText(url);
            });
            menu.addAction("View tx on block explorer", [=]() {
                QString url;
                auto explorer = Settings::getInstance()->getExplorer();
                if (Settings::getInstance()->isTestnet()) {
                    url = explorer.testnetTxExplorerUrl + txid;
                } else {
                    url = explorer.txExplorerUrl + txid;
                }
                QDesktopServices::openUrl(QUrl(url));
            });
        }

        menu.addAction("Refresh", [=]() {
            rpc->refresh(true);
        });
        QPoint gpos(mapToGlobal(pos).x(), mapToGlobal(pos).y() + this->height() - ui->statusBar->height());
        menu.exec(gpos);
    });

    statusLabel = new QLabel();
    ui->statusBar->addPermanentWidget(statusLabel);

    statusIcon = new QLabel();
    ui->statusBar->addPermanentWidget(statusIcon);
}

void MainWindow::setupSettingsModal() {
    // Set up File -> Settings action
    QObject::connect(ui->actionSettings, &QAction::triggered, [=]() {
        QDialog settingsDialog(this);
        Ui_Settings settings;
        settings.setupUi(&settingsDialog);
        Settings::saveRestore(&settingsDialog);

        // Setup save sent check box
        QObject::connect(settings.chkSaveTxs, &QCheckBox::stateChanged, [=](auto checked) {
            Settings::getInstance()->setSaveZtxs(checked);
        });

        std::string currency_name;
        try {
            currency_name = Settings::getInstance()->get_currency_name();
        } catch (const std::exception& e) {
            qDebug() << QString("Currency name exception! : ");
            currency_name = "USD";
        }

        this->slot_change_currency(currency_name);

        // Setup clear button
        QObject::connect(settings.btnClearSaved, &QCheckBox::clicked, [=]() {
            if (QMessageBox::warning(this, "Clear saved history?",
                "Shielded z-Address transactions are stored locally in your wallet, outside hushd. You may delete this saved information safely any time for your privacy.\nDo you want to delete the saved shielded transactions now?",
                QMessageBox::Yes, QMessageBox::Cancel)) {
                    SentTxStore::deleteHistory();
                    // Reload after the clear button so existing txs disappear
                    rpc->refresh(true);
            }
        });

        int theme_index = settings.comboBoxTheme->findText(Settings::getInstance()->get_theme_name(), Qt::MatchExactly);
        settings.comboBoxTheme->setCurrentIndex(theme_index);

        QObject::connect(settings.comboBoxTheme, &QComboBox::currentTextChanged, [=] (QString theme_name) {
            this->slot_change_theme(theme_name);
            QMessageBox::information(this, tr("Theme Change"), tr("This change can take a few seconds."), QMessageBox::Ok);
        });

        // Set local currency
        QString ticker = QString::fromStdString( Settings::getInstance()->get_currency_name() );
        int currency_index = settings.comboBoxCurrency->findText(ticker, Qt::MatchExactly);
        settings.comboBoxCurrency->setCurrentIndex(currency_index);
        QObject::connect(settings.comboBoxCurrency, &QComboBox::currentTextChanged, [=] (QString ticker) {
            this->slot_change_currency(ticker.toStdString());
            rpc->refresh(true);
            QMessageBox::information(this, tr("Currency Change"), tr("This change can take a few seconds."), QMessageBox::Ok);
        });

        // Save sent transactions
        settings.chkSaveTxs->setChecked(Settings::getInstance()->getSaveZtxs());

        // Custom fees
        settings.chkCustomFees->setChecked(Settings::getInstance()->getAllowCustomFees());

        // Auto shielding
        settings.chkAutoShield->setChecked(Settings::getInstance()->getAutoShield());

        // Check for updates
        settings.chkCheckUpdates->setChecked(Settings::getInstance()->getCheckForUpdates());

        // Fetch prices
        settings.chkFetchPrices->setChecked(Settings::getInstance()->getAllowFetchPrices());

        // Use Tor
        bool isUsingTor = false;
        if (rpc->getConnection() != nullptr) {
            isUsingTor = !rpc->getConnection()->config->proxy.isEmpty();
        }
        settings.chkTor->setChecked(isUsingTor);
        if (rpc->getEZcashD() == nullptr) {
            settings.chkTor->setEnabled(false);
            settings.lblTor->setEnabled(false);
            QString tooltip = tr("Tor configuration is available only when running an embedded hushd.");
            settings.chkTor->setToolTip(tooltip);
            settings.lblTor->setToolTip(tooltip);
        }

        //Use Consolidation

        bool isUsingConsolidation = false;
        int size = 0;
        QDir zcashdir(rpc->getConnection()->config->zcashDir);
        QFile WalletSize(zcashdir.filePath("wallet.dat"));
        if (WalletSize.open(QIODevice::ReadOnly)){
        size = WalletSize.size() / 1000000;  //when file does open.
        //QString size1 = QString::number(size) ;
        settings.WalletSize->setText(QString::number(size));
        WalletSize.close();
        } 
        if (rpc->getConnection() != nullptr) {
            isUsingConsolidation = !rpc->getConnection()->config->consolidation.isEmpty() == true;
        }
        settings.chkConso->setChecked(isUsingConsolidation);
        if (rpc->getEZcashD() == nullptr) {
            settings.chkConso->setEnabled(false);
        }

         //Use Deletetx

        bool isUsingDeletetx = false;
        if (rpc->getConnection() != nullptr) {
            isUsingDeletetx = !rpc->getConnection()->config->deletetx.isEmpty() == true;
        }
        settings.chkDeletetx->setChecked(isUsingDeletetx);
        if (rpc->getEZcashD() == nullptr) {
            settings.chkDeletetx->setEnabled(false);
        }

          //Use Zindex

        bool isUsingZindex = false;
        if (rpc->getConnection() != nullptr) {
            isUsingZindex = !rpc->getConnection()->config->zindex.isEmpty() == true;
        }
        settings.chkzindex->setChecked(isUsingZindex);
        if (rpc->getEZcashD() == nullptr) {
            settings.chkzindex->setEnabled(false);      
        }

        // Connection Settings
        QIntValidator validator(0, 65535);
        settings.port->setValidator(&validator);

        // If values are coming from HUSH3.conf, then disable all the fields
        auto zcashConfLocation = Settings::getInstance()->getZcashdConfLocation();
        if (!zcashConfLocation.isEmpty()) {
            settings.confMsg->setText("Settings are being read from \n" + zcashConfLocation);
            settings.hostname->setEnabled(false);
            settings.port->setEnabled(false);
            settings.rpcuser->setEnabled(false);
            settings.rpcpassword->setEnabled(false);         
        } else {
            settings.confMsg->setText("No local HUSH3.conf found. Please configure connection manually.");
            settings.hostname->setEnabled(true);
            settings.port->setEnabled(true);
            settings.rpcuser->setEnabled(true);
            settings.rpcpassword->setEnabled(true);     
        }

        // Load current values into the dialog
        auto conf = Settings::getInstance()->getSettings();
        settings.hostname->setText(conf.host);
        settings.port->setText(conf.port);
        settings.rpcuser->setText(conf.rpcuser);
        settings.rpcpassword->setText(conf.rpcpassword);

        // Load current explorer values into the dialog
        auto explorer = Settings::getInstance()->getExplorer();
        settings.txExplorerUrl->setText(explorer.txExplorerUrl);
        settings.addressExplorerUrl->setText(explorer.addressExplorerUrl);
        settings.testnetTxExplorerUrl->setText(explorer.testnetTxExplorerUrl);
        settings.testnetAddressExplorerUrl->setText(explorer.testnetAddressExplorerUrl);

        // Connection tab by default
        settings.tabWidget->setCurrentIndex(0);

        // Enable the troubleshooting options only if using embedded hushd
        if (!rpc->isEmbedded()) {
            settings.chkRescan->setEnabled(false);
            settings.chkRescan->setToolTip(tr("You're using an external hushd. Please restart hushd with -rescan"));

            settings.chkReindex->setEnabled(false);
            settings.chkReindex->setToolTip(tr("You're using an external hushd. Please restart hushd with -reindex"));
        }

        if (settingsDialog.exec() == QDialog::Accepted) {
            qDebug() << "Setting dialog box accepted";
            // Custom fees
            bool customFees = settings.chkCustomFees->isChecked();
            Settings::getInstance()->setAllowCustomFees(customFees);
            ui->minerFeeAmt->setReadOnly(!customFees);
            if (!customFees)
                ui->minerFeeAmt->setText(Settings::getDecimalString(Settings::getMinerFee()));

            // Auto shield
            Settings::getInstance()->setAutoShield(settings.chkAutoShield->isChecked());

            // Check for updates
            Settings::getInstance()->setCheckForUpdates(settings.chkCheckUpdates->isChecked());

            // Allow fetching prices
            Settings::getInstance()->setAllowFetchPrices(settings.chkFetchPrices->isChecked());

            if (!isUsingTor && settings.chkTor->isChecked()) {
                // If "use tor" was previously unchecked and now checked
                Settings::addToZcashConf(zcashConfLocation, "proxy=127.0.0.1:9050");
                rpc->getConnection()->config->proxy = "proxy=127.0.0.1:9050";

                QMessageBox::information(this, tr("Enable Tor"),
                    tr("Connection over Tor has been enabled. To use this feature, you need to restart SilentDragon."),
                    QMessageBox::Ok);
            }

            if (isUsingTor && !settings.chkTor->isChecked()) {
                // If "use tor" was previously checked and now is unchecked
                Settings::removeFromZcashConf(zcashConfLocation, "proxy");
                rpc->getConnection()->config->proxy.clear();

                QMessageBox::information(this, tr("Disable Tor"),
                    tr("Connection over Tor has been disabled. To fully disconnect from Tor, you need to restart SilentDragon."),
                    QMessageBox::Ok);
            }

            if (zcashConfLocation.isEmpty()) {
                // Save settings
                Settings::getInstance()->saveSettings(
                    settings.hostname->text(),
                    settings.port->text(),
                    settings.rpcuser->text(),
                    settings.rpcpassword->text());

                auto cl = new ConnectionLoader(this, rpc);
                cl->loadConnection();
            }

            // Save explorer
            Settings::getInstance()->saveExplorer(
                settings.txExplorerUrl->text(),
                settings.addressExplorerUrl->text(),
                settings.testnetTxExplorerUrl->text(),
                settings.testnetAddressExplorerUrl->text());

            // Check to see if rescan or reindex have been enabled
            bool showRestartInfo = false;
            bool showReindexInfo = false;
            if (settings.chkRescan->isChecked()) {
                Settings::addToZcashConf(zcashConfLocation, "rescan=1");
                showRestartInfo = true;
            }

            if (settings.chkReindex->isChecked()) {
                Settings::addToZcashConf(zcashConfLocation, "reindex=1");
                showRestartInfo = true;
            }

             if (!rpc->getConnection()->config->consolidation.isEmpty()==false) {
                 if (settings.chkConso->isChecked()) {
                 Settings::addToZcashConf(zcashConfLocation, "consolidation=1");
                showRestartInfo = true;       
                }
            }

            if (!rpc->getConnection()->config->consolidation.isEmpty()) {
                 if (settings.chkConso->isChecked() == false) {
                 Settings::removeFromZcashConf(zcashConfLocation, "consolidation");
                showRestartInfo = true;       
                 }
            }
                  
             if (!rpc->getConnection()->config->deletetx.isEmpty() == false) {
                 if (settings.chkDeletetx->isChecked()) {
                 Settings::addToZcashConf(zcashConfLocation, "deletetx=1");
                showRestartInfo = true;       
                }
            }

             if (!rpc->getConnection()->config->deletetx.isEmpty()) {
                 if (settings.chkDeletetx->isChecked() == false) {
                 Settings::removeFromZcashConf(zcashConfLocation, "deletetx");
                showRestartInfo = true;
                 }
            }
    
             if (!rpc->getConnection()->config->zindex.isEmpty() == false) {
                 if (settings.chkzindex->isChecked()) {
                 Settings::addToZcashConf(zcashConfLocation, "zindex=1");
                 Settings::addToZcashConf(zcashConfLocation, "reindex=1");
                showReindexInfo = true;       
                }
            }

             if (!rpc->getConnection()->config->zindex.isEmpty()) {
                 if (settings.chkzindex->isChecked() == false) {
                 Settings::removeFromZcashConf(zcashConfLocation, "zindex");
                 Settings::addToZcashConf(zcashConfLocation, "reindex=1");
                showReindexInfo = true;       
                 }
            }
        
            if (showRestartInfo) {
                auto desc = tr("SilentDragon needs to restart to rescan,reindex,consolidation or deletetx. SilentDragon will now close, please restart SilentDragon to continue");

                QMessageBox::information(this, tr("Restart SilentDragon"), desc, QMessageBox::Ok);
                QTimer::singleShot(1, [=]() { this->close(); });
            }
            if (showReindexInfo) {
                auto desc = tr("SilentDragon needs to reindex for zindex. SilentDragon will now close, please restart SilentDragon to continue");

                QMessageBox::information(this, tr("Restart SilentDragon"), desc, QMessageBox::Ok);
                QTimer::singleShot(1, [=]() { this->close(); });
            }
        }
    });
}

void MainWindow::addressBook() {
    // Check to see if there is a target.
    QRegularExpression re("Address[0-9]+", QRegularExpression::CaseInsensitiveOption);
    for (auto target: ui->sendToWidgets->findChildren<QLineEdit *>(re)) {
        if (target->hasFocus()) {
            AddressBook::open(this, target);
            return;
        }
    };

    // If there was no target, then just run with no target.
    AddressBook::open(this);
}

void MainWindow::discord() {
    QString url = "https://myhush.org/discord/";
    QDesktopServices::openUrl(QUrl(url));
}

void MainWindow::reportbug() {
    QString url = "https://github.com/MyHush/SilentDragon/issues/new";
    QDesktopServices::openUrl(QUrl(url));
}

void MainWindow::website() {
    QString url = "https://myhush.org";
    QDesktopServices::openUrl(QUrl(url));
}

void MainWindow::donate() {
    removeExtraAddresses();

    ui->Address1->setText(Settings::getDonationAddr());
    ui->Address1->setCursorPosition(0);
    ui->Amount1->setText("0.00");
    ui->MemoTxt1->setText(tr("Some feedback about SilentDragon or Hush..."));

    ui->statusBar->showMessage(tr("Send Duke some private and shielded feedback about ") % Settings::getTokenName() % tr(" or SilentDragon"));

    // And switch to the send tab.
    ui->tabWidget->setCurrentIndex(1);
}

/**
 * Validate an address
 */
void MainWindow::validateAddress() {
    // Make sure everything is up and running
    if (!getRPC() || !getRPC()->getConnection())
        return;

    // First thing is ask the user for an address
    bool ok;
    auto address = QInputDialog::getText(this, tr("Enter Address to validate"),
        tr("Transparent or Shielded Address:") + QString(" ").repeated(140),    // Pad the label so the dialog box is wide enough
        QLineEdit::Normal, "", &ok);
    if (!ok)
        return;

    getRPC()->validateAddress(address, [=] (json props) {
        QDialog d(this);
        Ui_ValidateAddress va;
        va.setupUi(&d);
        Settings::saveRestore(&d);
        Settings::saveRestoreTableHeader(va.tblProps, &d, "validateaddressprops");
        va.tblProps->horizontalHeader()->setStretchLastSection(true);

        va.lblAddress->setText(address);

        QList<QPair<QString, QString>> propsList;
        for (auto it = props.begin(); it != props.end(); it++) {

            propsList.append(
                QPair<QString, QString>(
                    QString::fromStdString(it.key()), QString::fromStdString(it.value().dump()))
            );
        }

        ValidateAddressesModel model(va.tblProps, propsList);
        va.tblProps->setModel(&model);

        d.exec();
    });

}

void MainWindow::doImport(QList<QString>* keys) {
    if (rpc->getConnection() == nullptr) {
        // No connection, just return
        return;
    }

    if (keys->isEmpty()) {
        delete keys;
        ui->statusBar->showMessage(tr("Private key import rescan finished"));
        return;
    }

    // Pop the first key
    QString key = keys->first();
    keys->pop_front();
    bool rescan = keys->isEmpty();

    if (key.startsWith("SK") ||
        key.startsWith("secret")) { // Z key
        rpc->importZPrivKey(key, rescan, [=] (auto) { this->doImport(keys); });
    } else {
        rpc->importTPrivKey(key, rescan, [=] (auto) { this->doImport(keys); });
    }
}


// Callback invoked when the RPC has finished loading all the balances, and the UI
// is now ready to send transactions.
void MainWindow::balancesReady() {
    // First-time check
    if (uiPaymentsReady)
        return;

    uiPaymentsReady = true;
    qDebug() << "Payment UI now ready!";

    // There is a pending URI payment (from the command line, or from a secondary instance),
    // process it.
    if (!pendingURIPayment.isEmpty()) {
        qDebug() << "Paying hush URI";
        payZcashURI(pendingURIPayment);
        pendingURIPayment = "";
    }

}

bool MainWindow::eventFilter(QObject *object, QEvent *event) {
    // Event filter for MacOS specific handling of payment URIs
    if (event->type() == QEvent::FileOpen) {
        QFileOpenEvent *fileEvent = static_cast<QFileOpenEvent*>(event);
        if (!fileEvent->url().isEmpty())
            payZcashURI(fileEvent->url().toString());

        return true;
    } else if (event->type() == QEvent::MouseButtonPress) {
        qDebug() << __func__ <<": "<<" mouse button event on " << object->objectName();
        QMouseEvent *ev = static_cast<QMouseEvent *>(event);
        if (ev->buttons() & Qt::RightButton)
        {
            //qDebug()<< "RightButton clicked";
        }
        if (ev->buttons() & Qt::LeftButton)
        {
            //qDebug()<< "LeftButton clicked";
            //TODO: if this was a HushContact object in chatView, update MainWindow::contact
        }
        //return false;
    }

    return QObject::eventFilter(object, event);
}


// Pay the Hush URI by showing a confirmation window. If the URI parameter is empty, the UI
// will prompt for one. If the myAddr is empty, then the default from address is used to send
// the transaction.
void MainWindow::payZcashURI(QString uri, QString myAddr) {
    // If the Payments UI is not ready (i.e, all balances have not loaded), defer the payment URI
    if (!uiPaymentsReady) {
        qDebug() << "Payment UI not ready, waiting for UI to pay URI";
        pendingURIPayment = uri;
        return;
    }

    // If there was no URI passed, ask the user for one.
    if (uri.isEmpty()) {
        uri = QInputDialog::getText(this, tr("Paste HUSH URI"),
            "HUSH URI" + QString(" ").repeated(180));
    }

    // If there's no URI, just exit
    if (uri.isEmpty())
        return;

    // Extract the address
    qDebug() << "Received URI " << uri;
    PaymentURI paymentInfo = Settings::parseURI(uri);
    if (!paymentInfo.error.isEmpty()) {
        QMessageBox::critical(this, tr("Error paying Hush URI"),
                tr("URI should be of the form 'hush:<addr>?amt=x&memo=y") + "\n" + paymentInfo.error);
        return;
    }

    // Now, set the fields on the send tab
    removeExtraAddresses();
    if (!myAddr.isEmpty()) {
        ui->inputsCombo->setCurrentText(myAddr);
    }

    ui->Address1->setText(paymentInfo.addr);
    ui->Address1->setCursorPosition(0);
    ui->Amount1->setText(Settings::getDecimalString(paymentInfo.amt.toDouble()));
    ui->MemoTxt1->setText(paymentInfo.memo);

    // And switch to the send tab.
    ui->tabWidget->setCurrentIndex(1);
    raise();

    // And click the send button if the amount is > 0, to validate everything. If everything is OK, it will show the confirm box
    // else, show the error message;
    if (paymentInfo.amt > 0) {
        sendButton();
    }
}


void MainWindow::importPrivKey() {
    QDialog d(this);
    Ui_PrivKey pui;
    pui.setupUi(&d);
    Settings::saveRestore(&d);

    pui.buttonBox->button(QDialogButtonBox::Save)->setVisible(false);
    pui.helpLbl->setText(QString() %
                        tr("Please paste your private keys here, one per line") % ".\n" %
                        tr("The keys will be imported into your connected Hush node"));

    if (d.exec() == QDialog::Accepted && !pui.privKeyTxt->toPlainText().trimmed().isEmpty()) {
        auto rawkeys = pui.privKeyTxt->toPlainText().trimmed().split("\n");

        QList<QString> keysTmp;
        // Filter out all the empty keys.
        std::copy_if(rawkeys.begin(), rawkeys.end(), std::back_inserter(keysTmp), [=] (auto key) {
            return !key.startsWith("#") && !key.trimmed().isEmpty();
        });

        auto keys = new QList<QString>();
        std::transform(keysTmp.begin(), keysTmp.end(), std::back_inserter(*keys), [=](auto key) {
            return key.trimmed().split(" ")[0];
        });

        // Special case.
        // Sometimes, when importing from a paperwallet or such, the key is split by newlines, and might have
        // been pasted like that. So check to see if the whole thing is one big private key
        if (Settings::getInstance()->isValidSaplingPrivateKey(keys->join(""))) {
            auto multiline = keys;
            keys = new QList<QString>();
            keys->append(multiline->join(""));
            delete multiline;
        }

        // Start the import. The function takes ownership of keys
        QTimer::singleShot(1, [=]() {doImport(keys);});

        // Show the dialog that keys will be imported.
        QMessageBox::information(this,
            "Imported", tr("The keys were imported! It may take several minutes to rescan the blockchain. Until then, functionality may be limited"),
            QMessageBox::Ok);
    }
}

/**
 * Export transaction history into a CSV file
 */
void MainWindow::exportTransactions() {
    // First, get the export file name
    QString exportName = "hush-transactions-" + QDateTime::currentDateTime().toString("yyyyMMdd") + ".csv";

    QUrl csvName = QFileDialog::getSaveFileUrl(this,
            tr("Export transactions"), exportName, "CSV file (*.csv)");

    if (csvName.isEmpty())
        return;

    if (!rpc->getTransactionsModel()->exportToCsv(csvName.toLocalFile())) {
        QMessageBox::critical(this, tr("Error"),
            tr("Error exporting transactions, file was not saved"), QMessageBox::Ok);
    }
}

/**
 * Backup the wallet.dat file. This is kind of a hack, since it has to read from the filesystem rather than an RPC call
 * This might fail for various reasons - Remote hushd, non-standard locations, custom params passed to hushd, many others
*/
void MainWindow::backupWalletDat() {
    if (!rpc->getConnection())
        return;

    QDir zcashdir(rpc->getConnection()->config->zcashDir);
    QString backupDefaultName = "hush-wallet-backup-" + QDateTime::currentDateTime().toString("yyyyMMdd") + ".dat";

    if (Settings::getInstance()->isTestnet()) {
        zcashdir.cd("testnet3");
        backupDefaultName = "testnet-" + backupDefaultName;
    }

    QFile wallet(zcashdir.filePath("wallet.dat"));
    if (!wallet.exists()) {
        QMessageBox::critical(this, tr("No wallet.dat"), tr("Couldn't find the wallet.dat on this computer") + "\n" +
            tr("You need to back it up from the machine hushd is running on"), QMessageBox::Ok);
        return;
    }

    QUrl backupName = QFileDialog::getSaveFileUrl(this, tr("Backup wallet.dat"), backupDefaultName, "Data file (*.dat)");
    if (backupName.isEmpty())
        return;

    if (!wallet.copy(backupName.toLocalFile())) {
        QMessageBox::critical(this, tr("Couldn't backup"), tr("Couldn't backup the wallet.dat file.") +
            tr("You need to back it up manually."), QMessageBox::Ok);
    }
}

void MainWindow::exportAllKeys() {
    exportKeys("");
}

void MainWindow::getViewKey(QString addr) {
    QDialog d(this);
    Ui_ViewKey vui;
    vui.setupUi(&d);

    // Make the window big by default
    auto ps = this->geometry();
    QMargins margin = QMargins() + 50;
    d.setGeometry(ps.marginsRemoved(margin));

    Settings::saveRestore(&d);

    vui.viewKeyTxt->setPlainText(tr("Loading..."));
    vui.viewKeyTxt->setReadOnly(true);
    vui.viewKeyTxt->setLineWrapMode(QPlainTextEdit::LineWrapMode::NoWrap);

    // Disable the save button until it finishes loading
    vui.buttonBox->button(QDialogButtonBox::Save)->setEnabled(false);
    vui.buttonBox->button(QDialogButtonBox::Ok)->setVisible(false);

    bool allKeys = false; //addr.isEmpty() ? true : false;
    // Wire up save button
    QObject::connect(vui.buttonBox->button(QDialogButtonBox::Save), &QPushButton::clicked, [=] () {
        QString fileName = QFileDialog::getSaveFileName(this, tr("Save File"),
                           allKeys ? "hush-all-viewkeys.txt" : "hush-viewkey.txt");
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly)) {
            QMessageBox::information(this, tr("Unable to open file"), file.errorString());
            return;
        }
        QTextStream out(&file);
        // TODO: Output in address, viewkey CSV format?
        out << vui.viewKeyTxt->toPlainText();
    });

    auto isDialogAlive = std::make_shared<bool>(true);

    auto fnUpdateUIWithKeys = [=](QList<QPair<QString, QString>> viewKeys) {
        // Check to see if we are still showing.
        if (! *(isDialogAlive.get()) ) return;

        QString allKeysTxt;
        for (auto keypair : viewKeys) {
            allKeysTxt = allKeysTxt % keypair.second % " # addr=" % keypair.first % "\n";
        }

        vui.viewKeyTxt->setPlainText(allKeysTxt);
        vui.buttonBox->button(QDialogButtonBox::Save)->setEnabled(true);
    };

    auto fnAddKey = [=](json key) {
        QList<QPair<QString, QString>> singleAddrKey;
        singleAddrKey.push_back(QPair<QString, QString>(addr, QString::fromStdString(key.get<json::string_t>())));
        fnUpdateUIWithKeys(singleAddrKey);
    };

    rpc->getZViewKey(addr, fnAddKey);

    d.exec();
    *isDialogAlive = false;
}

void MainWindow::exportKeys(QString addr) {
    bool allKeys = addr.isEmpty() ? true : false;

    QDialog d(this);
    Ui_PrivKey pui;
    pui.setupUi(&d);

    // Make the window big by default
    auto ps = this->geometry();
    QMargins margin = QMargins() + 50;
    d.setGeometry(ps.marginsRemoved(margin));

    Settings::saveRestore(&d);

    pui.privKeyTxt->setPlainText(tr("Loading..."));
    pui.privKeyTxt->setReadOnly(true);
    pui.privKeyTxt->setLineWrapMode(QPlainTextEdit::LineWrapMode::NoWrap);

    if (allKeys)
        pui.helpLbl->setText(tr("These are all the private keys for all the addresses in your wallet"));
    else
        pui.helpLbl->setText(tr("Private key for ") + addr);

    // Disable the save button until it finishes loading
    pui.buttonBox->button(QDialogButtonBox::Save)->setEnabled(false);
    pui.buttonBox->button(QDialogButtonBox::Ok)->setVisible(false);

    // Wire up save button
    QObject::connect(pui.buttonBox->button(QDialogButtonBox::Save), &QPushButton::clicked, [=] () {
        QString fileName = QFileDialog::getSaveFileName(this, tr("Save File"),
                           allKeys ? "hush-all-privatekeys.txt" : "hush-privatekey.txt");
        QFile file(fileName);
        if (!file.open(QIODevice::WriteOnly)) {
            QMessageBox::information(this, tr("Unable to open file"), file.errorString());
            return;
        }
        QTextStream out(&file);
        out << pui.privKeyTxt->toPlainText();
    });

    // Call the API
    auto isDialogAlive = std::make_shared<bool>(true);

    auto fnUpdateUIWithKeys = [=](QList<QPair<QString, QString>> privKeys) {
        // Check to see if we are still showing.
        if (! *(isDialogAlive.get()) ) return;

        QString allKeysTxt;
        for (auto keypair : privKeys) {
            allKeysTxt = allKeysTxt % keypair.second % " # addr=" % keypair.first % "\n";
        }

        pui.privKeyTxt->setPlainText(allKeysTxt);
        pui.buttonBox->button(QDialogButtonBox::Save)->setEnabled(true);
    };

    if (allKeys) {
        rpc->getAllPrivKeys(fnUpdateUIWithKeys);
    }
    else {
        auto fnAddKey = [=](json key) {
            QList<QPair<QString, QString>> singleAddrKey;
            singleAddrKey.push_back(QPair<QString, QString>(addr, QString::fromStdString(key.get<json::string_t>())));
            fnUpdateUIWithKeys(singleAddrKey);
        };

        if (Settings::getInstance()->isZAddress(addr)) {
            rpc->getZPrivKey(addr, fnAddKey);
        } else {
            rpc->getTPrivKey(addr, fnAddKey);
        }
    }

    d.exec();
    *isDialogAlive = false;
}

void MainWindow::setupBalancesTab() {
    ui->unconfirmedWarning->setVisible(false);

    // Double click on balances table
    auto fnDoSendFrom = [=](const QString& addr, const QString& to = QString(), bool sendMax = false) {
        // Find the inputs combo
        for (int i = 0; i < ui->inputsCombo->count(); i++) {
            auto inputComboAddress = ui->inputsCombo->itemText(i);
            if (inputComboAddress.startsWith(addr)) {
                ui->inputsCombo->setCurrentIndex(i);
                break;
            }
        }

        // If there's a to address, add that as well
        if (!to.isEmpty()) {
            // Remember to clear any existing address fields, because we are creating a new transaction.
            this->removeExtraAddresses();
            ui->Address1->setText(to);
        }

        // See if max button has to be checked
        if (sendMax) {
            ui->Max1->setChecked(true);
        }

        // And switch to the send tab.
        ui->tabWidget->setCurrentIndex(1);
    };

    // Double click opens up memo if one exists
    QObject::connect(ui->balancesTable, &QTableView::doubleClicked, [=](auto index) {
        index = index.sibling(index.row(), 0);
        auto addr = AddressBook::addressFromAddressLabel(ui->balancesTable->model()->data(index).toString());

        fnDoSendFrom(addr);
    });

    // Setup context menu on balances tab
    ui->balancesTable->setContextMenuPolicy(Qt::CustomContextMenu);
    QObject::connect(ui->balancesTable, &QTableView::customContextMenuRequested, [=] (QPoint pos) {
        QModelIndex index = ui->balancesTable->indexAt(pos);
        if (index.row() < 0) return;

        index = index.sibling(index.row(), 0);
        auto addr = AddressBook::addressFromAddressLabel(
                            ui->balancesTable->model()->data(index).toString());

        QMenu menu(this);

        menu.addAction(tr("Copy address"), [=] () {
            QClipboard *clipboard = QGuiApplication::clipboard();
            clipboard->setText(addr);
            ui->statusBar->showMessage(tr("Copied to clipboard"), 3 * 1000);
        });

        menu.addAction(tr("Get private key"), [=] () {
            this->exportKeys(addr);
        });

        if (addr.startsWith("zs1")) {
            menu.addAction(tr("Get viewing key"), [=] () {
                this->getViewKey(addr);
            });
        }

        menu.addAction("Send from " % addr.left(40) % (addr.size() > 40 ? "..." : ""), [=]() {
            fnDoSendFrom(addr);
        });

        menu.addAction("Send to " % addr.left(40) % (addr.size() > 40 ? "..." : ""), [=]() {
            fnDoSendFrom("",addr);
        });

        if (addr.startsWith("R")) {
            auto defaultSapling = rpc->getDefaultSaplingAddress();
            if (!defaultSapling.isEmpty()) {
                menu.addAction(tr("Shield balance to Sapling"), [=] () {
                    fnDoSendFrom(addr, defaultSapling, true);
                });
            }

            menu.addAction(tr("View on block explorer"), [=] () {
                QString url;
                auto explorer = Settings::getInstance()->getExplorer();
                if (Settings::getInstance()->isTestnet()) {
                    url = explorer.testnetAddressExplorerUrl + addr;
                } else {
                    url = explorer.addressExplorerUrl + addr;
                }
                QDesktopServices::openUrl(QUrl(url));
            });

            menu.addAction("Copy explorer link", [=]() {
                QString url;
                auto explorer = Settings::getInstance()->getExplorer();
                if (Settings::getInstance()->isTestnet()) {
                    url = explorer.testnetAddressExplorerUrl + addr;
                } else {
                    url = explorer.addressExplorerUrl + addr;
                }
                QGuiApplication::clipboard()->setText(url);
            });

            menu.addAction(tr("Address Asset Viewer"), [=] () {
                QString url;
                url = "https://dexstats.info/assetviewer.php?address=" + addr;
                QDesktopServices::openUrl(QUrl(url));
            });

            menu.addAction(tr("Convert Address"), [=] () {
                QString url;
                url = "https://dexstats.info/addressconverter.php?fromcoin=HUSH3&address=" + addr;
                QDesktopServices::openUrl(QUrl(url));
            });
        }

        menu.exec(ui->balancesTable->viewport()->mapToGlobal(pos));
    });
}

void MainWindow::setupHushTab() {
    ui->hushlogo->setBasePixmap(QPixmap(":/img/res/zcashdlogo.gif"));
}

void MainWindow::setupChatTab() {
    qDebug() << __FUNCTION__;
    QList<QList<QString>> addressLabels = AddressBook::getInstance()->getAllAddressLabels();
    QStringListModel *chatModel = new QStringListModel();
    QStringList contacts;
    //contacts << "Alice" << "Bob" << "Charlie" << "Eve";
    for (int i = 0; i < addressLabels.size(); ++i) {
        QList<QString> thisContact = addressLabels.at(i);
        qDebug() << "Found contact " << thisContact[0] << " " << thisContact[1];
        contacts << thisContact[0];
    }

    chatModel->setStringList(contacts);

    QStringListModel *conversationModel = new QStringListModel();
    QStringList conversations;
    conversations << "The Bitcoin network might actually reduce spam by diverting zombie farms to generating bitcoins instead.";
    conversationModel->setStringList(conversations);

    //conversationModel[0].setItemAlignment(Qt::AlignRight);

    // iterate on all elements in chat view and set alignment
    // ui->chatView->


    //Ui_addressBook ab;
    //AddressBookModel model(ab.addresses);
    //ab.addresses->setModel(&model);

    //TODO: ui->contactsView->setModel( model of address book );
    //ui->contactsView->setModel(&model );

    ui->contactsView->setModel(chatModel);
    ui->chatView->setModel( conversationModel );
    ui->chatGridLayout->setColumnStretch(1,1);
    ui->chatGridLayout->setRowStretch(1,2);
}

void MainWindow::setupMarketTab() {
    qDebug() << "Setting up market tab";
    auto s      = Settings::getInstance();
    auto ticker = s->get_currency_name();

    ui->volume->setText(QString::number((double)       s->get_volume("HUSH") ,'f',8) + " HUSH");
    ui->volumeLocal->setText(QString::number((double)  s->get_volume(ticker) ,'f',8) + " " + QString::fromStdString(ticker));
    ui->volumeBTC->setText(QString::number((double)    s->get_volume("BTC") ,'f',8) + " BTC");
}

void MainWindow::setupTransactionsTab() {
    // Double click opens up memo if one exists
    QObject::connect(ui->transactionsTable, &QTableView::doubleClicked, [=] (auto index) {
        auto txModel = dynamic_cast<TxTableModel *>(ui->transactionsTable->model());
        QString memo = txModel->getMemo(index.row());

        if (!memo.isEmpty()) {
            QMessageBox mb(QMessageBox::Information, tr("Memo"), memo, QMessageBox::Ok, this);
            mb.setTextFormat(Qt::PlainText);
            mb.setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
            mb.exec();
        }
    });

    // Set up context menu on transactions tab
    ui->transactionsTable->setContextMenuPolicy(Qt::CustomContextMenu);

    // Table right click
    QObject::connect(ui->transactionsTable, &QTableView::customContextMenuRequested, [=] (QPoint pos) {
        QModelIndex index = ui->transactionsTable->indexAt(pos);
        if (index.row() < 0) return;

        QMenu menu(this);

        auto txModel = dynamic_cast<TxTableModel *>(ui->transactionsTable->model());

        QString txid = txModel->getTxId(index.row());
        QString memo = txModel->getMemo(index.row());
        QString addr = txModel->getAddr(index.row());

        menu.addAction(tr("Copy txid"), [=] () {
            QGuiApplication::clipboard()->setText(txid);
            ui->statusBar->showMessage(tr("Copied to clipboard"), 3 * 1000);
        });

        if (!addr.isEmpty()) {
            menu.addAction(tr("Copy address"), [=] () {
                QGuiApplication::clipboard()->setText(addr);
                ui->statusBar->showMessage(tr("Copied to clipboard"), 3 * 1000);
            });
        }

        menu.addAction(tr("View on block explorer"), [=] () {
            QString url;
            auto explorer = Settings::getInstance()->getExplorer();
            if (Settings::getInstance()->isTestnet()) {
                url = explorer.testnetTxExplorerUrl + txid;
            } else {
                url = explorer.txExplorerUrl + txid;
            }
            QDesktopServices::openUrl(QUrl(url));
        });

        menu.addAction(tr("Copy block explorer link"), [=] () {
            QString url;
            auto explorer = Settings::getInstance()->getExplorer();
            if (Settings::getInstance()->isTestnet()) {
                url = explorer.testnetTxExplorerUrl + txid;
            } else {
                url = explorer.txExplorerUrl + txid;
            }
            QGuiApplication::clipboard()->setText(url);
        });

        // Payment Request
        if (!memo.isEmpty() && memo.startsWith("hush:")) {
            menu.addAction(tr("View Payment Request"), [=] () {
                RequestDialog::showPaymentConfirmation(this, memo);
            });
        }

        // View Memo
        if (!memo.isEmpty()) {
            menu.addAction(tr("View Memo"), [=] () {
                QMessageBox mb(QMessageBox::Information, tr("Memo"), memo, QMessageBox::Ok, this);
                mb.setTextFormat(Qt::PlainText);
                mb.setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
                mb.exec();
            });
        }

        // If memo contains a reply to address, add a "Reply to" menu item
        if (!memo.isEmpty()) {
            int lastPost     = memo.trimmed().lastIndexOf(QRegExp("[\r\n]+"));
            QString lastWord = memo.right(memo.length() - lastPost - 1);

            if (Settings::getInstance()->isSaplingAddress(lastWord)) {
                menu.addAction(tr("Reply to ") + lastWord.left(25) + "...", [=]() {
                    // First, cancel any pending stuff in the send tab by pretending to click
                    // the cancel button
                    cancelButton();

                    // Then set up the fields in the send tab
                    ui->Address1->setText(lastWord);
                    ui->Address1->setCursorPosition(0);
                    ui->Amount1->setText("0.0001");

                    // And switch to the send tab.
                    ui->tabWidget->setCurrentIndex(1);

                    qApp->processEvents();

                    // Click the memo button
                    this->memoButtonClicked(1, true);
                });
            }
        }

        menu.exec(ui->transactionsTable->viewport()->mapToGlobal(pos));
    });
}

void MainWindow::addNewZaddr() {
    rpc->newZaddr( [=] (json reply) {
        QString addr = QString::fromStdString(reply.get<json::string_t>());
        // Make sure the RPC class reloads the z-addrs for future use
        rpc->refreshAddresses();

        // Just double make sure the z-address is still checked
        if ( ui->rdioZSAddr->isChecked() ) {
            ui->listReceiveAddresses->insertItem(0, addr);
            ui->listReceiveAddresses->setCurrentIndex(0);

            ui->statusBar->showMessage(QString::fromStdString("Created new Sapling zaddr"), 10 * 1000);
        }
    });
}


// Adds z-addresses to the combo box. Technically, returns a
// lambda, which can be connected to the appropriate signal
std::function<void(bool)> MainWindow::addZAddrsToComboList(bool sapling) {
    return [=] (bool checked) {
        if (checked && this->rpc->getAllZAddresses() != nullptr) {
            auto addrs = this->rpc->getAllZAddresses();
            ui->listReceiveAddresses->clear();

            std::for_each(addrs->begin(), addrs->end(), [=] (auto addr) {
                if ( (sapling &&  Settings::getInstance()->isSaplingAddress(addr)) ||
                    (!sapling && !Settings::getInstance()->isSaplingAddress(addr))) {
                        if (rpc->getAllBalances()) {
                            auto bal = rpc->getAllBalances()->value(addr);
                            ui->listReceiveAddresses->addItem(addr, bal);
                        }
                }
            });

            // If z-addrs are empty, then create a new one.
            if (addrs->isEmpty()) {
                addNewZaddr();
            }
        }
    };
}

void MainWindow::setupReceiveTab() {
    auto addNewTAddr = [=] () {
        rpc->newTaddr([=] (json reply) {
            qDebug() << "New addr button clicked";
            QString addr = QString::fromStdString(reply.get<json::string_t>());
            // Make sure the RPC class reloads the t-addrs for future use
            rpc->refreshAddresses();

            // Just double make sure the t-address is still checked
            if (ui->rdioTAddr->isChecked()) {
                ui->listReceiveAddresses->insertItem(0, addr);
                ui->listReceiveAddresses->setCurrentIndex(0);

                ui->statusBar->showMessage(tr("Created new t-Addr"), 10 * 1000);
            }
        });
    };

    // Connect t-addr radio button
    QObject::connect(ui->rdioTAddr, &QRadioButton::toggled, [=] (bool checked) {
        qDebug() << "taddr radio toggled";
        if (checked && this->rpc->getUTXOs() != nullptr) {
            updateTAddrCombo(checked);
        }

        // Toggle the "View all addresses" button as well
        ui->btnViewAllAddresses->setVisible(checked);
    });

    // View all addresses goes to "View all private keys"
    QObject::connect(ui->btnViewAllAddresses, &QPushButton::clicked, [=] () {
        // If there's no RPC, return
        if (!getRPC())
            return;

        QDialog d(this);
        Ui_ViewAddressesDialog viewaddrs;
        viewaddrs.setupUi(&d);
        Settings::saveRestore(&d);
        Settings::saveRestoreTableHeader(viewaddrs.tblAddresses, &d, "viewalladdressestable");
        viewaddrs.tblAddresses->horizontalHeader()->setStretchLastSection(true);

        ViewAllAddressesModel model(viewaddrs.tblAddresses, *getRPC()->getAllTAddresses(), getRPC());
        viewaddrs.tblAddresses->setModel(&model);

        QObject::connect(viewaddrs.btnExportAll, &QPushButton::clicked,  this, &MainWindow::exportAllKeys);

        viewaddrs.tblAddresses->setContextMenuPolicy(Qt::CustomContextMenu);
        QObject::connect(viewaddrs.tblAddresses, &QTableView::customContextMenuRequested, [=] (QPoint pos) {
            QModelIndex index = viewaddrs.tblAddresses->indexAt(pos);
            if (index.row() < 0) return;

            index = index.sibling(index.row(), 0);
            QString addr = viewaddrs.tblAddresses->model()->data(index).toString();

            QMenu menu(this);
            menu.addAction(tr("Export Private Key"), [=] () {
                if (addr.isEmpty())
                    return;

                this->exportKeys(addr);
            });
            menu.addAction(tr("Copy Address"), [=]() {
                QGuiApplication::clipboard()->setText(addr);
            });
            menu.exec(viewaddrs.tblAddresses->viewport()->mapToGlobal(pos));
        });

        d.exec();
    });

    QObject::connect(ui->rdioZSAddr, &QRadioButton::toggled, addZAddrsToComboList(true));

    // Explicitly get new address button.
    QObject::connect(ui->btnReceiveNewAddr, &QPushButton::clicked, [=] () {
        if (!rpc->getConnection())
            return;

        if (ui->rdioZSAddr->isChecked()) {
            addNewZaddr();
        } else if (ui->rdioTAddr->isChecked()) {
            addNewTAddr();
        }
    });

    // Focus enter for the Receive Tab
    QObject::connect(ui->tabWidget, &QTabWidget::currentChanged, [=] (int tab) {
        if (tab == 2) {
            // Switched to receive tab, select the z-addr radio button
            ui->rdioZSAddr->setChecked(true);
            ui->btnViewAllAddresses->setVisible(false);

            // And then select the first one
            ui->listReceiveAddresses->setCurrentIndex(0);
        }
    });

    // Validator for label
    QRegExpValidator* v = new QRegExpValidator(QRegExp(Settings::labelRegExp), ui->rcvLabel);
    ui->rcvLabel->setValidator(v);

    // Select item in address list
    QObject::connect(ui->listReceiveAddresses,
        QOverload<int>::of(&QComboBox::currentIndexChanged), [=] (int index) {
        QString addr = ui->listReceiveAddresses->itemText(index);
        if (addr.isEmpty()) {
            // Draw empty stuff

            ui->rcvLabel->clear();
            ui->rcvBal->clear();
            ui->txtReceive->clear();
            ui->qrcodeDisplay->clear();
            return;
        }

        auto label = AddressBook::getInstance()->getLabelForAddress(addr);
        if (label.isEmpty()) {
            ui->rcvUpdateLabel->setText("Add Label");
        }
        else {
            ui->rcvUpdateLabel->setText("Update Label");
        }

        ui->rcvLabel->setText(label);
        ui->rcvBal->setText(Settings::getZECUSDDisplayFormat(rpc->getAllBalances()->value(addr)));
        ui->txtReceive->setPlainText(addr);
        ui->qrcodeDisplay->setQrcodeString(addr);
        if (rpc->getUsedAddresses()->value(addr, false)) {
            ui->rcvBal->setToolTip(tr("Address has been previously used"));
        } else {
            ui->rcvBal->setToolTip(tr("Address is unused"));
        }

    });

    // Receive tab add/update label
    QObject::connect(ui->rcvUpdateLabel, &QPushButton::clicked, [=]() {
        QString addr = ui->listReceiveAddresses->currentText();
        if (addr.isEmpty())
            return;

        auto curLabel = AddressBook::getInstance()->getLabelForAddress(addr);
        auto label = ui->rcvLabel->text().trimmed();

        if (curLabel == label)  // Nothing to update
            return;

        QString info;

        if (!curLabel.isEmpty() && label.isEmpty()) {
            info = "Removed Label '" % curLabel % "'";
            AddressBook::getInstance()->removeAddressLabel(curLabel, addr);
        } else if (!curLabel.isEmpty() && !label.isEmpty()) {
            info = "Updated Label '" % curLabel % "' to '" % label % "'";
            AddressBook::getInstance()->updateLabel(curLabel, addr, label);
        } else if (curLabel.isEmpty() && !label.isEmpty()) {
            info = "Added Label '" % label % "'";
            AddressBook::getInstance()->addAddressLabel(label, addr,"");
        }

        // Update labels everywhere on the UI
        updateLabels();

        // Show the user feedback
        if (!info.isEmpty()) {
            QMessageBox::information(this, "Label", info, QMessageBox::Ok);
        }
    });

    // Receive Export Key
    QObject::connect(ui->exportKey, &QPushButton::clicked, [=]() {
        QString addr = ui->listReceiveAddresses->currentText();
        if (addr.isEmpty())
            return;

        this->exportKeys(addr);
    });
}


void MainWindow::updateTAddrCombo(bool checked) {
    if (checked) {
        auto utxos = this->rpc->getUTXOs();
        ui->listReceiveAddresses->clear();

        std::for_each(utxos->begin(), utxos->end(), [=](auto& utxo) {
            auto addr = utxo.address;
            if (addr.startsWith("R") && ui->listReceiveAddresses->findText(addr) < 0) {
                auto bal = rpc->getAllBalances()->value(addr);
                ui->listReceiveAddresses->addItem(addr, bal);
            }
        });
    }
};

// Updates the labels everywhere on the UI. Call this after the labels have been updated
void MainWindow::updateLabels() {
    // Update the Receive tab
    if (ui->rdioTAddr->isChecked()) {
        updateTAddrCombo(true);
    }
    else {
        addZAddrsToComboList(ui->rdioZSAddr->isChecked())(true);
    }

    // Update the Send Tab
    updateFromCombo();

    // Update the autocomplete
    updateLabelsAutoComplete();
}

void MainWindow::slot_change_currency(const std::string& currency_name)
{
    qDebug() << "slot_change_currency"; //<< ": " << currency_name;
    Settings::getInstance()->set_currency_name(currency_name);
    qDebug() << "Refreshing price stats after currency change";
    rpc->refreshPrice();

    // Include currency
    std::string saved_currency_name;
    try {
       saved_currency_name = Settings::getInstance()->get_currency_name();
    } catch (const std::exception& e) {
        qDebug() << QString("Ignoring currency change Exception! : ");
        saved_currency_name = "BTC";
    }
}

void MainWindow::slot_change_theme(const QString& theme_name)
{
    Settings::getInstance()->set_theme_name(theme_name);

    // Include css
    QString saved_theme_name;
    try {
       saved_theme_name = Settings::getInstance()->get_theme_name();
    } catch (const std::exception& e) {
        qDebug() << QString("Ignoring theme change Exception! : ");
        saved_theme_name = "default";
    }

    QFile qFile(":/css/res/css/" + saved_theme_name +".css");
    if (qFile.open(QFile::ReadOnly))
    {
      QString styleSheet = QLatin1String(qFile.readAll());
      this->setStyleSheet(""); // reset styles
      this->setStyleSheet(styleSheet);
    }

}

MainWindow::~MainWindow()
{
    delete ui;
    delete rpc;
    delete labelCompleter;

    delete amtValidator;
    delete feesValidator;

    delete loadingMovie;
    delete logger;

    delete wsserver;
    delete wormhole;
}
