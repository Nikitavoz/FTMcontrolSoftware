#include "mainwindow.h"
#include "ui_mainwindow.h"

MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

	QMenu *fileMenu = menuBar()->addMenu("&File");
	fileMenu->addAction("&Save settings to...", this, SLOT(save()), QKeySequence::Save);
	fileMenu->addAction("&Load settings from...", this, SLOT(load()), QKeySequence::Open);
	QMenu *networkMenu = menuBar()->addMenu("&Network");
    networkMenu->addAction("&Recheck target status", this, SLOT(recheckTarget()), QKeySequence::Refresh);
    networkMenu->addAction("&Change target IP adress...", this, SLOT(changeIP()));
	networkMenu->addAction("Init &GBT", &FTM, SLOT(initGBT()));

	this->setWindowTitle(QApplication::applicationName() + " " + QApplication::applicationVersion());
	//ui->buttonResetGBTerrors->setVisible(false);

	ui->lineEdit_ps->setValidator(new QRegExpValidator(QRegExp("\\-?([0-3]?[0-9]{1,4}|4[0-4][0-9]{3}|45000)"), this));
	ui->lineEdit_ps->setToolTip("A number from [-45000; 45000] range");
	ui->lineEditFrequency->setValidator(new QDoubleValidator(1, 9999999, 3));
	ui->buttonSwitch->setIcon(QIcon(":/images/SW0s.png"));

	connect(&FTM, &IPbusTarget::networkError, this, [=](QString message) {
        responseTimer->stop();
		FTM.qsocket->disconnect(SIGNAL(readyRead()));
        on_buttonStop_clicked();
        statusBar()->showMessage(message);
        //QMessageBox::warning(this, "Network error", message);
    });
	connect(&FTM, &IPbusTarget::statusOK, this, [=]() {
		responseTimer->stop();
		regsCount += 16;
		bitsCount += (16 * sizeof(quint32) + 28) * 8;
        statusBar()->showMessage("Ready");
		FTM.requestGBTstatus();
    });
	connect(&FTM, &IPbusTarget::successfulRead, this, [=](quint16 n) { //GTBstatus filled
		regsCount += n;
		bitsCount += ((n + 2) * sizeof(quint32) + 28) * 8;
		++packetsCount;
		if (loopConnection) {
            checkFIFOcount();
        } else updateStats();
    });
	connect(&FTM, &IPbusTarget::successfulWrite, this, [=](quint16 n) {
		bitsCount += ((n + 2) * sizeof(quint32) + 28) * 8;
		regsCount += n;
		++packetsCount;
	});
	connect(&FTM, &IPbusTarget::HDMIstatusReady, this, [=] (bool ok) {
		regsCount += 1;
		bitsCount += ((1 + 2) * sizeof(quint32) + 28) * 8;
		++packetsCount;
		if (!ok) ui->groupBoxHDMI->setEnabled(false);
		else {
			ui->listStatusHDMI->item(0)->setIcon(QIcon(FTM.HDMI.syncComplete?":/images/1Gs.png":":/images/0Rs.png"));
			ui->listStatusHDMI->item(1)->setIcon(QIcon(FTM.HDMI.errorDetected?":/images/1Rs.png":":/images/0Gs.png"));
			if (HDMIerror)
				ui->listStatusHDMI->item(1)->setIcon(QIcon(":/images/1Rs.png"));
			else if (FTM.HDMI.errorDetected) {
				ui->listStatusHDMI->item(1)->setIcon(QIcon(":/images/1Rs.png"));
				HDMIerror = true;
			} else
				ui->listStatusHDMI->item(1)->setIcon(QIcon(":/images/0Gs.png"));
			ui->listStatusLinkLine0->item(0)->setIcon(QIcon(FTM.HDMI.line0linkLost?":/images/1Rs.png":":/images/0Gs.png"));
			ui->listStatusLinkLine0->item(1)->setIcon(QIcon(FTM.HDMI.line0linkStable?":/images/1Gs.png":":/images/0Rs.png"));
			ui->listStatusLinkLine1->item(0)->setIcon(QIcon(FTM.HDMI.line1linkLost?":/images/1Rs.png":":/images/0Gs.png"));
			ui->listStatusLinkLine1->item(1)->setIcon(QIcon(FTM.HDMI.line1linkStable?":/images/1Gs.png":":/images/0Rs.png"));
			ui->listStatusLinkLine2->item(0)->setIcon(QIcon(FTM.HDMI.line2linkLost?":/images/1Rs.png":":/images/0Gs.png"));
			ui->listStatusLinkLine2->item(1)->setIcon(QIcon(FTM.HDMI.line2linkStable?":/images/1Gs.png":":/images/0Rs.png"));
			ui->listStatusLinkLine3->item(0)->setIcon(QIcon(FTM.HDMI.line3linkLost?":/images/1Rs.png":":/images/0Gs.png"));
			ui->listStatusLinkLine3->item(1)->setIcon(QIcon(FTM.HDMI.line3linkStable?":/images/1Gs.png":":/images/0Rs.png"));
			ui->labelDelayLine1->setText(QString::asprintf( "%.2f", double(FTM.HDMI.line1delay * 2.5 / 32) ));
			ui->labelDelayLine2->setText(QString::asprintf( "%.2f", double(FTM.HDMI.line2delay * 2.5 / 32) ));
			ui->labelDelayLine3->setText(QString::asprintf( "%.2f", double(FTM.HDMI.line3delay * 2.5 / 32) ));
		}
	});
	connect(&FTM, &IPbusTarget::readyGBTbunch, this, [=] () {
		wordsCount += 120;
		wordsSaved += 120;
		regsCount += 364;
		++packetsCount;
		bitsCount += 1500 * 8;
		if (nPacketsToTrash > 0) {
			if (--nPacketsToTrash == 0) {
				FIFOoverflow = false;
				nextWordID = (FTM.response[360] >> 16) + 1;
				dataIsConsistent = true;
				statusBar()->showMessage("Reading...");
			}
			return;
		}
		if (dataFile.isOpen() && loopConnection) {
			p = FTM.response + 2; //skip packet header and transaction header
			bool text = ui->radioButtonText->isChecked();
			for (quint16 i=0; i<120; ++i, p+=3) {
                if (i == 80) ++p; //skip 2nd transcation header
                if (*p >> 16 != nextWordID++) {
					//if (text) dout << "! consistency break !" << endl;
                    dataIsConsistent = false;
                    nextWordID = (*p >> 16) + 1;
                }
				if (text)
					dout << QString::asprintf("%04hX%08X%08X\n", quint16(*p), *(p+1), *(p+2));
				else { //binary mode
					dataFile.write(reinterpret_cast<char *>(p + 2), 4);
					dataFile.write(reinterpret_cast<char *>(p + 1), 4);
					dataFile.write(reinterpret_cast<char *>(p), 2);
				}
            }
        }
    });
	connect(&FTM, &IPbusTarget::phaseSet, this, [=]() {
		bitsCount += (2 * sizeof(quint32) + 28) * 8;
		++packetsCount;
		phaseUpdate();
	});
	connect(&FTM, &IPbusTarget::frequencySet, this, &MainWindow::frequencyUpdate);

	oneTimeConnection = connect(&FTM, &IPbusTarget::statusOK, this, [=]() {
		disconnect(oneTimeConnection);
		FTM.initSPI();
		FTM.setPhase();
		//FTM.initGBT();
	});

    oneTimer->setSingleShot(true);
    responseTimer->setSingleShot(true);
    responseTimer->callOnTimeout(this, [=]() {
		on_buttonStop_clicked();
		FTM.qsocket->disconnect(SIGNAL(readyRead()));
		statusBar()->showMessage(FTM.targetIPaddress + ": no response");
    });
    statsTimer->callOnTimeout(this, [=]() {
		if (ui->groupBoxHDMI->isEnabled()) FTM.requestHDMIstatus();
		if (loopConnection)
            updateStats();
		else FTM.requestGBTstatus();
    });

	fileRead(defaultFileName);

	statsTimer->start(1000);
	recheckTarget();

	dataFile.setFileName( ui->radioButtonText->isChecked() ? dataFileName : dataBinFileName );
    dout.setDevice(&dataFile);

	dataFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append);
	wordsSaved = quint32(dataFile.size() / ( ui->radioButtonText->isChecked() ? 22 : 10));
	ui->labelCountMB->setText(QString::number(dataFile.size()/1048576));
	ui->labelCountWords->setText(QString::number(wordsSaved));
