#gmake Makefile
EXECUTABLE = tss

SRC    = src/main.c
CFLAGS = -Wall -ansi -pedantic -lcurses -lcrypt -s #-DBSD
COMPILE= $(CC) $(CFLAGS)
CC = gcc

all: $(EXECUTABLE)

$(EXECUTABLE): $(SRC)
	$(CC) $(CFLAGS) -o $(EXECUTABLE) $(SRC)

%.o: %.c
	$(COMPILE) -o $@ $<

clean:
	-rm -f $(OBJS) $(EXECUTABLE) src/*~
	-rm -f ./*~
