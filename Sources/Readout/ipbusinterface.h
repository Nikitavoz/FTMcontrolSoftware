#ifndef IPBUSINTERFACE_H
#define IPBUSINTERFACE_H

#include "gbt.h"

const quint16 maxWordsPerPacket = 368; //limit from ethernet MTU of 1500 bytes

class IPbusTarget: public QObject {
    Q_OBJECT
public:
	QString targetIPaddress;
//	QUdpSocket *controlSocket = new QUdpSocket(this);
	QUdpSocket *readoutSocket = new QUdpSocket(this);
//	QString dataFileName = QCoreApplication::applicationName() + ".GBT";
//	QFile dataFile;
//	QTextStream dout;
/* */
    QMetaObject::Connection OKconnection;
	QUdpSocket *qsocket = new QUdpSocket(this);
    quint16 nextPacketID = 0;
    StatusPacket statusPacket;
    GBTmodule GBT;
	quint16 genFrequency = 0x0000;

	HDMIlinkStatus HDMI;

	quint32 SPIaddress = 0x00002000;
	qint16 phaseShift = 0;

    quint16 requestSize = 0, responseSize = 0;
    quint32 request[maxWordsPerPacket], response[maxWordsPerPacket];
    char *pRequest = reinterpret_cast<char *>(request);
    char *pResponse = reinterpret_cast<char *>(response);

    IPbusTarget() {
        connect(qsocket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error), this, [=](QAbstractSocket::SocketError socketError) {emit networkError("Network Error " + QString::number(socketError) + ": " + qsocket->errorString());});
        connect(qsocket, SIGNAL(connected()), this, SLOT(requestStatus()));
        connect(qsocket, SIGNAL(disconnected()), this, SLOT(reinit()));
        qsocket->bind(QHostAddress::AnyIPv4, 50002);
		readoutSocket->bind(QHostAddress::AnyIPv4, 50003);
    }

    ~IPbusTarget() {
        qsocket->disconnect(SIGNAL(disconnected()));
        qsocket->disconnectFromHost();
		readoutSocket->disconnectFromHost();
    }

private:
    bool writingOK() {
        qint16 n = qint16(qsocket->write(pRequest, requestSize));
        if (n == -1) {
            emit networkError("Socket write error: " + qsocket->errorString());
            return false;
        } else if (n != requestSize) {
            emit networkError("Sending packet failed");
            return false;
        } else return true;
    }

    bool readingOK() {
        qsocket->disconnect(SIGNAL(readyRead()));
        qint16 n = qint16(qsocket->read(pResponse, responseSize));
        qint16 m = qint16(qsocket->bytesAvailable());
        if (m > 0) qsocket->readAll();
        if (n == -1) {
            emit networkError("Socket read error");
            return false;
        } else if (n == 0) {
            emit networkError(targetIPaddress + ": empty response, no IPbus");
            return false;
        } else if (response[0] != request[0]) {
            emit networkError(targetIPaddress + QString::asprintf(": incorrect response (%dB)", n + m));
            return false;
        } else {
            responseSize = quint16(n);
            return true;
        }
    }

public:
signals:
    void networkError(QString);
    void statusOK();
    void successfulRead(quint16);
    void successfulWrite(quint16);
    void readyGBTbunch();
    void readoutSynced();
	void HDMIstatusReady(bool);
	void phaseSet();
	void frequencySet();

