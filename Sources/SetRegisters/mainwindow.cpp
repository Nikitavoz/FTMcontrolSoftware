#include "mainwindow.h"


MainWindow::MainWindow(QWidget *parent) :
    QMainWindow(parent),
    ui(new Ui::MainWindow)
{
    ui->setupUi(this);

    addressList = centralWidget()->findChildren<QLineEdit *>(QRegularExpression("addressEdit_.*"));
    valueList = centralWidget()->findChildren<QLineEdit *>(QRegularExpression("valueEdit_.*"));
    checkboxList = centralWidget()->findChildren<QCheckBox *>(QRegularExpression("checkBox_.*"));
    for (quint8 i=0; i<maxNumberOfRegisters; ++i) {
        checkboxList.at(i)->setVisible(false);
        connect(checkboxList.at(i), &QCheckBox::clicked, this, [=](bool checked) {
            if (checked) {
                quint16 u = valueList.at(i)->text().toUShort();
                valueList.at(i)->setText(QString::asprintf("%hd", qint16(u)));
                valueList.at(i)->setValidator(int16Validator);
            } else {
                qint16 s = valueList.at(i)->text().toShort();
                valueList.at(i)->setText(QString::asprintf("%hu", quint16(s)));
                valueList.at(i)->setValidator(uint16Validator);
            }
        });
    }
    ui->labelSigned->setVisible(false);
    QMenu *fileMenu = menuBar()->addMenu("&File");
    fileMenu->addAction("&Save registers to...", this, SLOT(save()), QKeySequence::Save);
    fileMenu->addAction("&Load registers from...", this, SLOT(load()), QKeySequence::Open);
    QMenu *networkMenu = menuBar()->addMenu("&Network");
    networkMenu->addAction("&Recheck target status", this, SLOT(recheckTarget()), QKeySequence::Refresh);
    networkMenu->addAction("&Change target IP adress...", this, SLOT(changeIP()));
    networkMenu->addAction("&Send custom packet", this, [=]() {
        for (quint8 i=0; i<nRegisters; ++i) data[i] = valueList.at(i)->text().toUInt(&ok, 16);
        testboard.qsocket->write(pData, nRegisters * sizeof(quint32));
        statusBar()->showMessage(testboard.targetIPaddress + ": " + QString::number(nRegisters) + " values sent");
        responseTimer->start(500);
        connect(testboard.qsocket, &QUdpSocket::readyRead, this, [=]() {
            responseTimer->stop();
            testboard.qsocket->disconnect(SIGNAL(readyRead()));
            qint16 n = qint16(testboard.qsocket->read(pData, maxNumberOfRegisters * 4)); //4 == sizeof(quint32), number of bytes per register
            quint16 m = quint16(testboard.qsocket->bytesAvailable());
            if (m > 0) {
                testboard.qsocket->readAll();
                n += m;
            }
            if (n == -1) {
                statusBar()->showMessage("Socket read error");
            } else if (n == 0 || n % 4 != 0)
                statusBar()->showMessage(testboard.targetIPaddress + ": " + QString::number(n) + " bytes received");
            else {
                nRegisters = (n>(maxNumberOfRegisters * 4))?maxNumberOfRegisters:quint8(n/4);
                ui->verticalSlider->setValue(nRegisters);
                for (quint8 i=0; i<nRegisters; ++i) valueList.at(i)->setText(QString::asprintf("%08X", data[i]));
                statusBar()->showMessage(testboard.targetIPaddress + ": " + QString::number(n/4) + " values received");
            }
        });
    });

    connect(&testboard, &IPbusTarget::networkError, this, [=](QString message) {
        responseTimer->stop();
        testboard.qsocket->disconnect(SIGNAL(readyRead()));
        if (testboard.OKconnection) disconnect(testboard.OKconnection);
        statusBar()->showMessage(message);
        //QMessageBox::warning(this, "Network error", message);
    });
    connect(&testboard, &IPbusTarget::statusOK, this, [=]() {
        responseTimer->stop();
        statusBar()->showMessage(testboard.targetIPaddress + " IPbus status: OK");
    });
    responseTimer->setSingleShot(true);
    responseTimer->callOnTimeout(this, [=]() {
        if (testboard.OKconnection) disconnect(testboard.OKconnection);
        testboard.qsocket->disconnect(SIGNAL(readyRead()));
        statusBar()->showMessage(testboard.targetIPaddress + ": no response");
    });
    readingTimer->callOnTimeout(this, &MainWindow::readRegisters);
    connect(&testboard, &IPbusTarget::successfulRead, this, [=](quint16 nWords) {
        if (ui->radioButtonHex->isChecked()) for (quint8 i=0; i<nWords; ++i)
            valueList.at(i)->setText(QString::asprintf("%08X", data[i]));
        else for (quint8 i=0; i<nWords; ++i)
                if (checkboxList.at(i)->isChecked())
                    valueList.at(i)->setText( QString::asprintf("%hd", qint16(data[i])) );
                else
                    valueList.at(i)->setText( QString::asprintf("%hu", quint16(data[i])) );
        statusBar()->showMessage("Successfully read " + QString::number(nWords) + " words", 500);
    });
    connect(&testboard, &IPbusTarget::successfulWrite, this, [=](quint16 nWords) {
        statusBar()->showMessage("Successfully written " + QString::number(nWords) + " words");
    });

    fileRead(defaultFileName);
    recheckTarget();
}

