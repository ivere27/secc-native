CC=g++
CFLAGS=-c -Wall -std=c++11 \
		-I./SimpleHttpRequest/http-parser/ \
		-I./SimpleHttpRequest/libuv/include/ \
		-I./SimpleHttpRequest/openssl/include/ \
		-I./SimpleHttpRequest \
		-I./SimpleProcessSpawn \
		-I./json/src
LDFLAGS=-lpthread -lz \
		./SimpleHttpRequest/libuv/.libs/libuv.a \
		./SimpleHttpRequest/openssl/libcrypto.a \
		./SimpleHttpRequest/http-parser/http_parser.o
SOURCES=secc.cpp
OBJECTS=$(SOURCES:.cpp=.o)
EXECUTABLE=secc

all: $(SOURCES) $(EXECUTABLE)

$(EXECUTABLE): $(OBJECTS)
		$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

.cpp.o:
		$(CC) $(CFLAGS) $< -o $@

clean:
		rm -rf *.o $(EXECUTABLE)