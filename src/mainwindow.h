#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include "precompiled.h"
#include "logger.h"

// Forward declare to break circular dependency.
class RPC;
class Settings;
class WSServer;
class WormholeClient;

using json = nlohmann::json;

// Struct used to hold destination info when sending a Tx. 
struct ToFields {
    QString addr;
    double  amount;
    QString txtMemo;
    QString encodedMemo;
};

// Struct used to represent a Transaction. 
struct Tx {
    QString         fromAddr;
    QList<ToFields> toAddrs;
    double          fee;
};

namespace Ui {
    class MainWindow;
}

class HushContact
{

public:
    void setName(QString newname) { name =  newname; }
    QString getName() { return name; }
    QString getZaddr() { return zaddr; }
    void setZaddr(QString z) { zaddr = z; }
    QString getMyZaddr() { return myZaddr; }
    void setMyZaddr(QString z) { myZaddr = z; }
private:
    QString name;
    QString nickname;
    QString zaddr;
    QString myZaddr;
    int64_t lastSentTime;
    int64_t lastReceivedTime;

};

class HushChat
{

public:
    void setContact(HushContact newcontact) { contact = newcontact; }
    HushContact getContact() { return contact; }
    QString getMyZaddr() { return myZaddr; }
    void setMyZaddr(QString z) {  myZaddr = z; }
private:
    QString myZaddr;
    HushContact contact;

};


class MainWindow : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();

    void updateLabelsAutoComplete();
    RPC* getRPC() { return rpc; }

    QCompleter*         getLabelCompleter() { return labelCompleter; }
    QRegExpValidator*   getAmountValidator() { return amtValidator; }

    QString doSendTxValidations(Tx tx);
    void setDefaultPayFrom();

    void replaceWormholeClient(WormholeClient* newClient);
    bool isWebsocketListening();
    void createWebsocket(QString wormholecode);
    void stopWebsocket();

    void balancesReady();
    void payZcashURI(QString uri = "", QString myAddr = "");

    void validateAddress();

    void updateLabels();
    void updateTAddrCombo(bool checked);
    void updateFromCombo();

    Ui::MainWindow*     ui;

    QLabel*             statusLabel;
    QLabel*             statusIcon;
    QLabel*             loadingLabel;
    QWidget*            zcashdtab;

    Logger*      logger;

    void doClose();
    HushChat getHushChat() { return hushChat; }
    void setHushChat(HushChat chat) { hushChat = chat; }

private:    
    void closeEvent(QCloseEvent* event);

    void setupSendTab();
    void setupTransactionsTab();
    void setupReceiveTab();
    void setupBalancesTab();
    void setupHushTab();
    void setupChatTab();
    void setupMarketTab();

    void slot_change_theme(const QString& themeName);
    void slot_change_currency(const std::string& currencyName);
    void setupTurnstileDialog();
    void setupSettingsModal();
    void setupStatusBar();

    void removeExtraAddresses();

    Tx   createTxFromSendPage();
    bool confirmTx(Tx tx);

    void turnstileDoMigration(QString fromAddr = "");
    void turnstileProgress();

    void cancelButton();
    void sendButton();
    void sendMemo();
    void inputComboTextChanged(int index);
    void addAddressSection();
    void maxAmountChecked(int checked);

    void editSchedule();

    void addressChanged(int number, const QString& text);
    void amountChanged (int number, const QString& text);

    void addNewZaddr();
    std::function<void(bool)> addZAddrsToComboList(bool sapling);

    void memoButtonClicked(int number, bool includeReplyTo = false);
    void fileUploadButtonClicked(int number);
    void setMemoEnabled(int number, bool enabled);
    
    void donate();
    void website();
    void discord();
    void reportbug();
    void addressBook();
    void postToZBoard();
    void importPrivKey();
    void exportAllKeys();
    void exportKeys(QString addr = "");
    void getViewKey(QString addr = "");
    void backupWalletDat();
    void exportTransactions();

    void doImport(QList<QString>* keys);

    void restoreSavedStates();
    bool eventFilter(QObject *object, QEvent *event);

    bool            uiPaymentsReady    = false;
    QString         pendingURIPayment;

    WSServer*       wsserver = nullptr;
    WormholeClient* wormhole = nullptr;
    HushChat        hushChat;

    RPC*                rpc             = nullptr;
    QCompleter*         labelCompleter  = nullptr;
    QRegExpValidator*   amtValidator    = nullptr;
    QRegExpValidator*   feesValidator   = nullptr;

    QMovie*      loadingMovie;
};

#endif // MAINWINDOW_H