//    if (!dataFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append)) {
//        QMessageBox::warning(this, "File error",
//                             QString("Cannot write file %1:\n%2.")
//                             .arg(QDir::toNativeSeparators(dataFileName),
//                                  dataFile.errorString()));
//        statusBar()->showMessage("File error");
//        ui->buttonStart->setEnabled(false);
//    } else {
//		wordsSaved = quint32(dataFile.size() / ( ui->radioButtonText->isChecked() ? 22 : 10));
//		ui->labelCountMB->setText(QString::number(dataFile.size()/1048576));
//		ui->labelCountWords->setText(QString::number(wordsSaved));
//    }
}

MainWindow::~MainWindow() {
    if (dataFile.isOpen()) dataFile.close();
    fileWrite(defaultFileName);
    delete ui;
}

void MainWindow::recheckTarget() {
	statusBar()->showMessage(FTM.targetIPaddress + ": status requested...");
    responseTimer->start(500);
	FTM.reinit();
}

bool MainWindow::fileWrite(QString fileName) {
    QFile file(fileName);
    if (!file.open(QFile::WriteOnly | QFile::Text)) {
        QMessageBox::warning(this, "File error",
                             QString("Cannot write file %1:\n%2.")
                             .arg(QDir::toNativeSeparators(fileName),
                                  file.errorString()));
        return false;
    }
    QTextStream out(&file);
    QApplication::setOverrideCursor(Qt::WaitCursor);
	out << "[Target IP address]" << endl << FTM.targetIPaddress << endl << endl;
	out << "[File mode]" << endl << (ui->radioButtonText->isChecked() ? "text" : "bin") << endl << endl;
	out << "[Rounding]" << endl << (ui->radioButtonOdd->isChecked() ? "odd" : "even") << endl << endl;
	out << "[Frequency in Hz]" << endl << ui->lineEditFrequency->text() << endl << endl;
	out << "[Trigger pattern]" << endl << ui->lineEditPattern1->text() << endl << ui->lineEditPattern0->text() << endl << endl;
	out << "[Phase shift in ps]" << endl << ui->lineEdit_ps->text() << endl;
    QApplication::restoreOverrideCursor();
    file.close();
    return true;
}

