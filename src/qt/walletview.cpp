// Copyright (c) 2011-2013 The Bitcoin developers
// Distributed under the MIT/X11 software license, see the accompanying
// file COPYING or http://www.opensource.org/licenses/mit-license.php.

#include "walletview.h"

#include "addressbookpage.h"
#include "bip38tooldialog.h"
#include "bitcoingui.h"
#include "blockexplorer.h"
#include "clientmodel.h"
#include "guiutil.h"
#include "historypage.h"
#include "masternodeconfig.h"
#include "multisenddialog.h"
#include "optionsmodel.h"
#include "overviewpage.h"
#include "optionspage.h"
#include "receivecoinsdialog.h"
#include "sendcoinsdialog.h"
#include "signverifymessagedialog.h"
#include "transactiontablemodel.h"
#include "transactionview.h"
#include "walletmodel.h"

#include "guiinterface.h"

#include <QAction>
#include <QActionGroup>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QProgressDialog>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

WalletView::WalletView(QWidget* parent) : QStackedWidget(parent),
                                          clientModel(0),
                                          walletModel(0)
{
    // Create tabs
    overviewPage = new OverviewPage();
    explorerWindow = new BlockExplorer(this);
    transactionsPage = new QWidget(this);
    QVBoxLayout* vbox = new QVBoxLayout();
    QHBoxLayout* hbox_buttons = new QHBoxLayout();
    transactionView = new TransactionView(this);
    vbox->addWidget(transactionView);
    QPushButton* exportButton = new QPushButton(tr("&Export"), this);
    exportButton->setToolTip(tr("Export the data in the current tab to a file"));
    hbox_buttons->addStretch();
    // Sum of selected transactions
    QLabel* transactionSumLabel = new QLabel();                // Label
    transactionSumLabel->setObjectName("transactionSumLabel"); // Label ID as CSS-reference
    transactionSumLabel->setText(tr("Selected amount:"));
    hbox_buttons->addWidget(transactionSumLabel);

    transactionSum = new QLabel();                   // Amount
    transactionSum->setObjectName("transactionSum"); // Label ID as CSS-reference
    transactionSum->setMinimumSize(200, 8);
    transactionSum->setTextInteractionFlags(Qt::TextSelectableByMouse);
    hbox_buttons->addWidget(transactionSum);

    hbox_buttons->addWidget(exportButton);
    vbox->addLayout(hbox_buttons);

    receiveCoinsPage = new ReceiveCoinsDialog();
    sendCoinsPage = new SendCoinsDialog();
    optionsPage = new OptionsPage();
    historyPage = new HistoryPage();
    masternodeListPage = new MasternodeList();

    addWidget(overviewPage);
    addWidget(historyPage);
    addWidget(receiveCoinsPage);
    addWidget(sendCoinsPage);
    addWidget(optionsPage);
    addWidget(explorerWindow);
    addWidget(masternodeListPage);

    // Clicking on a transaction on the overview pre-selects the transaction on the transaction history page
    connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), transactionView, SLOT(focusTransaction(QModelIndex)));

    // Double-clicking on a transaction on the transaction history page shows details
    connect(transactionView, SIGNAL(doubleClicked(QModelIndex)), transactionView, SLOT(showDetails()));

    // Update wallet with sum of selected transactions
    connect(transactionView, SIGNAL(trxAmount(QString)), this, SLOT(trxAmount(QString)));

    // Clicking on "Export" allows to export the transaction list
    connect(exportButton, SIGNAL(clicked()), transactionView, SLOT(exportClicked()));

    // Pass through messages from transactionView
    connect(transactionView, SIGNAL(message(QString, QString, unsigned int)), this, SIGNAL(message(QString, QString, unsigned int)));
}

WalletView::~WalletView()
{
}

