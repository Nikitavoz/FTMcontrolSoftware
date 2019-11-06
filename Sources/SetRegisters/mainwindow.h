#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtWidgets>
#include "ipbusinterface.h"
#include "ui_mainwindow.h"

namespace Ui {
    class MainWindow;
}

const quint8 maxNumberOfRegisters = 24;
const quint32 defaultBaseAddress = 0x00001000;

class MainWindow : public QMainWindow
{
    Q_OBJECT
    IPbusTarget testboard;
    QString defaultIPaddress = "172.20.75.175";
    QString defaultFileName = QCoreApplication::applicationName() + ".ini";

    quint8 nRegisters = maxNumberOfRegisters;
    quint32 baseAddress, data[maxNumberOfRegisters];
    char *pData = reinterpret_cast<char *>(&data);

    bool ok;
    QList<QLineEdit *> addressList, valueList;
    QList<QCheckBox *> checkboxList;
    QRegExpValidator *uint16Validator = new QRegExpValidator(QRegExp("[0-5]?[0-9]{1,4}|6[0-4][0-9]{3}|65[0-4][0-9]{2}|655[0-2][0-9]|6553[0-5]"), this);
    QRegExpValidator *int16Validator = new QRegExpValidator(QRegExp("(\\-?([0-2]?[0-9]{1,4}|3[0-1][0-9]{3}|32[0-6][0-9]{2}|327[0-5][0-9]|3276[0-7])|-32768)"), this);
    QRegExpValidator *hexValidator = new QRegExpValidator(QRegExp("[0-9a-fA-F]{8}"), this);
    QTimer *readingTimer = new QTimer(this), *responseTimer = new QTimer(this);

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    bool fileRead(QString);
    bool fileWrite(QString);
inline bool validIPaddress(QString text) {return QRegExp("(0?0?[1-9]|0?[1-9][0-9]|1[0-9][0-9]|2[0-4][0-9]|25[0-5])\\.(([01]?[0-9]?[0-9]|2[0-4][0-9]|25[0-5])\\.){2}(0?0?[1-9]|0?[1-9][0-9]|1[0-9][0-9]|2[0-4][0-9]|25[0-5])").exactMatch(text);}

private slots:
    void on_addressEdit_0_editingFinished();

    void on_button_read_clicked();

    void on_button_write_clicked();

    void on_amountEdit_editingFinished();

    void on_verticalSlider_valueChanged(int value);

    void recheckTarget();

    void readRegisters();

    void save();

    void load();

    void changeIP();



    void on_radioButtonSeq_clicked(bool checked);

    void on_radioButtonFIFO_clicked(bool checked);

    void on_radioButtonDec_clicked(bool checked);

    void on_radioButtonHex_clicked(bool checked);

private:
    Ui::MainWindow *ui;
};

#endif // MAINWINDOW_H
