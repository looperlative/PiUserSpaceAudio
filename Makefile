#

default:
#	-rsync -avu --include='*.[ch]' --exclude '*' . pizero:work && \
#	ssh pizero -t 'cd work; make remote'
	-rsync -avu --include='*.[ch]' --include='Makefile' --exclude '*' . looperpi2:work && \
	ssh looperpi2 -t 'cd work; make remote'

remote: t midit

t: t.c bcmhw.h bcmhw.c
	gcc -g -o t t.c bcmhw.c codec_lp1b.c -li2c

midit: midit.c
	gcc -g -o midit midit.c -lasound