void WalletView::setBitcoinGUI(BitcoinGUI* gui)
{
    if (gui) {
        // Clicking on a transaction on the overview page simply sends you to transaction history page
        connect(overviewPage, SIGNAL(transactionClicked(QModelIndex)), gui, SLOT(gotoHistoryPage()));

        // Receive and report messages
        connect(this, SIGNAL(message(QString, QString, unsigned int)), gui, SLOT(message(QString, QString, unsigned int)));

        // Pass through encryption status changed signals
        connect(this, SIGNAL(encryptionStatusChanged(int)), gui, SLOT(setEncryptionStatus(int)));

        // Pass through transaction notifications
        connect(this, SIGNAL(incomingTransaction(QString, int, CAmount, QString, QString, QString)), gui, SLOT(incomingTransaction(QString, int, CAmount, QString, QString, QString)));
        connect(this, SIGNAL(stakingStatusChanged(bool)), gui, SLOT(setStakingInProgress(bool)));
    }
}

void WalletView::stakingStatus(bool stt)
{
    Q_EMIT stakingStatusChanged(stt);
}

void WalletView::setClientModel(ClientModel* clientModel)
{
    this->clientModel = clientModel;

    overviewPage->setClientModel(clientModel);
    sendCoinsPage->setClientModel(clientModel);
    masternodeListPage->setClientModel(clientModel);

}

void WalletView::setWalletModel(WalletModel* walletModel)
{
    this->walletModel = walletModel;

    // Put transaction list in tabs
    transactionView->setModel(walletModel);
    overviewPage->setWalletModel(walletModel);
    masternodeListPage->setWalletModel(walletModel);
    historyPage->setModel(walletModel);
    receiveCoinsPage->setModel(walletModel);
    sendCoinsPage->setModel(walletModel);
    optionsPage->setModel(walletModel);
    if (walletModel) {
        // Receive and pass through messages from wallet model
        connect(walletModel, SIGNAL(message(QString, QString, unsigned int)), this, SIGNAL(message(QString, QString, unsigned int)));

        // Handle changes in encryption status
        connect(walletModel, SIGNAL(encryptionStatusChanged(int)), this, SIGNAL(encryptionStatusChanged(int)));
        updateEncryptionStatus();

        // Balloon pop-up for new transaction
        connect(walletModel->getTransactionTableModel(), SIGNAL(rowsInserted(QModelIndex, int, int)),
            this, SLOT(processNewTransaction(QModelIndex, int, int)));

        // Ask for passphrase if needed
        connect(walletModel, SIGNAL(requireUnlock(AskPassphraseDialog::Context)), this, SLOT(unlockWallet(AskPassphraseDialog::Context)));

        // Show progress dialog
        connect(walletModel, SIGNAL(showProgress(QString, int)), this, SLOT(showProgress(QString, int)));
        connect(walletModel, SIGNAL(stakingStatusChanged(bool)), this, SLOT(stakingStatus(bool)));
    }
}

void WalletView::processNewTransaction(const QModelIndex& parent, int start, int /*end*/)
{
    // Prevent balloon-spam when initial block download is in progress
    if (!walletModel || !clientModel || clientModel->inInitialBlockDownload())
        return;

    TransactionTableModel* ttm = walletModel->getTransactionTableModel();
    if (!ttm || ttm->processingQueuedTransactions())
        return;

    QString date = ttm->index(start, TransactionTableModel::Date, parent).data().toString();
    qint64 amount = ttm->index(start, TransactionTableModel::Amount, parent).data(Qt::EditRole).toULongLong();
    QString type = ttm->index(start, TransactionTableModel::Type, parent).data().toString();
    QString address = ttm->index(start, TransactionTableModel::ToAddress, parent).data().toString();
    QString confirmations = ttm->index(start, TransactionTableModel::Confirmations, parent).data().toString();

    Q_EMIT incomingTransaction(date, walletModel->getOptionsModel()->getDisplayUnit(), amount, type, address, confirmations);
}

void WalletView::gotoOverviewPage()
{
    setCurrentWidget(overviewPage);
    // Refresh UI-elements in case coins were locked/unlocked in CoinControl
    walletModel->emitBalanceChanged();
}

void WalletView::gotoHistoryPage()
{
    int lastTime = GetAdjustedTime();
    setCurrentWidget(historyPage);
    if (GetAdjustedTime() - lastTime < 30) {
        historyPage->updateTableData();
    }
}


