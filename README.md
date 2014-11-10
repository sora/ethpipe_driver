ethpipe_driver
--------------

```bash
# compile device driver
$ make
$ sudo insmod ./ethpipe.ko
$ sudo chmod 777 /dev/ethpipe/0

# packet sending (packet size=60B, 14.88 Mpps)
$ gcc -Wall -O -o pktgen ./pktgen_stdout.c
$ time ./pktgen -s 60 -n 41 -m 362950 > /dev/ethpipe/0
```
