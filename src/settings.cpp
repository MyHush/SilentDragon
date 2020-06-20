// Copyright 2019-2020 Hush developers
// Released under the GPLv3
#include "mainwindow.h"
#include "settings.h"

Settings* Settings::instance = nullptr;

Settings* Settings::init() {
    if (instance == nullptr)
        instance = new Settings();

    return instance;
}

Settings* Settings::getInstance() {
    return instance;
}

bool Settings::getCheckForUpdates() {
    return QSettings().value("options/allowcheckupdates", true).toBool();
}

void Settings::setCheckForUpdates(bool allow) {
     QSettings().setValue("options/allowcheckupdates", allow);
}

bool Settings::getAllowFetchPrices() {
    return QSettings().value("options/allowfetchprices", true).toBool();
}

void Settings::setAllowFetchPrices(bool allow) {
     QSettings().setValue("options/allowfetchprices", allow);
}

Explorer Settings::getExplorer() {
    // Load from the QT Settings.
    QSettings s;
    //TODO: make it easy for people to use other explorers like komodod.com
    QString explorer = "https://explorer.myhush.org";

    auto txExplorerUrl                = s.value("explorer/txExplorerUrl", explorer + "/tx/").toString();
    auto addressExplorerUrl           = s.value("explorer/addressExplorerUrl", explorer + "/address/").toString();
    auto testnetTxExplorerUrl         = s.value("explorer/testnetTxExplorerUrl").toString();
    auto testnetAddressExplorerUrl    = s.value("explorer/testnetAddressExplorerUrl").toString();

    return Explorer{txExplorerUrl, addressExplorerUrl, testnetTxExplorerUrl, testnetAddressExplorerUrl};
}

void Settings::saveExplorer(const QString& txExplorerUrl, const QString& addressExplorerUrl, const QString& testnetTxExplorerUrl, const QString& testnetAddressExplorerUrl) {
    QSettings s;
    s.setValue("explorer/txExplorerUrl", txExplorerUrl);
    s.setValue("explorer/addressExplorerUrl", addressExplorerUrl);
    s.setValue("explorer/testnetTxExplorerUrl", testnetTxExplorerUrl);
    s.setValue("explorer/testnetAddressExplorerUrl", testnetAddressExplorerUrl);
}

Config Settings::getSettings() {
    // Load from the QT Settings.
    QSettings s;

    auto host        = s.value("connection/host").toString();
    auto port        = s.value("connection/port").toString();
    auto username    = s.value("connection/rpcuser").toString();
    auto password    = s.value("connection/rpcpassword").toString();
    
    return Config{host, port, username, password};
}

void Settings::saveSettings(const QString& host, const QString& port, const QString& username, const QString& password) {
    QSettings s;

    s.setValue("connection/host", host);
    s.setValue("connection/port", port);
    s.setValue("connection/rpcuser", username);
    s.setValue("connection/rpcpassword", password);

    s.sync();

    // re-init to load correct settings
    init();
}

void Settings::saveRestoreTableHeader(QTableView* table, QDialog* d, QString tablename) {
    table->horizontalHeader()->restoreState(QSettings().value(tablename).toByteArray());
    table->horizontalHeader()->setStretchLastSection(true);

    QObject::connect(d, &QDialog::finished, [=](auto) {
        QSettings().setValue(tablename, table->horizontalHeader()->saveState());
    });
}

void Settings::setUsingZcashConf(QString confLocation) {
    if (!confLocation.isEmpty())
        _confLocation = confLocation;
}

bool Settings::isTestnet() {
    return _isTestnet;
}

void Settings::setTestnet(bool isTestnet) {
    this->_isTestnet = isTestnet;
}

bool Settings::isSaplingAddress(QString addr) {
    if (!isValidAddress(addr))
        return false;

    return ( isTestnet() && addr.startsWith("ztestsapling")) ||
           (!isTestnet() && addr.startsWith("zs1"));
}

bool Settings::isSproutAddress(QString addr) {
    if (!isValidAddress(addr))
        return false;

    return isZAddress(addr) && !isSaplingAddress(addr);
}

bool Settings::isZAddress(QString addr) {
    if (!isValidAddress(addr))
        return false;

    return addr.startsWith("z");
}

bool Settings::isTAddress(QString addr) {
    if (!isValidAddress(addr))
        return false;

    return addr.startsWith("R");
}

int Settings::getZcashdVersion() {
    return _zcashdVersion;
}

void Settings::setZcashdVersion(int version) {
    _zcashdVersion = version;
}

bool Settings::isSyncing() {
    return _isSyncing;
}

void Settings::setSyncing(bool syncing) {
    this->_isSyncing = syncing;
}

int Settings::getBlockNumber() {
    return this->_blockNumber;
}

void Settings::setBlockNumber(int number) {
    this->_blockNumber = number;
}

