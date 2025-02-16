/*
 * Header file for library.
 */

#ifndef __pusa_h__
#define __pusa_h__

typedef void (*pusa_audio_handler_t)(int *data, int nchannels);

int pusa_init(const char *codec_name, pusa_audio_handler_t func);
void pusa_print_stats(void);

void pusa_stall_rt(void);
void pusa_unstall_rt(void);

#endif /* __pusa_h__ */
