// Copyright 2019 The Hush developers
#ifndef SETTINGS_H
#define SETTINGS_H

#include "precompiled.h"

struct Config {
    QString host;
    QString port;
    QString rpcuser;
    QString rpcpassword;
};

struct Explorer {
    QString txExplorerUrl;
    QString addressExplorerUrl;
    QString testnetTxExplorerUrl;
    QString testnetAddressExplorerUrl;
};

struct ToFields;
struct Tx;

struct PaymentURI {
    QString addr;
    QString amt;
    QString memo;

    // Any errors are stored here
    QString error;
};

class Settings
{
public:
    static  Settings* init();
    static  Settings* getInstance();

    Explorer  getExplorer();
    void    saveExplorer(const QString& txExplorerUrl, const QString& addressExplorerUrl, const QString& testnetTxExplorerUrl, const QString& testnetAddressExplorerUrl);

    Config  getSettings();
    void    saveSettings(const QString& host, const QString& port, const QString& username, const QString& password);

    bool    isTestnet();
    void    setTestnet(bool isTestnet);

    bool    isSaplingAddress(QString addr);
    bool    isSproutAddress(QString addr);

    bool    isValidSaplingPrivateKey(QString pk);

    bool    isSyncing();
    void    setSyncing(bool syncing);

    int     getZcashdVersion();
    void    setZcashdVersion(int version);

    void    setUseEmbedded(bool r) { _useEmbedded = r; }
    bool    useEmbedded() { return _useEmbedded; }

    void    setHeadless(bool h) { _headless = h; }
    bool    isHeadless() { return _headless; }

    int     getBlockNumber();
    void    setBlockNumber(int number);

    bool    getSaveZtxs();
    void    setSaveZtxs(bool save);

    bool    getAutoShield();
    void    setAutoShield(bool allow);

    bool    getAllowCustomFees();
    void    setAllowCustomFees(bool allow);

    bool    getAllowFetchPrices();
    void    setAllowFetchPrices(bool allow);

    bool    getCheckForUpdates();
    void    setCheckForUpdates(bool allow);

    bool    isSaplingActive();

    QString get_theme_name();
    void set_theme_name(QString theme_name);

    QString get_currency_name();
    void set_currency_name(QString currency_name);

    void    setUsingZcashConf(QString confLocation);
    const   QString& getZcashdConfLocation() { return _confLocation; }

    void    setZECPrice(double p)       { zecPrice = p;   }
    void    set_fiat_price(double p)    { fiat_price = p; }
    void    setBTCPrice(unsigned int p) { btcPrice = p;   }
    double  getZECPrice();
    double  get_fiat_price();
    unsigned int  getBTCPrice();
    double get_price(QString currency);
    void   set_price(QString currency, double price);
    double get_volume(QString ticker);
    void   set_volume(QString curr, double volume);
    double get_marketcap(QString curr);
    void   set_marketcap(QString curr, double marketcap);

    void    setPeers(int peers);
    int     getPeers();

    // Static stuff
    static const QString txidStatusMessage;

    static void saveRestore(QDialog* d);
    static void saveRestoreTableHeader(QTableView* table, QDialog* d, QString tablename) ;

    static PaymentURI parseURI(QString paymentURI);
    static QString    paymentURIPretty(PaymentURI);

    static bool    isZAddress(QString addr);
    static bool    isTAddress(QString addr);

    static QString getDecimalString(double amt);
    static QString getUSDFormat(double bal);
    static QString getDisplayFormat(double bal);
    static QString getZECUSDDisplayFormat(double bal);

    static QString getTokenName();
    static QString getDonationAddr();

    static double  getMinerFee();
    static double  getZboardAmount();
    static QString getZboardAddr();

    static int     getMaxMobileAppTxns() { return 30; }

    static bool    isValidAddress(QString addr);

    static bool    addToZcashConf(QString confLocation, QString line);
    static bool    removeFromZcashConf(QString confLocation, QString option);

    static const QString labelRegExp;

    //TODO: add these as advanced options, with sane minimums
    static const int     updateSpeed         = 10 * 1000;        // 10 sec
    static const int     quickUpdateSpeed    = 3  * 1000;        // 3 sec
    static const int     priceRefreshSpeed   = 15 * 60 * 1000;   // 15 mins

private:
    // This class can only be accessed through Settings::getInstance()
    Settings() = default;
    ~Settings() = default;

    static Settings* instance;

    QString _confLocation;
    QString _executable;
    bool    _isTestnet        = false;
    bool    _isSyncing        = false;
    int     _blockNumber      = 0;
    int     _zcashdVersion    = 0;
    bool    _useEmbedded      = false;
    bool    _headless         = false;
    int     _peerConnections  = 0;

    double  zecPrice          = 0.0;
    double  fiat_price        = 0.0;
    unsigned int  btcPrice    = 0;
    std::map<QString, double> prices;
    std::map<QString, double> volumes;
    std::map<QString, double> marketcaps;
};

#endif // SETTINGS_H