bool MainWindow::fileRead(QString fileName) {
    QFile file(fileName);
    if (!file.open(QFile::ReadOnly | QFile::Text)) {
        QMessageBox::warning(this, "File error",
                             QString("Cannot read file %1:\n%2.")
                             .arg(QDir::toNativeSeparators(fileName), file.errorString()));
        return false;
    }
    QTextStream in(&file);
    QApplication::setOverrideCursor(Qt::WaitCursor);
    QString s; //a line being analyzed
	while (!in.atEnd()) {
		s = in.readLine();
        if (s == "" || s.front() == ';' || s.front() == '#') continue;
        if (s == "[Target IP address]") {
            s = in.readLine();
			FTM.targetIPaddress = validIPaddress(s)?s:defaultIPaddress;
			continue;
        }
		if (s == "[File mode]") {
			s = in.readLine().toLower();
			if (s == "text") ui->radioButtonText->setChecked(true); else if (s == "bin") ui->radioButtonBinary->setChecked(true);
			continue;
		}
		if (s == "[Rounding]") {
			s = in.readLine().toLower();
			if (s == "even") {
				ui->radioButtonEven->setChecked(true);
				ui->spinBoxRegFrequency->setMinimum(40);
			} else if (s == "odd") {
				ui->radioButtonOdd->setChecked(true);
				ui->spinBoxRegFrequency->setMinimum(41);
			}
			continue;
		}
		if (s == "[Frequency in Hz]") {
			s = in.readLine();
			ui->lineEditFrequency->setText(s);
			on_lineEditFrequency_editingFinished();
			continue;
		}
		if (s == "[Trigger pattern]") {
			s = in.readLine();
			quint32 v;
			v = s.toUInt(&ok, 16);
			if (ok) {
				ui->lineEditPattern1->setText(s);
				FTM.GBT.Control.TriggerContinuousPattern1 = v;
			}
			s = in.readLine();
			v = s.toUInt(&ok, 16);
			if (ok) {
				ui->lineEditPattern0->setText(s);
				FTM.GBT.Control.TriggerContinuousPattern0 = v;
			}
			continue;
		}
		if (s == "[Phase shift in ps]") {
			s = in.readLine();
			qint32 ps = s.toInt(&ok);
			if (ok) {
				if (ps > 45000) ps = 45000;
				if (ps < -45000) ps = -45000;
				FTM.phaseShift = qint16(round(ps / 9.765625));
			}
		}
    }
    QApplication::restoreOverrideCursor();
    file.close();
    return true;
}

void MainWindow::load() {
	QFileDialog dialog(this);
	dialog.setWindowModality(Qt::WindowModal);
	dialog.setAcceptMode(QFileDialog::AcceptOpen);
	if (dialog.exec() != QDialog::Accepted || !fileRead(dialog.selectedFiles().first()))
		statusBar()->showMessage("File not loaded");
	else
		statusBar()->showMessage("File loaded", 2000);
}

