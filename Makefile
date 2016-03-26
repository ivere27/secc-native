CC=g++
CFLAGS=-c -Wall -std=c++11 -O3
LDFLAGS=-lcrypto -lcpprest -lz
SOURCES=utils.cpp untar.cpp zip.cpp secc.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=secc

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
		$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

.cpp.o:
		$(CC) $(CFLAGS) $< -o $@

clean:
		rm -rf *.o $(EXECUTABLE)