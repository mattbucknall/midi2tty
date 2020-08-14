/*
 * MIDI-2-TTY
 *
 * A basic user-space ALSA MIDI device input to TTY device output bridge
 * for Linux.
 *
 * This was written in a hurry without any real thought or care - Do not
 * write proper code like this!
 * 
 * Copyright (C) 2020 - Matthew T. Bucknall
 * 
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 * 
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * 
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <poll.h>
#include <signal.h>
#include <sys/signalfd.h>
#include <sys/resource.h>
#include <termio.h>

#include <alsa/asoundlib.h>

#define APP_TTY_BAUD_RATE		B115200
#define APP_MIDI_BUFFER_SIZE	4096


static int m_signal_fd;
static int m_tty_fd;
static snd_rawmidi_t* m_midi_handle;


static int write_all(int fd, const uint8_t* buffer, int length) {
	int result;
	const uint8_t* buffer_e = buffer + length;

	while (buffer < buffer_e) {
		result = write(fd, buffer, buffer_e - buffer);

		if ( result < 0 ) {
			return result;
		}

		buffer += result;
	}

	return 0;
}


int main(int argc, char* argv[]) {
	static int exit_code = 1;
	static int result;
	static sigset_t mask;
	static struct signalfd_siginfo signal_info;
	static struct termios tty_config;
	static int n_pfds;
	static struct pollfd* pfds;
	static unsigned short midi_revents;
	static uint8_t midi_buffer[APP_MIDI_BUFFER_SIZE];
	static bool logging_enabled = false;

	if ( argc < 3 ) {
		printf("Usage: <midi-device> <tty-device> [log]\n");
		exit_code = 0;
		goto cleanup1;
	}

	for (int i = 3; i < argc; i++) {
		if ( strcmp(argv[i], "log") == 0 ) {
			logging_enabled = true;
		}
	};

	result = setpriority(PRIO_PROCESS, 0, 100);

	if ( result < 0 ) {
		printf("WARNING: Unable to increase process priority: %s\n", strerror(errno));
	}

	sigfillset(&mask);
	result = sigprocmask(SIG_SETMASK, &mask, NULL);

	if ( result != 0 ) {
		fprintf(stderr, "Unable to block signals\n");
		goto cleanup1;
	}

	sigemptyset(&mask);
	sigaddset(&mask, SIGINT);
	m_signal_fd = signalfd(-1, &mask, SFD_NONBLOCK);

	if ( m_signal_fd < 0 ) {
		fprintf(stderr, "Unable to create signal file descriptor: %s\n", strerror(errno));
		goto cleanup1;
	}

	m_tty_fd = open(argv[2], O_WRONLY | O_NOCTTY | O_NONBLOCK);

	if ( m_tty_fd < 0 ) {
		fprintf(stderr, "Unable to open TTY device: %s\n", strerror(errno));
		goto cleanup2;
	}

	result = tcgetattr(m_tty_fd, &tty_config);

	if ( result != 0 ) {
		fprintf(stderr, "Unable to get TTY device configuration: %s\n", strerror(errno));
		goto cleanup3;
	}

	tty_config.c_cflag = CLOCAL | CREAD | CS8;
	tty_config.c_lflag = 0;
	tty_config.c_iflag = INPCK | IGNPAR |IGNBRK;
	tty_config.c_oflag = 0;
	tty_config.c_cc[VMIN] = 1;
	tty_config.c_cc[VTIME] = 0;

	cfsetispeed(&tty_config, APP_TTY_BAUD_RATE);
	cfsetospeed(&tty_config, APP_TTY_BAUD_RATE);

	result = tcsetattr(m_tty_fd, TCSAFLUSH, &tty_config);

	if ( result != 0 ) {
		fprintf(stderr, "Unable to set TTY device configuration: %s\n", strerror(errno));
		goto cleanup3;
	}

	result = snd_rawmidi_open(&m_midi_handle, NULL, argv[1], 0);

	if ( result < 0 ) {
		fprintf(stderr, "Unable to open MIDI device: %s\n", snd_strerror(result));
		goto cleanup3;
	}

	n_pfds = snd_rawmidi_poll_descriptors_count(m_midi_handle);

	if ( n_pfds < 0 ) {
		fprintf(stderr, "Unable to get poll descriptor count for MIDI device: %s\n", snd_strerror(n_pfds));
		goto cleanup4;
	}

	n_pfds += 2;
	pfds = alloca((n_pfds) * sizeof(struct pollfd));
	snd_rawmidi_poll_descriptors(m_midi_handle, pfds + 2, n_pfds - 2);

	pfds[0].fd = m_signal_fd;
	pfds[0].events = POLL_IN;
	pfds[0].revents = 0;

	pfds[1].fd = m_tty_fd;
	pfds[1].events = 0;
	pfds[1].revents = 0;

	printf("Bridging MIDI input on '%s' to '%s'\n", argv[1], argv[2]);

	snd_rawmidi_read(m_midi_handle, NULL, 0);

	for (;;) {
		result = poll(pfds, n_pfds, -1);

		if ( result == 0 ) {
			continue;
		} else if ( result < 0 ) {
			fprintf(stderr, "Unable to poll devices: %s\n", strerror(errno));
			goto cleanup4;
		}

		if ( pfds[1].revents & (POLL_ERR | POLL_HUP) ) {
			fprintf(stderr, "Unable to poll TTY device\n");
			goto cleanup4;
		}

		result = snd_rawmidi_poll_descriptors_revents(m_midi_handle, pfds + 2, n_pfds - 2, &midi_revents);

		if ( result < 0 ) {
			fprintf(stderr, "Unable to query MIDI device events: %s\n", snd_strerror(result));
			goto cleanup4;
		}

		if ( midi_revents & POLL_IN ) {
			int n_read = snd_rawmidi_read(m_midi_handle, midi_buffer, sizeof(midi_buffer));

			if ( n_read < 0 ) {
				fprintf(stderr, "Unable to read MIDI event data: %s\n", snd_strerror(n_read));
				goto cleanup4;
			}

			result = write_all(m_tty_fd, midi_buffer, n_read);
			tcflush(m_tty_fd, TCIOFLUSH);

			if ( result < 0 ) {
				fprintf(stderr, "Unable to write MIDI event data to TTY device: %s\n", strerror(errno));
				goto cleanup4;
			}

			if ( logging_enabled ) {
				for (int i = 0; i < n_read; i++) {
					printf("%02x ", midi_buffer[i]);
				}

				putc('\n', stdout);

				if ( isatty(STDOUT_FILENO) ) {
					tcflush(STDOUT_FILENO, TCIOFLUSH);
				}
			}
		}

		if ( pfds[0].revents & POLLIN ) {
			result = read(m_signal_fd, &signal_info, sizeof(signal_info));

			if ( result != sizeof(signal_info) ) {
				fprintf(stderr, "Unable to get signal information: %s\n", strerror(errno));
				goto cleanup4;
			}

			if ( signal_info.ssi_signo == SIGINT ) {
				break;
			}
		}
	}

	puts("\rDone\n");

	exit_code = 0;

cleanup4:

	snd_rawmidi_close(m_midi_handle);

cleanup3:

	close(m_tty_fd);

cleanup2:

	close(m_signal_fd);

cleanup1:

    return exit_code;
}
