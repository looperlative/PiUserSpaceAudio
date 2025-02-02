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

#define MIDI_PORT_MAX	32

static pthread_t midi_in_tids[MIDI_PORT_MAX];
static int midi_in_num;

static snd_rawmidi_t *midi_outs[MIDI_PORT_MAX];
static int midi_out_num = 0;

void *midi_in_thread(void *arg)
{
    snd_rawmidi_t *midiport;
    char *hwname = arg;

    if (snd_rawmidi_open(&midiport, NULL, hwname, 0) >= 0)
    {
	printf(" In: tid: %d, name %s\n", gettid(), hwname);
    }

    char buffer[10];
    while (1)
    {
	int status = snd_rawmidi_read(midiport, buffer, sizeof(buffer));
	if (status < 0 && status != -EBUSY && status != -EAGAIN)
	{
	    printf("%s: %s\n", hwname, snd_strerror(status));
	    return NULL;
	}
	else if (status > 0)
	{
	    for (int j = 0; j < status; j++)
		printf("%02x ", buffer[j]);
	    printf("\n");
	}
    }

    return NULL;
}

void enumerate_subdevices(snd_ctl_t *ctld, int cardnum, int devicenum)
{
    snd_rawmidi_info_t *info;
    snd_rawmidi_info_alloca(&info);

    snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_INPUT);
    snd_rawmidi_info_set_device(info, devicenum);
    snd_ctl_rawmidi_info(ctld, info);

    int n = snd_rawmidi_info_get_subdevices_count(info);
    for (int i = 0; i < n; i++)
    {
	snd_rawmidi_info_set_subdevice(info, i);
	snd_ctl_rawmidi_info(ctld, info);
	const char *name = snd_rawmidi_info_get_subdevice_name(info);

	if (midi_in_num < MIDI_PORT_MAX)
	{
	    char hwname[100];
	    sprintf(hwname, "hw:%d,%d,%d", cardnum, devicenum, i);
	    pthread_create(&midi_in_tids[midi_in_num], NULL, midi_in_thread, strdup(hwname));
	}
    }

    snd_rawmidi_info_set_stream(info, SND_RAWMIDI_STREAM_OUTPUT);
    snd_rawmidi_info_set_device(info, devicenum);
    snd_ctl_rawmidi_info(ctld, info);

    n = snd_rawmidi_info_get_subdevices_count(info);
    for (int i = 0; i < n; i++)
    {
	snd_rawmidi_info_set_subdevice(info, i);
	snd_ctl_rawmidi_info(ctld, info);
	const char *name = snd_rawmidi_info_get_subdevice_name(info);

	char hwname[100];
	sprintf(hwname, "hw:%d,%d,%d", cardnum, devicenum, i);

	if (midi_out_num < MIDI_PORT_MAX &&
	    snd_rawmidi_open(NULL, &midi_outs[midi_out_num], hwname, SND_RAWMIDI_SYNC) >= 0)
	{
	    printf("Out: hw:%d,%d,%d: %s\n", cardnum, devicenum, i, name);
	    midi_out_num++;
	}
    }
}

int main(int argc, char **argv)
{
    int cardnum = -1;
    snd_ctl_t *ctld;
    char hwname[100];

    if (snd_card_next(&cardnum) < 0)
	return 0;

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
		    enumerate_subdevices(ctld, cardnum, devicenum);
		}
	    }
	    while (devicenum >= 0);

	    snd_ctl_close(ctld);
	}

	if (snd_card_next(&cardnum) < 0)
	    return 0;
    }

    while (1)
    {
    }

    return 0;
}