bool Settings::isSaplingActive() {
    return  (isTestnet() && getBlockNumber() > 0) || (!isTestnet() && getBlockNumber() > 0);
}

double Settings::getZECPrice() {
    return zecPrice;
}

double Settings::get_price(QString currency) {
    currency = currency.toLower();
    QString ticker = currency;
    auto search = prices.find(currency);
    if (search != prices.end()) {
        qDebug() << "Found price of " << ticker << " = " << search->second;
        return search->second;
    } else {
        qDebug() << "Could not find price of" << ticker << "!!!";
        return 0.0;
    }
}

void Settings::set_price(QString curr, double price) {
    QString ticker = curr;
    qDebug() << "Setting price of " << ticker << "=" << QString::number(price);
    prices.insert( std::make_pair(curr, price) );
    prices.insert( std::make_pair(curr, price) );
}

void Settings::set_volume(QString curr, double volume) {
    QString ticker = curr;
    qDebug() << "Setting volume of " << ticker << "=" << QString::number(volume);
    volumes.insert( std::make_pair(curr, volume) );
}

double Settings::get_volume(QString currency) {
    currency = currency.toLower();
    QString ticker = currency;
    auto search = volumes.find(currency);
    if (search != volumes.end()) {
        qDebug() << "Found volume of " << ticker << " = " << search->second;
        return search->second;
    } else {
        qDebug() << "Could not find volume of" << ticker << "!!!";
        return 0.0;
    }
}

void Settings::set_marketcap(QString curr, double marketcap) {
    QString ticker = curr;
    qDebug() << "Setting marketcap of " << ticker << "=" << QString::number(marketcap);
    marketcaps.insert( std::make_pair(curr, marketcap) );
}

double Settings::get_marketcap(QString currency) {
    currency = currency.toLower();
    QString ticker = currency;
    auto search = marketcaps.find(currency);
    if (search != marketcaps.end()) {
        qDebug() << "Found marketcap of " << ticker << " = " << search->second;
        return search->second;
    } else {
        qDebug() << "Could not find marketcap of" << ticker << "!!!";
        return -1.0;
    }
}

unsigned int Settings::getBTCPrice() {
    // in satoshis
    return btcPrice;
}

bool Settings::getAutoShield() {
    // Load from Qt settings
    return QSettings().value("options/autoshield", true).toBool();
}

void Settings::setAutoShield(bool allow) {
    QSettings().setValue("options/autoshield", allow);
}

bool Settings::getAllowCustomFees() {
    // Load from the QT Settings.
    return QSettings().value("options/customfees", true).toBool();
}

void Settings::setAllowCustomFees(bool allow) {
    QSettings().setValue("options/customfees", allow);
}

QString Settings::get_theme_name() {
    // Load from the QT Settings.
    return QSettings().value("options/theme_name", false).toString();
}

void Settings::set_theme_name(QString theme_name) {
    QSettings().setValue("options/theme_name", theme_name);
}

bool Settings::getSaveZtxs() {
    // Load from the QT Settings.
    return QSettings().value("options/savesenttx", true).toBool();
}

void Settings::setSaveZtxs(bool save) {
    QSettings().setValue("options/savesenttx", save);
}

void Settings::setPeers(int peers) {
    _peerConnections = peers;
}

int Settings::getPeers() {
    return _peerConnections;
}
//=================================
// Static Stuff
//=================================
void Settings::saveRestore(QDialog* d) {
    d->restoreGeometry(QSettings().value(d->objectName() % "geometry").toByteArray());

    QObject::connect(d, &QDialog::finished, [=](auto) {
        QSettings().setValue(d->objectName() % "geometry", d->saveGeometry());
    });
}

QString Settings::getUSDFormat(double bal) {
    //TODO: respect current locale!
    return QLocale(QLocale::English).toString(bal * Settings::getInstance()->getZECPrice(), 'f', 8) + " " +Settings::getInstance()->get_currency_name();
}

QString Settings::getDecimalString(double amt) {
    QString f = QString::number(amt, 'f', 8);

    while (f.contains(".") && (f.right(1) == "0" || f.right(1) == ".")) {
        f = f.left(f.length() - 1);
    }
    if (f == "-0")
        f = "0";

    return f;
}

QString Settings::getDisplayFormat(double bal) {
    // This is idiotic. Why doesn't QString have a way to do this?
    return getDecimalString(bal) % " " % Settings::getTokenName();
}

QString Settings::getZECUSDDisplayFormat(double bal) {
    auto usdFormat = getUSDFormat(bal);
    if (!usdFormat.isEmpty())
        return getDisplayFormat(bal) % " (" % getUSDFormat(bal) % ")";
    else
        return getDisplayFormat(bal);
}

const QString Settings::txidStatusMessage = QString(QObject::tr("Transaction submitted (right click to copy) txid:"));

