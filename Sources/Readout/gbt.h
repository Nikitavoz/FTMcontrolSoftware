#ifndef GBT_H
#define GBT_H

#include "ipbusheaders.h"

union HDMIlinkStatus {
	char pointer[4];
	quint32 reg = 0;
	struct {
		quint32
			syncComplete	: 1,
			reserved0		: 4,
			line0linkLost	: 1,
			line0linkStable : 1,
			reserved1		: 1,
			line1delay		: 5,
			line1linkLost	: 1,
			line1linkStable	: 1,
			reserved2		: 1,
			line2delay		: 5,
			line2linkLost	: 1,
			line2linkStable	: 1,
			reserved3		: 1,
			line3delay		: 5,
			line3linkLost	: 1,
			line3linkStable	: 1,
			errorDetected	: 1;
	};
};
const quint32 HDMIaddress = 0x00000400;

union GBTword {
	quint8 bytes[16] = {0};
	quint32 registers[4];
	struct Header {
		quint64
			BCid		: 12,
			orbit		: 32,
			reserved0	: 20,
			reserved1	:  8,
			words		:  4,
			token		:  4,
			counter		: 16,
			reserved2	: 32;
	} header;
	struct Data {
		quint32
			time				: 12,
			charge				: 13,
			ADCnumber			: 1,
			doubleEvent			: 1,
			event1timeLost		: 1,
			event2timeLost		: 1,
			ADCinGate			: 1,
			timeInfoLate		: 1,
			amplitudeHigh		: 1,
			eventInTVDCtrigger	: 1,
			timeInfoLost		: 1,
			reserved			: 2,
			channelID			: 4;
	} data0;
};

struct GBTmodule {
	const quint8 //data generator states
		DG_noData	= 0,
		DG_main		= 1,
		DG_tx		= 2;
	const quint8 //trigger generator states
		TG_noTrigger  = 0,
		TG_continuous = 1;
	const quint8 //readout commands
		RO_OFF = 0,
		RO_SOC = 1,
		RO_SOT = 2,
		RO_EOC = 3,
		RO_EOT = 4;
	const quint8 //reset bits positions
		RS_orbitSync			=  8,
		RS_droppingHitCounters	=  9,
		RS_generatorsBunchOffset= 10,
		RS_GBTerrors			= 11,
		RS_GBT					= 12,
		RS_RXphaseError			= 13;
	quint32 controlAddress   =  0x00001000,
			statusAddress    =  0x0000100C,
			FIFOaddress      =  0x00001014,
			TriggerBunchFrequencyAddress = 0x00001007,
			dataRequest[7]   = {
				quint32(PacketHeader(control, 0)),                      //0x200000F0
				quint32(TransactionHeader(nonIncrementingRead, 240, 0)),//0x2000F02F
				FIFOaddress,                                            //0x00001014
				quint32(TransactionHeader(nonIncrementingRead, 120, 1)),//0x2001782F
				FIFOaddress,                                            //0x00001014
				quint32(TransactionHeader(read, 4, 2)),                 //0x2002040F
				statusAddress                                           //0x0000100C
			},
			statusRequest[3] = {
				quint32(PacketHeader(control, 0)),                      //0x200000F0
				quint32(TransactionHeader(read, 4, 0)),                 //0x2000040F
				statusAddress                                           //0x0000100C
			},
			initRequest[3]  = { //total size of request: 15 registers = 60 bytes
				quint32(PacketHeader(control, 0)),                      //0x200000F0
				quint32(TransactionHeader(write, 12, 0)),               //0x20000C1F
				controlAddress,                                         //0x00001000
			};
	union ControlData {
        char pointer[48]; // 12 registers * 4 bytes
		quint32 registers[12] { //initial values for laser_gen from D.Finogeev
			0x00000010, //0
			0x00000daf, //1
			0x00000000, //2
			0x00000000,	//3
			0x00000001,	//4
			0x00000000,	//5
			0x00000daf,	//6
			0x00000000,	//7
			0x000a0000,	//8
			0x00000000,	//9
			0x00000000,	//A
			0x00000000	//B
		};
        struct {
			quint32
				DataGenerator               :  4,
				TriggerGenerator            :  4,
				Reset						:  8,
				ReadoutCommand              :  4,
				reserved0                   : 12,   //[0]
				TriggerRespondMask,                 //[1]
				DataBunchPattern,                   //[2]
				TriggerSingleValue,                 //[3]
				TriggerContinuousPattern1,          //[4]
				TriggerContinuousPattern0,          //[5]
				TriggerContinuousValue,             //[6]
				DataBunchFrequency          : 16,
				TriggerBunchFrequency       : 16,   //[7]
				DataFrequencyOffset         : 12,
				reserved1                   :  4,
				TriggerFrequencyOffset      : 12,
				reserved2                   :  4,   //[8]
				RDH_PAR                     : 16,
				RDH_FEEID                   : 16,   //[9]
				RDH_DETfield                : 16,
				RDH_MaxPayload              : 16,
				BCIDdelay                   : 12,
				reserved3                   :  4,
				CRUtriggerCompareDelay      : 12,
				reserved4                   :  4;   //[11]
        };
/*		void printControl() { //debug function
			printf("%08x\t%x %d%d%d%d%d%d %x %x\n", registers[0], ReadoutCommand, (Reset >> 5) % 2, (Reset >> 4) % 2, (Reset >> 3) % 2, (Reset >> 2) % 2, (Reset >> 1) % 2, Reset % 2, TriggerGenerator, DataGenerator);
			printf("%08x\t%08x\n", registers[1], TriggerRespondMask);
			printf("%08x\t%08x\n", registers[2], DataBunchPattern);
			printf("%08x\t%08x\n", registers[3], TriggerSingleValue);
			printf("%08x\t%08x\n", registers[4], TriggerContinuousPattern1);
			printf("%08x\t%08x\n", registers[5], TriggerContinuousPattern0);
			printf("%08x\t%08x\n", registers[6], TriggerContinuousValue);
			printf("%08x\t%04hx %04hx\n", registers[7], TriggerBunchFrequency, DataBunchFrequency);
			printf("%08x\t %03hx  %03hx\n", registers[8], TriggerFrequencyOffset, DataFrequencyOffset);
			printf("%08x\t%04hx %04hx\n", registers[9], RDH_FEEID, RDH_PAR);
			printf("%08x\t%04hx %04hx\n", registers[10], RDH_MaxPayload, RDH_DETfield);
			printf("%08x\t %03hx  %03hx\n\n", registers[11], CRUtriggerCompareDelay, BCIDdelay);
		}
*/
    } Control;
	struct StatusData {
		quint32 GBTstatus           : 16,
				ReadoutMode         :  4,
				BCIDsyncMode        :  4,
				RXphase             :  4,
				reserved0           :  4, //[0]
				CRUorbit            : 32, //[1]
				CRUBC               : 12,
				reserved1           : 20, //[2]
				RawFIFOcount        : 16,
				SelectorFIFOcount   : 16; //[3]
	} Status;
    char *pDataRequest = reinterpret_cast<char *>(dataRequest),
         *pStatusRequest = reinterpret_cast<char *>(statusRequest),
         *pInitRequest = reinterpret_cast<char *>(initRequest);
};

#endif // GBT_H
