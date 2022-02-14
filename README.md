# SPAUN

Serial Protocol / AUN

For use with NFS_SERIAL ROM.

*****

An optional argument can be given with the device name of the serial port.

e.g. /.spaun "/dev/ttyS1"

If no argument give, it defaults to "/dev/ttyS0" (COM1).

[If the device argument is "\*", spaun listens for a connection from BeebEm using the IP232 option.]

Serial baud rate is 19200, but spaun adds a delay between bytes, to give an approximate rate of 15000 baud (1500 bytes/second).
