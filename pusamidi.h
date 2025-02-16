#ifndef __pusamidi_h__
#define __pusamidi_h__

void pusamidi_init(void);
int pusamidi_get_midi_in(void);
void pusamidi_send_midi_out(const void *buffer, size_t len);

#endif /* __pusamidi_h__ */