MainWindow::~MainWindow() {
    fileWrite(defaultFileName);
    delete ui;
}

void MainWindow::recheckTarget() {
    statusBar()->showMessage(testboard.targetIPaddress + ": status requested...");
    responseTimer->start(500);
    testboard.reinit();
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
    out << "[Target IP address]" << endl << testboard.targetIPaddress << endl << endl
        << "[Base register address]" << endl << ui->addressEdit_0->text() << endl << endl
        << "[Sequential address]" << endl << (ui->radioButtonSeq->isChecked()?"true":"false") << endl << endl
        << "[Number of registers]" << endl << ui->amountEdit->text() << endl << endl
        << "[Registers]" << endl;
    bool hex = ui->radioButtonHex->isChecked();
    if (hex)
        for (quint8 i=0; i<maxNumberOfRegisters; ++i) out << valueList.at(i)->text() << endl;
    else
        for (quint8 i=0; i<maxNumberOfRegisters; ++i) {
            QString t = valueList.at(i)->text();
            data[i] = (data[i] & 0xffff0000) + quint32(checkboxList.at(i)->isChecked()?t.toShort():t.toUShort());
            out << QString::asprintf("%08X", data[i]) << endl;
        }
    out << endl << "[Values base]" << endl << (hex?"Hex":"Dec") << endl << endl
        << "[Signed flags]" << endl;
    for (quint8 i=0; i<maxNumberOfRegisters; ++i) out << (checkboxList.at(i)->isChecked()?'s':'u');
    out << endl;
    QApplication::restoreOverrideCursor();
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
    int nValuesFound = 0;
    bool hex = true;
    QString s; //a line being analyzed
    while (!in.atEnd()) {
        s = in.readLine();
        if (s == "" || s.front() == ';' || s.front() == '#') continue;
        if (s == "[Target IP address]") {
            s = in.readLine();
            testboard.targetIPaddress = validIPaddress(s)?s:defaultIPaddress;
        } else if (s == "[Base register address]") {
            s = in.readLine();
            baseAddress = s.toUInt(&ok, 16);
            if (ok) {
                ui->addressEdit_0->setText(s);
            } else {
                baseAddress = defaultBaseAddress;
            }
            ui->addressEdit_0->setText(QString::asprintf("%08X", baseAddress));
            on_addressEdit_0_editingFinished();
        } else if (s == "[Sequential address]") {
            if (in.readLine() == "true") on_radioButtonSeq_clicked(true);
        } else if (s == "[Number of registers]") {
            ui->amountEdit->setText(in.readLine());
            on_amountEdit_editingFinished();
        } else if (s == "[Registers]") {
            s = in.readLine();
            while (s != "" && nValuesFound < maxNumberOfRegisters && !in.atEnd()) {
                data[nValuesFound] = s.toUInt(&ok, 16); //32bit hex check
                if (ok) {
                    valueList.at(nValuesFound)->setText(QString::asprintf("%08X", data[nValuesFound]));
                    ++nValuesFound;
                }
                s = in.readLine();
            }
        } else if (s == "[Values base]") {
            if (in.readLine().toLower() == "dec") hex = false;
        } else if (s == "[Signed flags]") {
            s = in.readLine();
            quint8 n = quint8(s.size());
            if (n > maxNumberOfRegisters) n = maxNumberOfRegisters;
            for (quint8 i=0; i<n; ++i) checkboxList.at(i)->setChecked(s.at(i)=='s');
        } else {
            data[nValuesFound] = s.toUInt(&ok, 16); //32bit hex check
            if (ok && (nValuesFound < maxNumberOfRegisters)) {
                valueList.at(nValuesFound)->setText(QString::asprintf("%08X", data[nValuesFound]));
                ++nValuesFound;
            }
        }
    }
    if (!hex) ui->radioButtonDec->click();
    QApplication::restoreOverrideCursor();
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
    bool ok;
    QString text = QInputDialog::getText(this, "Changing target IP address", "Enter new address", QLineEdit::Normal, testboard.targetIPaddress, &ok);
    if (ok && !text.isEmpty()) {
        if (validIPaddress(text)) {
            testboard.targetIPaddress = text;
            recheckTarget();
        } else statusBar()->showMessage(text + ": invalid IP address");
    }
}

void MainWindow::on_addressEdit_0_editingFinished() {
    baseAddress = ui->addressEdit_0->text().toUInt(&ok, 16);
    bool sequential = ui->radioButtonSeq->isChecked();
    for (quint8 i=1; i<maxNumberOfRegisters; ++i)
        addressList.at(i)->setText(sequential?QString::asprintf("%08X", baseAddress + i):ui->addressEdit_0->text());
}

