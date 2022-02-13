/* SPAUN                  */
/* serial.h               */
/* (c) 2022 Martin Mather */

extern int s_bytes_in, s_bytes_out;

int serial_put_empty(void);
int serial_put_free(void);
int serial_put(uint8_t c);
int serial_put2(uint8_t *c, int len);

int serial_get_count(void);
int serial_get(uint8_t *c);
int serial_get2(uint8_t *c, int len);

void serial_flush(void);

int serial_open(char *device, int b_rate);
int serial_close(void);
int serial_poll(void);
int serial_rate(int b_rate);
void serial_set_nrts(int nrts);

int serial_connected(void);


void sgg(void);