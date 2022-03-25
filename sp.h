/* SPAUN                  */
/* sp.h                   */
/* (c) 2022 Martin Mather */

#ifndef SP_H
#define SP_H

#include <memory>

#include "constants.h"
#include "queue.h"
#include "serial.h"
#include "aun.h"

class SPClass {
	
	private:
		// SP_IDLE		- Not doing anything
		// SP_DATAWAIT	- Rxed scout frame, ACK frame sent ... waiting for data frame
		// SP_SCOUTWAIT	- Txed scout frame, waiting for ACK/NAK frame
		// SP_ACKWAIT	- Txed data frame, waiting for ACK frame
		// SP_RESET		- On trigger send reset
		enum sp_state_t {
			SP_IDLE, SP_DATAWAIT, SP_SCOUTWAIT, SP_ACKWAIT, SP_RESET, SP_RESETWAIT, SP_REMOTEACKWAIT
		} sp_state = SP_IDLE;

		//RX
		enum {
			RX_VOID, RX_FLAG, RX_DATA
		} rx_state = RX_VOID;
		int EscFlag = 0, rx_counter, rx_flag, rx_fifo_notfull;
		uint8_t rx_fifo0, rx_fifo1, rx_buf2[SP_RXBUFLEN], *rx_buf = rx_buf2 + AUN_HDR_SIZE;
		
		//TX
		enum tx_state_t {
			TX_IDLE, TX_OPEN, TX_DATA, TX_CLOSE, TX_CLOSEWAIT, TX_CLOSEDELAY
		} tx_state = TX_IDLE;
		QueueClass tx_q;
		queue_t *tx_u = 0;
		int tx_ctrllen, tx_counter, tx_tail, tx_data_flag;
		uint8_t *tx_buf, tx_flag, tx_ackbuf[4], tx_aunack[AUN_HDR_SIZE];
		clock_t trigger;//used for CLOSEWAIT & CLOSEDELAY

		//Functions
		void put_raw(uint8_t c);
		void put(uint8_t c, int docrc);
		void check_queue(void);
		void send_ack(int stn);
		void aun_ack(queue_t *qptr);
		void set_state(sp_state_t new_state);
		void set_tx_state(tx_state_t new_state);//to be private
		int tx_open(int flag, int stn, uint8_t *buf, int len);
		void send_scout(void);
		void send_data(void);
		void send_reply(void);
		void send_reset(void);
		void end_of_frame(void);
		
		class CRCClass {
			public:
				uint32_t crc;
				void calc(uint8_t c);
		} rx_crc, tx_crc;
		
		SerialClass serial;
		AUNClass aun;
	
	public:
		int frame_count = 0, bad_count = 0;
		int	optimistic = 1, monitor_flag = 0, mystn;

		int open(int stn, string device, int serial_ip_port);
		void close(void);
		void reset(void);
		void poll(void);
		int send(queue_t *qptr);
		int connected(void);
		void monitor(int flag);
		
		void test(void);

};

class SPCollectionClass {
	
	private:
		int count = 0;
		unique_ptr<SPClass> stations[SP_MAX_STATIONS] = { 0 };
		
	public:
		int open(int stn, string device, int serial_ip_port);
		void close(void);
		void reset(void);
		void poll(void);
		int connected(void);
		void monitor(int flag);
		
		void test(void);
};

extern SPCollectionClass mysp;

#endif
