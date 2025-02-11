#

default:
#	-rsync -avu --include='*.[ch]' --exclude '*' . pizero:work && \
#	ssh pizero -t 'cd work; make remote'
	-rsync -avu --include='*.[ch]' --include='Makefile' --exclude '*' . looperpi2:work && \
	ssh looperpi2 -t 'cd work; make remote'

remote: t midit

t: t.c bcmhw.h bcmhw.c pusa.c codecs.c codecs.h pusa.h
	gcc -g -o t t.c bcmhw.c codecs.c pusa.c -li2c

midit: pusamidi.c
	gcc -g -DPUSAMIDI_UNIT_TEST -o midit $< -lasound
