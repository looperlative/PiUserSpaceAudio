/*
 * Header file for library.
 */

#ifndef __pusa_h__
#define __pusa_h__

typedef void (*pusa_audio_handler_t)(int *data, int nchannels);
typedef int (*pusa_rt_func)(void *parm);

int pusa_init(const char *codec_name, pusa_audio_handler_t func);
void pusa_print_stats(void);
int pusa_execute_in_rt(pusa_rt_func func, void *parm);

#endif /* __pusa_h__ */
