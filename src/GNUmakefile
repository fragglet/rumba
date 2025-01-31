
DESTDIR =
PREFIX = /usr/local
MANPATH = $(PREFIX)/share/man
DATADIR = /var/rumba

SMBLOGFILE = /var/log/rumba_smbd.log

DEFINES = -DSMBLOGFILE=\"$(SMBLOGFILE)\" \
          -D_FORTIFY_SOURCE=1

IWYU = iwyu
IWYU_FLAGS = --error
IWYU_TRANSFORMED_FLAGS = $(patsubst %,-Xiwyu %,$(IWYU_FLAGS))

CFLAGS = -O2 -MMD -Wall $(DEFINES)
LDFLAGS =

ifdef FIND_UNUSED_CODE
CFLAGS += -ffunction-sections -fdata-sections
LDFLAGS += -Wl,--gc-sections -Wl,--print-gc-sections
endif

OBJECTS = \
	dir.o                \
	ipc.o                \
	locking.o            \
	mangle.o             \
	reply.o              \
	server.o             \
	shares.o             \
	strfunc.o            \
	strlcat.o            \
	strlcpy.o            \
	system.o             \
	timefunc.o           \
	trans2.o             \
	util.o

DEPS = $(patsubst %.o,%.d,$(OBJECTS))

all: rumba_smbd

rumba_smbd: $(OBJECTS)
	$(CC) $(CFLAGS) $(LDFLAGS) $(OBJECTS) -o $@

.c.o:
	$(CC) $(CFLAGS) -c $<

clean:
	rm -f $(OBJECTS) rumba_smbd $(DEPS)

format:
	clang-format -i *.[ch]

install:
	mkdir -p $(DESTDIR)$(PREFIX)/bin
	install -m 755 rumba_smbd $(DESTDIR)$(PREFIX)/bin/rumba_smbd
	mkdir -p $(DESTDIR)$(PREFIX)/lib/systemd/system
	install -m 644 rumba_smbd.service $(DESTDIR)$(PREFIX)/lib/systemd/system/rumba_smbd.service
	mkdir -m 777 -p $(DESTDIR)$(DATADIR)/public
	install -m 666 doc-public.txt $(DESTDIR)$(DATADIR)/public/README.txt
	mkdir -m 755 -p $(DESTDIR)$(DATADIR)/readonly
	install -m 644 doc-readonly.txt $(DESTDIR)$(DATADIR)/readonly/README.txt
	mkdir -m 755 -p $(DESTDIR)$(MANPATH)/man8
	install rumba_smbd.8 $(DESTDIR)$(MANPATH)/man8/rumba_smbd.8

fixincludes:
	for d in $(patsubst %.o,%.c,$(OBJECTS)); do \
		$(IWYU) $(IWYU_TRANSFORMED_FLAGS) 2>&1 $$d | fix_include; \
	done

.PHONY: clean format all install

-include $(DEPS)