void MainWindow::save() {
	QFileDialog dialog(this);
	dialog.setWindowModality(Qt::WindowModal);
	dialog.setAcceptMode(QFileDialog::AcceptSave);
	if (dialog.exec() != QDialog::Accepted || !fileWrite(dialog.selectedFiles().first()))
		statusBar()->showMessage("File not saved");
	else
		statusBar()->showMessage("File saved", 2000);
}

void MainWindow::changeIP() {
	QString text = QInputDialog::getText(this, "Changing target IP address", "Enter new address", QLineEdit::Normal, FTM.targetIPaddress, &ok);
    if (ok && !text.isEmpty()) {
        if (validIPaddress(text)) {
			FTM.targetIPaddress = text;
            recheckTarget();
        } else statusBar()->showMessage(text + ": invalid IP address");
    }
}

void MainWindow::updateStats() {
	ui->labelCountWords->setText(QString::number(wordsSaved));
	ui->labelCountMB->setText(QString::number(wordsSaved/1024*( ui->radioButtonText->isChecked() ? 22 : 10)/1024));
	ui->labelSpeedWords->setText(QString::number(wordsCount));
	wordsCount = 0;
	ui->labelSpeedReg->setText(QString::number(regsCount));
	regsCount = 0;
	ui->labelSpeedPacket->setText(QString::number(packetsCount));
	packetsCount = 0;
	ui->labelSpeedMbit->setText(QString::asprintf("%.3f", double(bitsCount) / 1000000));
	bitsCount = 0;
	for (quint8 i=0; i<7; ++i) ui->listStatusGBT->item(i)->setIcon(QIcon((FTM.GBT.Status.GBTstatus & (1 << i))?":/images/1Gs.png":":/images/0Rs.png"));
	for (quint8 i=7; i<10; ++i) ui->listStatusGBT->item(i)->setIcon(QIcon((FTM.GBT.Status.GBTstatus & (1 << i))?":/images/1Rs.png":":/images/0Gs.png"));
	FIFOcount = FTM.GBT.Status.SelectorFIFOcount;
	ui->listFIFOcount->item(0)->setText(QString::number(FTM.GBT.Status.SelectorFIFOcount));
    if (loopConnection && FIFOcount > 1986) FIFOoverflow = true;
	ui->labelConsistency->setPixmap(QPixmap(dataIsConsistent?":/images/vs.png":":/images/xs.png"));
}

void MainWindow::checkFIFOcount() {
	FIFOcount = FTM.GBT.Status.SelectorFIFOcount;
    if (FIFOcount > 1986) FIFOoverflow = true;
    //dout << QString::number(FIFOcount) << " words left in FIFO\n";
    if (FIFOcount > 120)
		FTM.requestGBTdata();
    else {
		FTM.requestGBTstatus();
    }
}

void MainWindow::on_buttonStart_clicked() {
	if (FIFOcount < 64) {
		statusBar()->showMessage("Not enough data in FIFO");
		return;
	}
	ui->radioButtonText->setEnabled(false);
	ui->radioButtonBinary->setEnabled(false);
	if (!dataFile.isOpen()) dataFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append);
	//if (dataFile.size() > 0 || ui->radioButtonText->isChecked()) dout << "! consistency break !" << endl;
    ui->buttonStart->setEnabled(false);
    ui->buttonStop->setEnabled(true);
    oneTimer->callOnTimeout(this, [=]() {disconnect(oneTimeConnection);});
	oneTimeConnection = connect(&FTM, &IPbusTarget::readoutSynced, this, [=]() {
        oneTimer->stop();
        disconnect(oneTimeConnection);
		FIFOcount -= 2; //wasted for sync
		nPacketsToTrash = quint8(FIFOcount / 60) + 1; //usually is 35
		statusBar()->showMessage("Dumping old data... ");
		loopConnection = connect(&FTM, &IPbusTarget::readyGBTbunch, this, &MainWindow::checkFIFOcount);
		FTM.requestGBTdata();
    });
    oneTimer->start(500);
	FTM.syncGBTreadout();
}

void MainWindow::on_buttonStop_clicked() {
    disconnect(loopConnection);
	ui->radioButtonText->setEnabled(true);
	ui->radioButtonBinary->setEnabled(true);
	if (dataFile.isOpen()) dataFile.close();
    ui->buttonStart->setEnabled(true);
    ui->buttonStop->setEnabled(false);
    statusBar()->showMessage("Ready");
}

void MainWindow::on_buttonClear_clicked() {
    if (dataFile.isOpen()) dataFile.close();
    dataFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate);
	wordsSaved = 0;
}

