# httpofo

## What is this?

httpofo is a small HTTP/1.0 file server for the [Atari Portfolio](https://en.wikipedia.org/wiki/Atari_Portfolio), the world's first palmtop PC (1989). It includes a custom SLIP-based TCP/IP stack with ICMP echo (ping) support.

The Portfolio runs DOS on an 80C88 CPU with 128KB of RAM. The compiled executable is around 12KB.

## Requirements

- Atari Portfolio with the Serial Interface card attached
- A Linux host to act as the SLIP gateway (connected via serial cable)
- `slattach` and `ifconfig` on the Linux host (`net-tools` package)

## Quick Start

### 1. Set up the Linux host

Attach the Portfolio's serial cable to your Linux machine (a USB-to-RS232 adapter works well), then bring up the SLIP interface:

```sh
sudo slattach -s 9600 -p slip /dev/ttyUSB0 &
sudo ifconfig sl0 192.168.1.1 pointopoint 192.168.1.100 up
```

Adjust `/dev/ttyUSB0` to match your serial device.

### 2. Set up the Portfolio

Copy `httpofo.exe` to the Portfolio. It fits on the internal C: drive, but a memory card gives you more room to store files to serve.

The `www/` folder in this repository contains some example HTML files you can copy across too.

### 3. Run the server

On the Portfolio, run:

```
httpofo
```

The server will start and listen on `192.168.1.100:80`. Press `Ctrl+Q` to quit.

### 4. Browse to it

On the Linux host, open a browser and navigate to:

```
http://192.168.1.100
```

## Configuration

```
httpofo [ip] [path] [-w]
```

| Argument | Description |
|----------|-------------|
| `ip`     | IP address for the server to listen on. Default: `192.168.1.100` |
| `path`   | Path to the document root directory. Default: current directory |
| `-w`     | Enable file uploads via HTTP PUT. Disabled by default |

Arguments can be given in any order. Examples:

```
httpofo 192.168.7.2
httpofo 192.168.7.2 A:\WWW
httpofo 192.168.7.2 A:\WWW -w
httpofo -w
```

## Features

### File serving

GET requests serve files from the document root. The following MIME types are recognised:

| Extension | Type |
|-----------|------|
| `.htm`, `.html` | `text/html` |
| `.txt` | `text/plain` |
| `.jpg`, `.jpeg` | `image/jpeg` |
| `.gif` | `image/gif` |
| Everything else | `application/octet-stream` |

### Directory listing

If a directory is requested and it contains an `index.htm` file, that file is served. Otherwise an HTML directory listing is generated.

### File uploads

When started with the `-w` flag, the server accepts HTTP PUT requests. This lets you upload files from the host using `curl`:

```sh
curl -T myfile.txt http://192.168.1.100/myfile.txt
```

Files are streamed directly to disk as they arrive, so upload size is not limited by RAM. However, uploads are very slow (much slower than e.g. XMODEM) due to the TCP overheads.

## Network notes

- The server handles one connection at a time. Incoming connections while busy are queued and served in order.
- Browsers typically open several simultaneous connections (for images, favicon, etc). These will be queued and served sequentially — the page will load fully, just not all at once.
- The SLIP link runs at 9600 baud, so throughput is limited. Large files will be slow.
- ICMP echo (ping) is supported — you can ping the Portfolio to check connectivity.

## Building

Requires [Docker](https://www.docker.com). The build script compiles using an OpenWatcom cross-compiler container targeting 16-bit DOS:

```sh
./build.sh
```

This produces `httpofo.exe`. To build manually inside the container, or if you have OpenWatcom installed locally:

```sh
make
```

The compiler flags (`-ms -wx -we`) target the small memory model with strict warnings-as-errors.