void MainWindow::readRegisters() {
    statusBar()->showMessage(testboard.targetIPaddress + ": reading requested...");
    responseTimer->start(500);
    testboard.readRegisters(data, nRegisters, baseAddress, ui->radioButtonFIFO->isChecked());
}

void MainWindow::on_button_read_clicked() {
    if (ui->button_read->text() == "READ") {
        readRegisters();
        if (ui->checkBox->isChecked()) {
            ui->checkBox->setEnabled(false);
            ui->button_write->setEnabled(false);
            ui->button_read->setText("STOP");
            readingTimer->start(1000);
        }
    } else {
        readingTimer->stop();
        ui->checkBox->setEnabled(true);
        ui->button_write->setEnabled(true);
        ui->button_read->setText("READ");
    }
}

void MainWindow::on_button_write_clicked() {
	if (ui->radioButtonHex->isChecked())
		for (quint8 i=0; i<nRegisters; ++i) data[i] = valueList.at(i)->text().toUInt(&ok, 16);
	else
		for (quint8 i=0; i<nRegisters; ++i)
			if (checkboxList.at(i)->isChecked())
				data[i] = quint32(valueList.at(i)->text().toInt());
			else
				data[i] = quint32(valueList.at(i)->text().toUInt());
    statusBar()->showMessage(testboard.targetIPaddress + ": writing requested...");
    responseTimer->start(500);
    testboard.writeRegisters(data, nRegisters, baseAddress, ui->radioButtonFIFO->isChecked());
}

void MainWindow::on_amountEdit_editingFinished()
{
    nRegisters = quint8(ui->amountEdit->text().toUInt(&ok));
    if (nRegisters > maxNumberOfRegisters || !ok) {
        nRegisters = maxNumberOfRegisters;
        ui->amountEdit->setText(QString::number(maxNumberOfRegisters));
    } else if (nRegisters < 1) {
        nRegisters = 1;
        ui->amountEdit->setText("1");
    }
    ui->verticalSlider->setValue(nRegisters);
}

void MainWindow::on_verticalSlider_valueChanged(int value)
{
    nRegisters = quint8(value);
    ui->amountEdit->setText(QString::number(nRegisters));
    for (quint8 i=0; i<nRegisters; ++i) valueList.at(i)->setEnabled(true);
    for (quint8 i=nRegisters; i<maxNumberOfRegisters; ++i) valueList.at(i)->setEnabled(false);
}

void MainWindow::on_radioButtonSeq_clicked(bool checked)
{
    if (checked) {
        ui->radioButtonFIFO->setChecked(false);
        for (quint8 i=1; i<maxNumberOfRegisters; ++i) addressList.at(i)->setText(QString::asprintf("%08X", baseAddress + i));
    } else ui->radioButtonSeq->setChecked(true);
}

void MainWindow::on_radioButtonFIFO_clicked(bool checked)
{
    if (checked) {
        ui->radioButtonSeq->setChecked(false);
        for (quint8 i=1; i<maxNumberOfRegisters; ++i) addressList.at(i)->setText(ui->addressEdit_0->text());
    } else ui->radioButtonFIFO->setChecked(true);
}

void MainWindow::on_radioButtonDec_clicked(bool checked)
{
    if (checked) {
        ui->radioButtonHex->setChecked(false);
        for (quint8 i=0; i<maxNumberOfRegisters; ++i) {
            checkboxList.at(i)->setVisible(true);
            data[i] = valueList.at(i)->text().toUInt(&ok, 16);
            valueList.at(i)->setInputMask("");
            if (checkboxList.at(i)->isChecked()) {
                valueList.at(i)->setValidator(int16Validator);
                valueList.at(i)->setText(QString::asprintf("%hd", qint16(data[i])));
            }
            else {
                valueList.at(i)->setValidator(uint16Validator);
                valueList.at(i)->setText(QString::asprintf("%hu", quint16(data[i])));
            }
        }
        ui->labelSigned->setVisible(true);
    } else ui->radioButtonDec->setChecked(true);
}

void MainWindow::on_radioButtonHex_clicked(bool checked)
{
    if (checked) {
        ui->radioButtonDec->setChecked(false);
        for (quint8 i=0; i<maxNumberOfRegisters; ++i) {
            checkboxList.at(i)->setVisible(false);
            QString t = valueList.at(i)->text();
            data[i] = (data[i] & 0xffff0000) + quint32(checkboxList.at(i)->isChecked()?t.toShort():t.toUShort());
            valueList.at(i)->setValidator(nullptr);
            valueList.at(i)->setInputMask("HHHHHHHH");
            valueList.at(i)->setText(QString::asprintf("%08X", data[i]));
        }
        ui->labelSigned->setVisible(false);
    } else ui->radioButtonHex->setChecked(true);
}