void MainWindow::on_buttonResetGBTerrors_clicked() {
	FTM.resetGBTerrors();
}

void MainWindow::frequencyUpdate() {
	bitsCount += (2 * sizeof(quint32) + 28) * 8;
	++packetsCount;
	double frequency = 40000000. / FTM.genFrequency;
	ui->lineEditFrequency->setText(QString::asprintf(frequency < 100000 ? "%.2f" : "%.0f", frequency));
}

void MainWindow::on_buttonSwitch_toggled(bool checked) {
	FTM.setFrequency(checked);
	ui->buttonSwitch->setIcon(QIcon(checked?":/images/SW1s.png":":/images/SW0s.png"));
	QFont f(ui->labelTextOn->font());
	ui->labelTextOff->setFont(f);
	f.setBold(checked);
	ui->labelTextOn->setFont(f);
}

void MainWindow::on_lineEditFrequency_editingFinished() {
	QString st = ui->lineEditFrequency->text();
	if (st.contains(',')) st.replace(',', '.');
	quint16 regValue;
	double frequency = st.toDouble(&ok);
	if (ok) {
		bool odd = ui->radioButtonOdd->isChecked();
		if (frequency >= 1000000)
			regValue = odd ? 41 : 40; //0x28 for max frequency of 1MHz, 0x29 for max frequency with odd regvalue
		else if (frequency < 40000000. / 0xFFFF)
			regValue = 0xFFFF; //min frequency is 610.36 Hz
		else
			regValue = quint16(40000000. / frequency);
		if (regValue % 2) { //if regValue is odd
			if (!odd) --regValue; //correcting to even
		} else
			if (odd) ++regValue; //correcting to odd
		if (quint16(ui->spinBoxRegFrequency->value()) == regValue) {
			FTM.genFrequency = regValue;
			frequencyUpdate();
		} else
			ui->spinBoxRegFrequency->setValue(regValue);
	}
}

void MainWindow::on_spinBoxRegFrequency_valueChanged(int regValue) {
	FTM.genFrequency = quint16(regValue);
	if (ui->buttonSwitch->isChecked())
		FTM.setFrequency(true);
	else frequencyUpdate();
}

void MainWindow::on_radioButtonOdd_toggled(bool checked) {
	checked ? ++FTM.genFrequency : --FTM.genFrequency;
	ui->spinBoxRegFrequency->setMinimum(checked ? 41 : 40);
	ui->spinBoxRegFrequency->setValue(FTM.genFrequency);
}

void MainWindow::on_radioButtonText_toggled(bool checked) {
	if (dataFile.isOpen()) dataFile.close();
	dataFile.setFileName( checked ? dataFileName : dataBinFileName );
	dataFile.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Append);
	wordsSaved = quint32(dataFile.size() / ( checked ? 22 : 10));
	ui->labelCountMB->setText(QString::number(dataFile.size() / 1048576));
	ui->labelCountWords->setText(QString::number(wordsSaved));
}

void MainWindow::on_buttonDismiss_clicked() {
	HDMIerror = false;
}

void MainWindow::phaseUpdate() {
	qint32 ps = qint32(round(FTM.phaseShift * 9.765625));
	ui->lineEdit_ps->setText(QString::number(ps));
	ui->horizontalSlider->setValue(ps);

}

void MainWindow::on_lineEdit_ps_editingFinished() {
	FTM.phaseShift = qint16(round(ui->lineEdit_ps->text().toInt()/9.765625));
	phaseUpdate();
}

void MainWindow::on_buttonPlus_clicked() {
	if (FTM.phaseShift < 4608) ++FTM.phaseShift; //4608 * 9.765625 = 45000
	phaseUpdate();
}

void MainWindow::on_buttonMinus_clicked() {
	if (FTM.phaseShift > -4608) --FTM.phaseShift; //-4608 * 9.765625 = -45000
	phaseUpdate();
}

void MainWindow::on_lineEditPattern1_editingFinished() {
	FTM.GBT.Control.TriggerContinuousPattern1 = ui->lineEditPattern1->text().toUInt(&ok, 16);
	FTM.setTRGpattern1();
}

void MainWindow::on_lineEditPattern0_editingFinished() {
	FTM.GBT.Control.TriggerContinuousPattern0 = ui->lineEditPattern0->text().toUInt(&ok, 16);
	FTM.setTRGpattern0();
}

void MainWindow::on_horizontalSlider_valueChanged(int value)
{
	FTM.phaseShift = qint16(round(value/9.765625));
	phaseUpdate();
}

void MainWindow::on_horizontalSlider_sliderReleased()
{
	FTM.setPhase();
}