void WalletView::gotoBlockExplorerPage()
{
    setCurrentWidget(explorerWindow);
}

void WalletView::gotoMasternodePage()
{
    setCurrentWidget(masternodeListPage);
}

void WalletView::gotoReceiveCoinsPage()
{
    setCurrentWidget(receiveCoinsPage);
}

void WalletView::gotoOptionsPage()
{
    setCurrentWidget(optionsPage);
}

void WalletView::gotoSendCoinsPage(QString addr)
{
    setCurrentWidget(sendCoinsPage);
}

void WalletView::gotoMultiSendDialog()
{
    MultiSendDialog* multiSendDialog = new MultiSendDialog(this);
    multiSendDialog->setModel(walletModel);
    multiSendDialog->show();
}

void WalletView::showSyncStatus(bool fShow)
{
    overviewPage->showBlockSync(fShow);
}

void WalletView::updateEncryptionStatus()
{
    Q_EMIT encryptionStatusChanged(walletModel->getEncryptionStatus());
}

void WalletView::encryptWallet(bool status)
{
    if (!walletModel)
        return;
    AskPassphraseDialog dlg(status ? AskPassphraseDialog::Mode::Encrypt : AskPassphraseDialog::Mode::Decrypt, this,
                            walletModel, AskPassphraseDialog::Context::Encrypt);
    dlg.exec();

    updateEncryptionStatus();
}

void WalletView::backupWallet()
{
    QString filename = GUIUtil::getSaveFileName(this,
        tr("Backup Wallet"), QString(),
        tr("Wallet Data (*.dat)"), NULL);

    if (filename.isEmpty())
        return;

    walletModel->backupWallet(filename);
}

void WalletView::showSeedPhrase()
{
    if(!walletModel)
        return;

    WalletModel::EncryptionStatus encryptionStatus = walletModel->getEncryptionStatus();

    if (encryptionStatus == WalletModel::Locked || encryptionStatus == WalletModel::UnlockedForStakingOnly) {
        WalletModel::UnlockContext ctx(walletModel->requestUnlock(AskPassphraseDialog::Context::Unlock_Full, true));
        if (!ctx.isValid()) {
            QMessageBox msgBox;
            msgBox.setWindowTitle("Mnemonic Recovery Phrase");
            msgBox.setIcon(QMessageBox::Information);
            msgBox.setText("Attempt to view Mnemonic Phrase failed or canceled. Wallet locked for security.");
            msgBox.setStyleSheet(GUIUtil::loadStyleSheet());
            msgBox.exec();
            LogPrintf("Attempt to view Mnemonic Phrase failed or canceled. Wallet locked for security.\n");
            return;
        } else {
            SecureString pass;
            walletModel->setWalletLocked(false, pass);
            LogPrintf("Attempt to view Mnemonic Phrase successful.\n");
        }
    } else {
        QMessageBox::StandardButton reply;
        reply = QMessageBox::question(this, "Are You Sure?", "Are you sure you would like to view your Mnemonic Phrase?\nYou will be required to enter your passphrase. Failed or canceled attempts will be logged.", QMessageBox::Yes|QMessageBox::No);
        if (reply == QMessageBox::Yes) {
            walletModel->setWalletLocked(true);
            WalletModel::UnlockContext ctx(walletModel->requestUnlock(AskPassphraseDialog::Context::Unlock_Full, true));
            if (!ctx.isValid()) {
                QMessageBox msgBox;
                msgBox.setWindowTitle("Mnemonic Recovery Phrase");
                msgBox.setIcon(QMessageBox::Information);
                msgBox.setText("Attempt to view Mnemonic Phrase failed or canceled. Wallet locked for security.");
                msgBox.setStyleSheet(GUIUtil::loadStyleSheet());
                msgBox.exec();
                LogPrintf("Attempt to view Mnemonic Phrase failed or canceled. Wallet locked for security.\n");
                return;
            } else {
                SecureString pass;
                walletModel->setWalletLocked(false, pass);
                LogPrintf("Attempt to view Mnemonic Phrase successful.\n");
            }
        } else {
            LogPrintf("Attempt to view Mnemonic Phrase canceled.\n");
            return;
        }
    }

    QString phrase = "";
    std::string recoverySeedPhrase = "";
    if (walletModel->getSeedPhrase(recoverySeedPhrase)) {
        phrase = QString::fromStdString(recoverySeedPhrase);
    }

    QMessageBox msgBox;
    QPushButton *copyButton = msgBox.addButton(tr("Copy"), QMessageBox::ActionRole);
    QPushButton *okButton = msgBox.addButton(tr("OK"), QMessageBox::ActionRole);
    copyButton->setStyleSheet("background:transparent;");
    copyButton->setIcon(QIcon(":/icons/editcopy"));
    msgBox.setWindowTitle("Mnemonic Recovery Phrase");
    msgBox.setText("Below is your Mnemonic Recovery Phrase, consisting of 24 seed words. Please copy/write these words down in order. We strongly recommend keeping multiple copies in different locations.");
    msgBox.setInformativeText("\n<b>" + phrase + "</b>");
    msgBox.setStyleSheet(GUIUtil::loadStyleSheet());
    msgBox.exec();

    if (msgBox.clickedButton() == copyButton) {
        //Copy Mnemonic Recovery Phrase to clipboard
        GUIUtil::setClipboard(phrase);
    }
}

