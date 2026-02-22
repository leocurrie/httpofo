# httpofo

## What is this?

This is a primitive webserver for the Atari Portfolio.
It includes a very rudimentary SLIP-based TCP/IP stack, including ICMP echo support.

## How do I use it?

The webserver runs on a stock Atari Portfolio with the Serial Interface attached.
You will need to attach the serial port to a Linux PC or some other host to act as the SLIP gateway.

* Download the httpofo.exe from the releases page and copy onto your Portfolio. It will fit onto the C: drive, but you probably want to store it on a 64K memory card so that it can serve some HTML files too!
* There are some example .htm files in the www folder which you can also copy
* On your Linux pc, install `slattach` - on Ubuntu you might need to install via `apt install net-tools`
* Attach your portfolio serial cable to your Linux host. I use a USB->RS232 dongle.
* Start `slattach` as follows:
```
$ slattach -s 9600 -p slip /dev/ttyUSB0 &
```
* Configure the IP address using `ifconfig`
```
ifconfig sl0 192.168.1.1 pointopoint 192.168.1.100 up
```
* On the Portfolio, launch the webserver by running `httpofo`
* Point the browser on your Linux host to http://192.168.1.100

## How do I build it

There is a build.sh script that will compile in a docker container.