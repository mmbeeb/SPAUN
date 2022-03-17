/* SPAUN                  */
/* constants.h            */
/* (c) 2022 Martin Mather */


#ifndef CONSTANTS_H
#define CONSTANTS_H

/*
 * AUN
 */
#define AUN_MAX_STATIONS	50
#define AUN_PORT_BASE		10000

#define AUN_RXBUFLEN		2048	// max length of receive buffer
#define AUN_TXHANDLE		0x1000
#define AUN_RXTIMEOUT		5		// seconds

#define AUN_HDR_SIZE		8

#define AUN_TYPE_BROADCAST	1
#define AUN_TYPE_UNICAST	2
#define AUN_TYPE_ACK		3
#define AUN_TYPE_NACK		4
#define AUN_TYPE_IMMEDIATE	5
#define AUN_TYPE_IMM_REPLY	6

#define ECONET_MACHINEPEEK	8

/*
 * SP
 */
#define SP_MAX_STATIONS		5
#define SP_TIMEOUT			5		//seconds
#define SP_TXDELAY			5		//1/SP_TXDELAY seconds
#define SP_RXBUFLEN			2048

// Frame flags
#define SP_FLAG_VOID		0x7e
#define SP_FLAG_DATA		0xbe
#define SP_FLAG_SCOUT		0xce
#define SP_FLAG_BROADCAST	0xde
#define SP_FLAG_RESET		0xd6
#define SP_FLAG_ACK			0xee
#define SP_FLAG_NAK			0xfe

/*
 * Queue
 */
#define Q_SIZE				20

/*
 * Serial
 */
#define SERIAL_BAUD			8		// Default baud rate
#define SERIAL_IPPORT		25232	// BeebEm IP232 port
#define SBUF_SIZE			2048
#define STXBUF_SIZE			200
#define SRXBUF_SIZE			200
#define SBYTES_PER_SEC		1900	// Max Beeb can handle (it's much slower if rom in debug mode)

#endif
