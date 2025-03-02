/*
 * Copyright 2025 - Robert Amstadt
 *
 * This file is part of PiUserSpaceAudio.
 *
 * PiUserSpaceAudio is free software: you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License
 * as published by the Free Software Foundation, either version 3 of
 * the License, or (at your option) any later version.
 *
 * PiUserSpaceAudio is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with MidiUserSpaceAudio. If not, see
 * <https://www.gnu.org/licenses/>.
 */

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <sys/syscall.h>
#include <alsa/asoundlib.h>
#include <alsa/asoundef.h>

pid_t gettid(void);

static pthread_mutex_t pusamidi_fifo_lock = PTHREAD_MUTEX_INITIALIZER;
static unsigned char pusamidi_in_fifo[0x10000];
static int pusamidi_in_fifo_in = 0;
static int pusamidi_in_fifo_out = 0;

#define PUSAMIDI_PORT_MAX	32

struct pusamidi_port_s
{
    char *hwname;
    char *name;
    pid_t tid;
    snd_rawmidi_t *midiport;
};

static pthread_mutex_t pusamidi_db_lock = PTHREAD_MUTEX_INITIALIZER;
static struct pusamidi_port_s pusamidi_ins[PUSAMIDI_PORT_MAX];
static struct pusamidi_port_s pusamidi_outs[PUSAMIDI_PORT_MAX];

static struct pusamidi_port_s *pusamidi_find_port(char *hwname, snd_rawmidi_stream_t type)
{
    struct pusamidi_port_s *ports = NULL;

    if (type == SND_RAWMIDI_STREAM_INPUT)
	ports = pusamidi_ins;
    else if (type == SND_RAWMIDI_STREAM_OUTPUT)
	ports = pusamidi_outs;
    else
	return NULL;

    pthread_mutex_lock(&pusamidi_db_lock);
    for (int i = 0; i < PUSAMIDI_PORT_MAX; i++)
    {
	if (hwname == NULL)
	{
	    if (ports[i].hwname == NULL)
	    {
		pthread_mutex_unlock(&pusamidi_db_lock);
		return ports + i;
	    }
	}
	else if (ports[i].hwname != NULL && strcmp(hwname, ports[i].hwname) == 0)
	{
	    pthread_mutex_unlock(&pusamidi_db_lock);
	    return ports + i;
	}
    }

    pthread_mutex_unlock(&pusamidi_db_lock);
    return NULL;
}

static void pusamidi_close_port(char *hwname, snd_rawmidi_stream_t type)
{
    struct pusamidi_port_s *port = pusamidi_find_port(hwname, type);
    if (port)
    {
	pthread_mutex_lock(&pusamidi_db_lock);
	if (port->midiport)
	    snd_rawmidi_close(port->midiport);

	free(port->name);
	free(port->hwname);

	port->midiport = NULL;
	port->tid = 0;
	port->name = NULL;
	port->hwname = NULL;
	pthread_mutex_unlock(&pusamidi_db_lock);
    }
}

static int pusamidi_process_midi_in(unsigned char *buffer, int *len, unsigned char *last_cmd)
{
    if (*len > 1 && (buffer[*len-1] & 0x80) != 0 && !(buffer[0] == 0xf0 && buffer[*len-1] == 0xf7))
    {
	buffer[0] = buffer[*len-1];
	*len = 1;
    }

    if (buffer[0] > 0xf4 && buffer[0] < 0xff)
	return 1;

    if ((buffer[0] == 0xf1 || buffer[0] == 0xf3) && *len == 2)
	return *len;

    if (buffer[0] == 0xf2 && *len == 3)
	return *len;

    if (buffer[0] == 0xf0 && buffer[*len-1] == 0xf7)
	return *len;

    if ((buffer[0] & 0x80) == 0)
    {
	buffer[1] = buffer[0];
	buffer[0] = *last_cmd;
	*len = 2;
    }

    if ((buffer[0] == 0x80 || buffer[0] == 0x90 || buffer[0] == 0xa0 ||
	 buffer[0] == 0xb0 || buffer[0] == 0xe0) && *len == 3)
    {
	*last_cmd = buffer[0];
	return *len;
    }
    else if ((buffer[0] == 0xc0 || buffer[0] == 0xd0) && *len == 2)
    {
	*last_cmd = buffer[0];
	return *len;
    }

    return 0;
}

static void *pusamidi_in_thread(void *arg)
{
    unsigned char buffer[2048];
    unsigned char last_cmd = 0;
    int len = 0;

    struct pusamidi_port_s *port = arg;

    if (snd_rawmidi_open(&port->midiport, NULL, port->hwname, 0) < 0)
	goto done;

    printf(" In: tid: %d, name %s (%s)\n", gettid(), port->hwname, port->name);

    unsigned char c;
    while (1)
    {
	int status = snd_rawmidi_read(port->midiport, &c, sizeof(c));
	if (status < 0 && status != -EBUSY && status != -EAGAIN)
	{
	    printf("%s: %s\n", port->hwname, snd_strerror(status));
	    goto done;
	}
	else if (status > 0)
	{
	    if (len < sizeof(buffer))
		buffer[len++] = c;
	    else
	    {
		len = 1;
		buffer[0] = c;
		last_cmd = 0;
	    }

	    int n = pusamidi_process_midi_in(buffer, &len, &last_cmd);
	    if (n > 0)
	    {
		pthread_mutex_lock(&pusamidi_fifo_lock);
		int room = pusamidi_in_fifo_out - pusamidi_in_fifo_in;
		if (room <= 0)
		    room += sizeof(pusamidi_in_fifo);
		room -= 1;

		if (room > n)
		{
		    for (int i = 0; i < n; i++)
		    {
			pusamidi_in_fifo[pusamidi_in_fifo_in++] = buffer[i];
			if (pusamidi_in_fifo_in >= sizeof(pusamidi_in_fifo))
			    pusamidi_in_fifo_in = 0;
		    }
		}

		pthread_mutex_unlock(&pusamidi_fifo_lock);
		len = 0;
	    }
	}
    }

done:
    pusamidi_close_port(port->hwname, SND_RAWMIDI_STREAM_OUTPUT);
    pusamidi_close_port(port->hwname, SND_RAWMIDI_STREAM_INPUT);

    return NULL;
}

