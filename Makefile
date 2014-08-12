CFLAGS=-Wall -Wextra --std=c99 -O2
CUPSDIR=$(shell cups-config --serverbin)
CUPSDATADIR=$(shell cups-config --datadir)

all:	m2x00w-decode rastertom2x00w

ppd:	ppd/*.ppd

m2x00w-decode:	m2x00w-decode.c m2x00w.h
	gcc $(CFLAGS) m2x00w-decode.c -o m2x00w-decode

rastertom2x00w:	rastertom2x00w.c m2x00w.h
	gcc $(CFLAGS) rastertom2x00w.c -o rastertom2x00w -lcupsimage -lcups

ppd/*.ppd: m2x00w.drv
	ppdc m2x00w.drv

clean:
	rm -f m2x00w-decode rastertom2x00w

install: rastertom2x00w
	install -s rastertom2x00w $(CUPSDIR)/filter/
	install -m 644 m2x00w.drv $(CUPSDATADIR)/drv/