public slots:
    void reinit() {
		if (qsocket->state() == QAbstractSocket::ConnectedState) {
            qsocket->disconnectFromHost();
			if (readoutSocket->state() == QAbstractSocket::ConnectedState) readoutSocket->disconnectFromHost();
		} else
            qsocket->connectToHost(targetIPaddress, 50001, QIODevice::ReadWrite, QAbstractSocket::IPv4Protocol);
    }

    void requestStatus() {
        requestSize = sizeof(statusPacket);
        memcpy(pRequest, &statusPacket, requestSize);
        connect(qsocket, SIGNAL(readyRead()), this, SLOT(receiveStatus()));
        responseSize = requestSize;
        if (!writingOK()) {
            qsocket->disconnect(SIGNAL(readyRead()));
        }
    }

    void receiveStatus() {
        if (readingOK()) {
            nextPacketID = PacketHeader(qFromBigEndian(response[3])).PacketID;
			if (readoutSocket->state() != QAbstractSocket::ConnectedState) readoutSocket->connectToHost(targetIPaddress, 50001, QIODevice::ReadWrite, QAbstractSocket::IPv4Protocol);
			requestHDMIstatus();
			emit statusOK();
        }
    }

	void requestHDMIstatus() {
		requestSize = 3 * sizeof(quint32);
		request[0] = quint32(PacketHeader(control, 0));
		request[1] = quint32(TransactionHeader(read, 1, 0));
		request[2] = HDMIaddress;
		responseSize = 3 * sizeof(quint32);
		if (writingOK()) {
			if (qsocket->waitForReadyRead(500)) {
				if (readingOK()) {
					TransactionHeader th(response[1]);
					HDMI.reg = response[2];
					emit HDMIstatusReady(th.InfoCode == 0);
				}
			} else emit networkError("no response");
		}
	}

    void readRegisters(quint32 *data, quint16 nWords, quint32 baseAddress, bool FIFO = false) {if (nWords > 0) {
        if (nWords + (nWords>255?3:2) > maxWordsPerPacket)
            emit networkError("Requested packet exceeds MTU");
        else {
            if (OKconnection) disconnect(OKconnection);
            OKconnection = connect(this, &IPbusTarget::statusOK, this, [=]() {//to request reading only after statusOK is emitted
                disconnect(OKconnection); //to ignore next statusOK signals
                requestReading(data, nWords, baseAddress, FIFO);
            });
            requestStatus();
        }
    }}

    void requestReading(quint32 *data, quint16 nWords, quint32 baseAddress, bool FIFO = false) {
        request[0] = quint32(PacketHeader(control, nextPacketID));
        if (nWords > 255) {
            requestSize = 5 * sizeof(quint32);
            request[1] = quint32(TransactionHeader(FIFO?nonIncrementingRead:read, 255, 0));
            request[2] = baseAddress;
            request[3] = quint32(TransactionHeader(FIFO?nonIncrementingRead:read, quint8(nWords - 255), 1));
            request[4] = FIFO?baseAddress:(baseAddress + 255);
        } else {
            requestSize = 3 * sizeof(quint32);
            request[1] = quint32(TransactionHeader(FIFO?nonIncrementingRead:read, quint8(nWords)));
            request[2] = baseAddress;
        }
        if (writingOK()) {
            responseSize = (nWords + (nWords>255?3:2)) * sizeof(quint32);
            connect(qsocket, &QUdpSocket::readyRead, this, [=]() {receiveReadResponse(data, nWords);});
        }
    }

    void receiveReadResponse(quint32 *data, quint16 nWords) {
        if (readingOK()) {
            TransactionHeader th(response[1]);
            for (quint8 i=0; i<th.Words; ++i) data[i] = response[2 + i];
            quint16 nWordsReceived = th.Words;
            if (nWords > 255 && (responseSize > (nWordsReceived + 2) * sizeof(quint32))) {
                th = TransactionHeader(response[257]);
                for (quint8 i=0; i<(nWords - 255); ++i) data[255 + i] = response[258 + i];
                nWordsReceived += th.Words;
            }
            emit successfulRead(nWordsReceived);
            if (th.InfoCode != 0) emit networkError(th.infoCodeString());
        }
    }

    void writeRegisters(quint32 *data, quint16 nWords, quint32 baseAddress, bool FIFO = false) {
        if (nWords == 0) return;
        if (nWords + (nWords>255?5:3) > maxWordsPerPacket)
            emit networkError("Request exceeds MTU");
        else {
            if (OKconnection) disconnect(OKconnection);
            OKconnection = connect(this, &IPbusTarget::statusOK, this, [=]() {//to request writing only after statusOK is emitted
                disconnect(OKconnection); //to ignore next statusOK signals
                requestWriting(data, nWords, baseAddress, FIFO);
            });
            requestStatus();
        }
    }

    void requestWriting(quint32 *data, quint16 nWords, quint32 baseAddress, bool FIFO = false) {
        request[0] = quint32(PacketHeader(control, nextPacketID));
        if (nWords > 255) {
            requestSize = (nWords + 5) * sizeof(quint32);
            request[1] = quint32(TransactionHeader(FIFO?nonIncrementingWrite:write, 255, 0));
            request[2] = baseAddress;
            for (quint8 i=0; i<255; ++i) request[3 + i] = data[i];
            request[258] = quint32(TransactionHeader(FIFO?nonIncrementingWrite:write, quint8(nWords - 255), 1));
            request[259] = FIFO?baseAddress:(baseAddress + 255);
            for (quint8 i=0; i<(nWords - 255); ++i) request[260 + i] = data[255 + i];
        } else {
            requestSize = (nWords + 3) * sizeof(quint32);
            request[1] = quint32(TransactionHeader(FIFO?nonIncrementingWrite:write, quint8(nWords)));
            request[2] = baseAddress;
            for (quint8 i=0; i<nWords; ++i) request[3 + i] = data[i];
        }
        if (writingOK()) {
            responseSize = (nWords>255?3:2) * sizeof(quint32);
            connect(qsocket, &QUdpSocket::readyRead, this, [=]() {receiveWriteResponse(nWords);});
        }
    }

    void receiveWriteResponse(quint16 nWords) {
        if (readingOK()) {
            TransactionHeader th(response[1]);
            if (th.InfoCode != 0) {emit networkError(th.infoCodeString()); return;}
            if (nWords > 255) {
                th = TransactionHeader(response[2]);
                if (th.InfoCode != 0) {emit networkError(th.infoCodeString()); return;}
            }
            emit successfulWrite(nWords);
        }
    }
    void requestGBTstatus() {
		connect(readoutSocket, SIGNAL(readyRead()), this, SLOT(readGBTstatus()));
		readoutSocket->write(GBT.pStatusRequest, sizeof(GBT.statusRequest));
    }

    void readGBTstatus() {
		readoutSocket->disconnect(SIGNAL(readyRead()));
		readoutSocket->read(pResponse, 6 * sizeof(quint32));
		//GBT.Status = reinterpret_cast<StatusData *>(response + 2);
		memcpy(&GBT.Status, response + 2, 16);
		emit successfulRead(4);
    }

    void requestGBTdata() {
		connect(readoutSocket, SIGNAL(readyRead()), this, SLOT(readGBTdata()));
		readoutSocket->write(GBT.pDataRequest, sizeof(GBT.dataRequest));
    }

    void readGBTdata() {
		readoutSocket->disconnect(SIGNAL(readyRead()));
		readoutSocket->read(pResponse, 368 * sizeof(quint32));
		//GBT.Status = reinterpret_cast<StatusData *>(response + 364);
		memcpy(&GBT.Status, response + 364, 4 * sizeof(quint32));
		emit readyGBTbunch();
    }

    void resetGBTerrors() {
		requestSize = 9 * sizeof(quint32);
        request[0] = quint32(PacketHeader(control, 0));
        request[1] = quint32(TransactionHeader(RMWbits, 1, 0));
        request[2] = GBT.controlAddress;
		request[3] = 0xFFFFFFFF; //mask for writing 0s
		request[4] = 1 << GBT.RS_GBTerrors; //mask for writing 1s: 0x00000800;
        request[5] = quint32(TransactionHeader(RMWbits, 1, 1));
		request[6] = GBT.controlAddress;
		request[7] = ~request[4]; //mask for writing 0s: 0xFFFFF7FF;
		request[8] = 0x00000000; //mask for writing 1s
		responseSize = 5 * sizeof(quint32);
		qsocket->write(pRequest, requestSize);
		if (qsocket->waitForReadyRead(500)) {
			qsocket->read(pResponse, responseSize);
			TransactionHeader th(response[1]);
			if (th.InfoCode != 0) {emit networkError(th.infoCodeString()); return;}
			th = TransactionHeader(response[3]);
			if (th.InfoCode != 0) {emit networkError(th.infoCodeString()); return;}
			emit successfulWrite(2);
		} else emit networkError("no response");
    }

    void syncGBTreadout() {
        const quint8 nRegisters = 12, nPairs = nRegisters - 3;
        requestSize = 3 * sizeof(quint32);
        request[0] = quint32(PacketHeader(control, 0));
        request[1] = quint32(TransactionHeader(nonIncrementingRead, nRegisters, 0));
        request[2] = GBT.FIFOaddress;
        if (writingOK()) {
            responseSize = (nRegisters + 2) * sizeof(quint32);
			connect(qsocket, &QUdpSocket::readyRead, this, [=]() {
                if (readingOK()) {
                    TransactionHeader th(response[1]);
                    if (th.InfoCode != 0) {emit networkError(th.infoCodeString()); return;}
                    quint16 wID[nRegisters];
                    for (quint8 i=0; i<nRegisters; ++i) wID[i] = response[i + 2] >> 16;
                    quint8 L[3] = {0, 0, 0}; //length of consistent wID sequence
                    quint8 wordBeginning;
                    for (quint8 i=0; i<nPairs; ++i) if (wID[i + 3] == wID[i] + 1) ++L[i % 3];
                    if ((L[0] >= L[1]) && (L[0] > L[2])) wordBeginning = 0;
                    else if ((L[1] >= L[2]) && (L[1] > L[0])) wordBeginning = 1;
                    else if ((L[2] >= L[0]) && (L[2] > L[1])) wordBeginning = 2;
                    else {emit networkError("sync failed, fill FIFO first"); return;}
                    if (wID[wordBeginning] % 2) wordBeginning += 3;
                    if (wordBeginning == 0) emit readoutSynced();
                    else {
                        requestSize = 3 * sizeof(quint32);
                        request[0] = quint32(PacketHeader(control, 0));
                        request[1] = quint32(TransactionHeader(nonIncrementingRead, wordBeginning, 0));
                        request[2] = GBT.FIFOaddress;
                        if (writingOK()) {
                            responseSize = (wordBeginning + 2) * sizeof(quint32);
							connect(qsocket, &QUdpSocket::readyRead, this, [=]() {
                                if (readingOK()) {
                                    TransactionHeader th(response[1]);
                                    if (th.InfoCode != 0) emit networkError(th.infoCodeString());
                                    else if (th.Words != wordBeginning) emit networkError("sync failed");
                                    else emit readoutSynced();
                                }
                            });
                        }
                    }
                }
            });
        }
    }

	void initGBT() {
		//for (int i=0; i<15; ++i) printf("%X: %08X\n", i, GBT.initRequest[i]);
		qsocket->write(GBT.pInitRequest, 15 * sizeof(quint32));
		if (qsocket->waitForReadyRead(500)) {
			qsocket->read(pResponse, 2 * sizeof(quint32));
			printf("response:\n%08X\n%08X\n", response[0], response[1]);
			TransactionHeader th(response[1]);
			if (th.InfoCode != 0) {emit networkError(th.infoCodeString()); return;}
			emit successfulWrite(0);
		} else emit networkError("no response");
	}

	void initSPI(double freq_kHz = 250) {
		quint32 div = quint32(16000/freq_kHz) - 1;
		requestSize = 10 * sizeof(quint32);
		request[0] = quint32(PacketHeader(control, 0));
		request[1] = quint32(TransactionHeader(write, 7, 0));
		request[2] = SPIaddress;
		request[3] = 0x00000000; //d0
		request[4] = 0x00000000; //d1
		request[5] = 0x00000000; //d2
		request[6] = 0x00000000; //d3
		request[7] = 0x00002220; //ctrl: CHAR_LEN=32bit, Rx_NEG=1, ASS=1
		request[8] = div;		 //div: freq=32MHz/(div+1)/2
		request[9] = 0x000000ff; //ASS
		responseSize = 2 * sizeof(quint32);
		if (writingOK()) {
			if (qsocket->waitForReadyRead(500)) {
				if (readingOK()) {
					TransactionHeader th(response[1]);
					if (th.InfoCode != 0) {emit networkError(th.infoCodeString()); return;}
					emit successfulWrite(0);
				}
			} else emit networkError("no response");
		}
	}

	void setPhase() {
		requestSize = 8 * sizeof(quint32);
		request[0] = quint32(PacketHeader(control, 0));
		request[1] = quint32(TransactionHeader(write, 5, 0));
		request[2] = SPIaddress;
		request[3] = 0x03000000 + quint16(phaseShift); //d0
		request[4] = 0x00000000; //d1
		request[5] = 0x00000000; //d2
		request[6] = 0x00000000; //d3
		request[7] = 0x00002320; //ctrl: CHAR_LEN=32bit, Rx_NEG=1, ASS=1
		responseSize = 2 * sizeof(quint32);
		if (writingOK()) {
			if (qsocket->waitForReadyRead(500)) {
				if (readingOK()) {
					TransactionHeader th(response[1]);
					if (th.InfoCode != 0) {emit networkError(th.infoCodeString()); return;}
				} emit phaseSet();
			} else emit networkError("no response");
		}
	}

	void setFrequency(bool on) {
		requestSize = 5 * sizeof(quint32);
		request[0] = quint32(PacketHeader(control, 0));
		request[1] = quint32(TransactionHeader(RMWbits, 1, 0));
		request[2] = GBT.TriggerBunchFrequencyAddress;
		request[3] = 0x0000FFFF; //mask for writing 0s
		request[4] = on ? quint32(genFrequency << 16) : 0x00000000; //mask for writing 1s
		responseSize = 3 * sizeof(quint32);
		if (writingOK()) {
			if (qsocket->waitForReadyRead(500)) {
				if (readingOK()) {
					TransactionHeader th(response[1]);
					if (th.InfoCode != 0) {emit networkError(th.infoCodeString()); return;}
					emit frequencySet();
				}
			} else emit networkError("no response");
		}
	}

	void setTRGpattern1() {
		requestSize = 4 * sizeof(quint32);
		request[0] = quint32(PacketHeader(control, 0));
		request[1] = quint32(TransactionHeader(write, 1, 0));
		request[2] = 0x00001004;
		request[3] = GBT.Control.TriggerContinuousPattern1;
		responseSize = 2 * sizeof(quint32);
		if (writingOK()) {
			if (qsocket->waitForReadyRead(500)) {
				if (readingOK()) {
					TransactionHeader th(response[1]);
					if (th.InfoCode != 0) {emit networkError(th.infoCodeString()); return;}
					emit successfulWrite(0);
				}
			} else emit networkError("no response");
		}
	}

	void setTRGpattern0() {
		requestSize = 4 * sizeof(quint32);
		request[0] = quint32(PacketHeader(control, 0));
		request[1] = quint32(TransactionHeader(write, 1, 0));
		request[2] = 0x00001005;
		request[3] = GBT.Control.TriggerContinuousPattern0;
		responseSize = 2 * sizeof(quint32);
		if (writingOK()) {
			if (qsocket->waitForReadyRead(500)) {
				if (readingOK()) {
					TransactionHeader th(response[1]);
					if (th.InfoCode != 0) {emit networkError(th.infoCodeString()); return;}
					emit successfulWrite(0);
				}
			} else emit networkError("no response");
		}
	}
};

#endif // IPBUSINTERFACE_H