static void pusamidi_thread_create(struct pusamidi_port_s *port)
{
    pthread_t tid;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, pusamidi_in_thread, port);
    pthread_attr_destroy(&attr);
}

static void pusamidi_output_create(struct pusamidi_port_s *port)
{
    if (snd_rawmidi_open(NULL, &port->midiport, port->hwname, SND_RAWMIDI_SYNC) >= 0)
    {
	printf("Out: %s (%s)\n", port->hwname, port->name);
    }
}

static void pusamidi_enumerate_subdevices(snd_ctl_t *ctld, int cardnum, int devicenum,
				   snd_rawmidi_stream_t type,
				   void (*func)(struct pusamidi_port_s *))
{
    snd_rawmidi_info_t *info;
    snd_rawmidi_info_malloc(&info);

    snd_rawmidi_info_set_stream(info, type);
    snd_rawmidi_info_set_device(info, devicenum);
    snd_ctl_rawmidi_info(ctld, info);

    int n = snd_rawmidi_info_get_subdevices_count(info);
    for (int i = 0; i < n; i++)
    {
	snd_rawmidi_info_set_subdevice(info, i);
	snd_ctl_rawmidi_info(ctld, info);
	const char *name = snd_rawmidi_info_get_subdevice_name(info);

	char hwname[100];
	sprintf(hwname, "hw:%d,%d,%d", cardnum, devicenum, i);

	struct pusamidi_port_s *port = pusamidi_find_port(hwname, type);
	if (port == NULL)
	{
	    port = pusamidi_find_port(NULL, type);
	    if (port == NULL)
	    {
		printf("Can't register device %s (%s).\n", hwname, name);
		snd_rawmidi_info_free(info);
		return;
	    }

	    port->hwname = strdup(hwname);
	    port->name = strdup(name);
	    port->tid = 0;
	    port->midiport = NULL;

	    func(port);
	}
    }

    snd_rawmidi_info_free(info);
}

static void pusamidi_enumerate_devices(snd_rawmidi_stream_t type,
				void (*func)(struct pusamidi_port_s *s))
{
    int cardnum = -1;
    snd_ctl_t *ctld;
    char hwname[100];

    if (snd_card_next(&cardnum) < 0)
	return;

    while (cardnum >= 0)
    {
	char *name;

	if (snd_card_get_name(cardnum, &name) < 0)
	    name = "unknown";

	sprintf(hwname, "hw:%d", cardnum);
	if (snd_ctl_open(&ctld, hwname, 0) >= 0)
	{
	    int devicenum = -1;
	    do
	    {
		if (snd_ctl_rawmidi_next_device(ctld, &devicenum) < 0)
		    break;
		if (devicenum >= 0)
		{
		    pusamidi_enumerate_subdevices(ctld, cardnum, devicenum, type, func);
		}
	    }
	    while (devicenum >= 0);

	    snd_ctl_close(ctld);
	}

	if (snd_card_next(&cardnum) < 0)
	    return;
    }
}

static void *pusamidi_enumeration_thread(void *arg)
{
    while (1)
    {
	pusamidi_enumerate_devices(SND_RAWMIDI_STREAM_INPUT, pusamidi_thread_create);
	pusamidi_enumerate_devices(SND_RAWMIDI_STREAM_OUTPUT, pusamidi_output_create);

	sleep(1);
    }
}

void pusamidi_init(void)
{
    pthread_t tid;
    pthread_attr_t attr;

    pthread_attr_init(&attr);
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
    pthread_create(&tid, &attr, pusamidi_enumeration_thread, NULL);
    pthread_attr_destroy(&attr);
}

int pusamidi_get_midi_in(void)
{
    int c = -1;

    pthread_mutex_lock(&pusamidi_fifo_lock);
    if (pusamidi_in_fifo_out != pusamidi_in_fifo_in)
    {
	c = pusamidi_in_fifo[pusamidi_in_fifo_out++];
	if (pusamidi_in_fifo_out >= sizeof(pusamidi_in_fifo))
	    pusamidi_in_fifo_out = 0;
    }

    pthread_mutex_unlock(&pusamidi_fifo_lock);

    return c;
}

void pusamidi_send_midi_out(const void *buffer, size_t len)
{
    for (int i = 0; i < PUSAMIDI_PORT_MAX; i++)
    {
	snd_rawmidi_t *handle = pusamidi_outs[i].midiport;
	if (handle)
	    snd_rawmidi_write(handle, buffer, len);
    }
}

#ifdef PUSAMIDI_UNIT_TEST
int main(int argc, char **argv)
{
    pusamidi_init();

    sleep(1);
    unsigned char msg[6] = { 0xf0, 0x7e, 0x7f, 0x06, 0x01, 0xf7 };
    pusamidi_send_midi_out(msg, sizeof(msg));

    while (1)
    {
	int c = pusamidi_get_midi_in();

	if (c >= 0)
	{
	    if ((c & 0x80) && c != 0xf7)
		printf("\n");

	    printf("%02x ", c);
	    fflush(stdout);
	}
    }

    return 0;
}
#endif
