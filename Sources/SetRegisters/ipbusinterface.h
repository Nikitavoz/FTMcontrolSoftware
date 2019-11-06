#ifndef IPBUSINTERFACE_H
#define IPBUSINTERFACE_H

#include "ipbusheaders.h"

const quint16 maxWordsPerPacket = 368; //limit from ethernet MTU of 1500 bytes

class IPbusTarget: public QObject {
    Q_OBJECT
public:
    QUdpSocket *qsocket = new QUdpSocket(this);
    QMetaObject::Connection OKconnection;
    QString targetIPaddress; 
    quint16 nextPacketID = 0;
    StatusPacket statusPacket;
    quint16 requestSize = 0, responseSize = 0;
    quint32 request[maxWordsPerPacket], response[maxWordsPerPacket];
    char *pRequest = reinterpret_cast<char *>(request);
    char *pResponse = reinterpret_cast<char *>(response);

    IPbusTarget() {
        connect(qsocket, QOverload<QAbstractSocket::SocketError>::of(&QAbstractSocket::error), this, [=](QAbstractSocket::SocketError socketError) {emit networkError("Network Error " + QString::number(socketError) + ": " + qsocket->errorString());});
        connect(qsocket, SIGNAL(connected()), this, SLOT(requestStatus()));
        connect(qsocket, SIGNAL(disconnected()), this, SLOT(reinit()));
        qsocket->bind(QHostAddress::AnyIPv4, 50000);
    }

    ~IPbusTarget() {
        qsocket->disconnect(SIGNAL(disconnected()));
        qsocket->disconnectFromHost();
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

public slots:
    void reinit() {
        if (qsocket->state() == QAbstractSocket::ConnectedState)
            qsocket->disconnectFromHost();
        else
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
            emit statusOK();
        }
    }

    void readRegisters(quint32 *data, quint16 nWords, quint32 baseAddress, bool FIFO = false) {
        if (nWords == 0) return;
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
    }

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

};

#endif // IPBUSINTERFACE_H
