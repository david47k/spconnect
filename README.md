# spconnect

Connects to a serial port from a Windows Terminal/Console.
Copyright 2024 David Atkinson. MIT License. 
Available from https://github.com/david47k/spconnect/

## About

This program connects to a serial port from a Windows Console/Terminal.

It's a basic program, designed to be used directly from Windows Terminal, as an
alternative to e.g. PuTTY. It is tested on (and designed to work on) Windows 10.

## Using the program

### Configuring the serial port

You can specify baud rate etc. by first using the windows built-in `mode`
command. Running 'mode' by itself will give you a list of serial ports.
Common baud rates: 300, 1200, 2400, 4800, 9600, 19200, 38400, 57600,
115200, 230400, 460800, 921600. e.g.:

`mode com1 baud=115200 parity=n data=8 stop=1 to=off xon=off odsr=off octs=off dtr=on rts=on`

or

`mode com1 115200,n,8,1`

### Starting the program

Start this program with the serial port as an argument, along with any options. e.g.:

`spconnect com1 -W 10000`

### Options

```
-L       --local-echo          Enable local echo of characters typed.
-S       --system-codepage     Use system codepage instead of UTF-8.
-R       --replace-cr          Replace input CR (\r) with newline (\n).
-D       --disable-vt          Disable sending and receiving of virtual terminal (VT) codes.
-W 100   --write-timeout 100   Serial port write timeout, in milliseconds. Default 1000.
```

### Quitting

Use `Ctrl-F10` to quit.

### Using a different codepage

The default is to use UTF-8 for console input and output. You can use the system
codepage instead by using the `-S` option. You can check the system codepage 
and change it using the the windows built-in `mode con cp` command. e.g.:

`mode con cp select=1251`

### Disable VT processing (raw mode)

The default is to process VT commands from both the keyboard and the serial 
port. You can disable VT processing (essentially a raw mode) using `-D`.

## Similar programs

- SimpleSerial
- ???