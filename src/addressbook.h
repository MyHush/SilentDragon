// Copyright 2019-2020 The Hush Developers
// Released under the GPLv3
#ifndef ADDRESSBOOK_H
#define ADDRESSBOOK_H

#include "precompiled.h"

class MainWindow;

class AddressBookModel : public QAbstractTableModel {

public:
    AddressBookModel(QTableView* parent);
    ~AddressBookModel();
                            
    void                    addNewLabel(QString label, QString addr, QString myzaddr);
    void                    removeItemAt(int row);
    QList<QString>          itemAt(int row);

    int      rowCount(const QModelIndex &parent) const;
    int      columnCount(const QModelIndex &parent) const;
    QVariant data(const QModelIndex &index, int role) const;
    QVariant headerData(int section, Qt::Orientation orientation, int role) const;

private:
    void loadData();
    void saveData();

    QTableView* parent;
    QList<QList<QString>> labels;
    QStringList headers;    
};

class AddressBook {
public:    
    // Method that opens the AddressBook dialog window. 
    static void open(MainWindow* parent, QLineEdit* target = nullptr);

    static AddressBook* getInstance(); 
    static QString addLabelToAddress(QString addr);
    static QString addressFromAddressLabel(const QString& lblAddr);

    // Add a new address/label to the database
    void addAddressLabel(QString label, QString address, QString myZaddr);

    // Remove a new address/label from the database
    void removeAddressLabel(QString label, QString address);

    // Update a label/address
    void updateLabel(QString oldlabel, QString address, QString newlabel);

    // Read all addresses
    const QList<QList<QString>>& getAllAddressLabels();

    // Get an address's first label
    QString getLabelForAddress(QString address);
    // Get a Label's address
    QString getAddressForLabel(QString label);
private:
    AddressBook();

    void readFromStorage();
    void writeToStorage();

    QString writeableFile();
    // contact name, contact address, myzaddr (the zaddr we use to receive from this contact)
    QList<QList<QString>> allLabels;

    static AddressBook* instance;
};

#endif // ADDRESSBOOK_H