QString Settings::getTokenName() {
    if (Settings::getInstance()->isTestnet()) {
        return "TUSH";
    } else {
        return "HUSH";
    }
}

//TODO: this isn't used for donations
QString Settings::getDonationAddr() {
    if (Settings::getInstance()->isTestnet())  {
	    return "ztestsaplingXXX";
    }
    // This is used for user feedback
    return "zs1aq4xnrkjlnxx0zesqye7jz3dfrf3rjh7q5z6u8l6mwyqqaam3gx3j2fkqakp33v93yavq46j83q";
}

bool Settings::addToZcashConf(QString confLocation, QString line) {
    QFile file(confLocation);
    if (!file.open(QIODevice::ReadWrite | QIODevice::Append))
        return false;


    QTextStream out(&file);
    out << line << "\n";
    file.close();

    return true;
}

QString Settings::get_currency_name() {
    // Load from the QT Settings.
    return QSettings().value("options/currency_name", "BTC").toString();
}

void Settings::set_currency_name(QString currency_name) {
    QSettings().setValue("options/currency_name", currency_name);
}


bool Settings::removeFromZcashConf(QString confLocation, QString option) {
    if (confLocation.isEmpty())
        return false;

    // To remove an option, we'll create a new file, and copy over everything but the option.
    QFile file(confLocation);
    if (!file.open(QIODevice::ReadOnly))
        return false;

    QList<QString> lines;
    QTextStream in(&file);
    while (!in.atEnd()) {
        QString line = in.readLine();
        auto s = line.indexOf("=");
        QString name = line.left(s).trimmed().toLower();
        if (name != option) {
            lines.append(line);
        }
    }
    file.close();

    QFile newfile(confLocation);
    if (!newfile.open(QIODevice::ReadWrite | QIODevice::Truncate))
        return false;

    QTextStream out(&newfile);
    for (QString line : lines) {
        out << line << endl;
    }
    newfile.close();

    return true;
}

double Settings::getMinerFee() {
    return 0.0001;
}

bool Settings::isValidSaplingPrivateKey(QString pk) {
    if (isTestnet()) {
        QRegExp zspkey("^secret-extended-key-test[0-9a-z]{278}$", Qt::CaseInsensitive);
        return zspkey.exactMatch(pk);
    } else {
        QRegExp zspkey("^secret-extended-key-main[0-9a-z]{278}$", Qt::CaseInsensitive);
        return zspkey.exactMatch(pk);
    }
}

bool Settings::isValidAddress(QString addr) {
    QRegExp zsexp("^zs1[a-z0-9]{75}$",  Qt::CaseInsensitive);
    QRegExp ztsexp("^ztestsapling[a-z0-9]{76}", Qt::CaseInsensitive);
    QRegExp texp("^R[a-z0-9]{33}$", Qt::CaseInsensitive);
    //qDebug() << "isValidAddress(" << addr << ")";

    return  texp.exactMatch(addr) || ztsexp.exactMatch(addr) || zsexp.exactMatch(addr);
}

// Get a pretty string representation of this Payment URI
QString Settings::paymentURIPretty(PaymentURI uri) {
    return QString() + "Payment Request\n" + "Pay: " + uri.addr + "\nAmount: " + getDisplayFormat(uri.amt.toDouble())
        + "\nMemo:" + QUrl::fromPercentEncoding(uri.memo.toUtf8());
}

// Parse a payment URI string into its components
PaymentURI Settings::parseURI(QString uri) {
    PaymentURI ans;

    if (!uri.startsWith("hush:")) {
        ans.error = "Not a HUSH payment URI";
        return ans;
    }

    uri = uri.right(uri.length() - QString("hush:").length());

    QRegExp re("([a-zA-Z0-9]+)");
    int pos;
    if ( (pos = re.indexIn(uri)) == -1 ) {
        ans.error = "Couldn't find an address";
        return ans;
    }

    ans.addr = re.cap(1);
    if (!Settings::isValidAddress(ans.addr)) {
        ans.error = "Could not understand address";
        return ans;
    }

    uri = uri.right(uri.length() - ans.addr.length()-1);  // swallow '?'
    QUrlQuery query(uri);
    
    // parse out amt / amount
    if (query.hasQueryItem("amt")) {
        ans.amt  = query.queryItemValue("amt");
    } else if (query.hasQueryItem("amount")) {
        ans.amt  = query.queryItemValue("amount");
    }
    
    // parse out memo / msg / message
    if (query.hasQueryItem("memo")) {
        ans.memo  = query.queryItemValue("memo");
    } else if (query.hasQueryItem("msg")) {
        ans.memo  = query.queryItemValue("msg");
    } else if  (query.hasQueryItem("message")) {
        ans.memo  = query.queryItemValue("message");
    }

    return ans;
}

const QString Settings::labelRegExp("[a-zA-Z0-9\\-_]{0,40}");
