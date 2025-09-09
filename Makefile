CC     = gcc
CFLAGS = -Wall -Wextra -O2 -std=gnu17
LDFLAGS =

.PHONY: all clean

TARGET1 = approx-client
TARGET2 = approx-server

all: $(TARGET1) $(TARGET2)

$(TARGET1): $(TARGET1).o err.o common.o messages.o cb.o queue.o client.h
$(TARGET2): $(TARGET2).o err.o common.o messages.o cb.o queue.o client.h


err.o: err.c err.h
queue.o: queue.c queue.h err.h
common.o: common.c err.h common.h
cb.o: cb.c cb.h err.h
messages.o: messages.c messages.h cb.h err.h queue.h common.h client.h

approx-client.o: approx-client.c err.h common.h messages.h cb.h queue.h
approx-server.o: approx-server.c err.h common.h messages.h cb.h queue.h client.h

clean:
	rm -f $(TARGET1) $(TARGET2) *.o *~