void WalletView::changePassphrase()
{
    AskPassphraseDialog dlg(AskPassphraseDialog::Mode::ChangePass, this, walletModel, AskPassphraseDialog::Context::ChangePass);
    dlg.exec();
}

void WalletView::unlockWallet(AskPassphraseDialog::Context context)
{
    if (!walletModel)
        return;
    // Unlock wallet when requested by wallet model

    if (walletModel->getEncryptionStatus() == WalletModel::Locked || walletModel->getEncryptionStatus() == WalletModel::UnlockedForStakingOnly) {
        AskPassphraseDialog dlg(AskPassphraseDialog::Mode::UnlockStaking, this, walletModel, context);
        dlg.exec();
    }
}

void WalletView::lockWallet()
{
    if (!walletModel)
        return;

    walletModel->setWalletLocked(true);
}

void WalletView::toggleLockWallet()
{
    if (!walletModel)
        return;

    WalletModel::EncryptionStatus encStatus = walletModel->getEncryptionStatus();

    // Unlock the wallet when requested
    if (encStatus == walletModel->Locked) {
        AskPassphraseDialog dlg(AskPassphraseDialog::Mode::UnlockStaking, this, walletModel, AskPassphraseDialog::Context::ToggleLock);
        dlg.exec();
    }

    else if (encStatus == walletModel->Unlocked || encStatus == walletModel->UnlockedForStakingOnly) {
            walletModel->setWalletLocked(true);
    }
}

void WalletView::usedSendingAddresses()
{
    if (!walletModel)
        return;
    AddressBookPage* dlg = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::SendingTab, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setModel(walletModel->getAddressTableModel());
    dlg->show();
}

void WalletView::usedReceivingAddresses()
{
    if (!walletModel)
        return;
    AddressBookPage* dlg = new AddressBookPage(AddressBookPage::ForEditing, AddressBookPage::ReceivingTab, this);
    dlg->setAttribute(Qt::WA_DeleteOnClose);
    dlg->setModel(walletModel->getAddressTableModel());
    dlg->show();
}

void WalletView::showProgress(const QString& title, int nProgress)
{
    if (nProgress == 0) {
        progressDialog = new QProgressDialog(title, "", 0, 100);
        progressDialog->setWindowModality(Qt::ApplicationModal);
        progressDialog->setMinimumDuration(0);
        progressDialog->setCancelButton(0);
        progressDialog->setAutoClose(false);
        progressDialog->setValue(0);
    } else if (nProgress == 100) {
        if (progressDialog) {
            progressDialog->close();
            progressDialog->deleteLater();
        }
    } else if (progressDialog)
        progressDialog->setValue(nProgress);
}

/** Update wallet with the sum of the selected transactions */
void WalletView::trxAmount(QString amount)
{
    transactionSum->setText(amount);
}
