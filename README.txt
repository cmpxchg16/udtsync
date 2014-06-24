udtsync
-------

Send/Receive files beetween 2 peers on top of UDT protocol.
8x times faster compared to an optimized TCP.
tested on WAN with network toplogy:

DC1::1Gb----30ms---->10Gb----30ms---->DC2::1Gb

This tool was written as part of an hackathon event.

Build:
------
$>make

Run:
-----
./udtsync

Test in your environment:
-------------------------

1. create a file

dd if=/dev/urandom of=/tmp/file.dat bs=1M count=100

2. run server:

$>./udtsync server 9000

3. run client:

3.1 GET (get remote file (/tmp/file.dat) into local file (/tmp/get.dat):

$>./udtsync client 127.0.0.1 9000 /tmp/file.dat /tmp/get.dat GET

3.2 PUT (put local file (/tmp/file.dat) into remote file (/tmp/put.dat):

$>./udtsync client 127.0.0.1 9000 /tmp/put.dat /tmp/file.dat PUT


Author: 
-------
Uri Shamay (shamayuri@gmail.com)
