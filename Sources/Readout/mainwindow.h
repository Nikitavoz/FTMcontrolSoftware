#ifndef MAINWINDOW_H
#define MAINWINDOW_H

#include <QMainWindow>
#include <QtWidgets>
#include "ipbusinterface.h"

namespace Ui {
class MainWindow;
}

class MainWindow : public QMainWindow
{
    Q_OBJECT

//	QThread networkThread;
	QString defaultFileName = QCoreApplication::applicationName() + ".ini";
/* */
	IPbusTarget FTM;
	QFile dataFile;
	QTextStream dout;
	QString dataFileName = QCoreApplication::applicationName() + ".GBT";
	QString dataBinFileName = QCoreApplication::applicationName() + ".GBTbin";

	QMetaObject::Connection loopConnection, oneTimeConnection;
    QString defaultIPaddress = "172.20.75.175";
    QTimer *responseTimer = new QTimer(this), *statsTimer = new QTimer(this), *oneTimer = new QTimer(this);
	quint16 FIFOcount; //number of word pairs
	quint32 wordsCount = 0, bitsCount = 0, regsCount = 0, packetsCount = 0, wordsSaved = 0;
	bool FIFOoverflow, dataIsConsistent, HDMIerror = false, ok;
    quint16 nextWordID;
    quint8 nPacketsToTrash;
    quint32 *p;

public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow();
    bool fileRead(QString);
    bool fileWrite(QString);

    inline bool validIPaddress(QString text) {return QRegExp("(0?0?[1-9]|0?[1-9][0-9]|1[0-9][0-9]|2[0-4][0-9]|25[0-5])\\.(([01]?[0-9]?[0-9]|2[0-4][0-9]|25[0-5])\\.){2}(0?0?[1-9]|0?[1-9][0-9]|1[0-9][0-9]|2[0-4][0-9]|25[0-5])").exactMatch(text);}

private slots:

	void recheckTarget();

	void changeIP();

	void save();

	void load();

	void on_buttonStart_clicked();

	void on_buttonStop_clicked();

	void updateStats();

	void checkFIFOcount();

	void frequencyUpdate();

	void on_buttonClear_clicked();

	void on_buttonResetGBTerrors_clicked();

	void on_buttonSwitch_toggled(bool checked);

	void on_lineEditFrequency_editingFinished();

	void on_radioButtonText_toggled(bool checked);

	void on_spinBoxRegFrequency_valueChanged(int arg1);

	void on_radioButtonOdd_toggled(bool checked);

	void on_buttonDismiss_clicked();

	void phaseUpdate();

	void on_lineEdit_ps_editingFinished();

	void on_buttonPlus_clicked();

	void on_buttonMinus_clicked();

	void on_lineEditPattern1_editingFinished();

	void on_lineEditPattern0_editingFinished();

	void on_horizontalSlider_valueChanged(int value);

	void on_horizontalSlider_sliderReleased();

private:
    Ui::MainWindow *ui;
};

#endif // MAINWINDOW_H
