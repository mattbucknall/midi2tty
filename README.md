# MIDI-TO-TTY

## Overview

A basic user-space ALSA MIDI device input to TTY device output bridge for Linux.
The TTY device is configured to run at 115200 baud, 8-bit data, no parity, 1 stop bit.

Usage: `midi2tty <midi-device> <tty-device> [log]`

If the optional argument 'log' is specified, MIDI bytes will be written to stdout in hexadecimal notation.


A list of available MIDI devices can be obtained with `amidi -l`.

## Dependencies

libasound2
