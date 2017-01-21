INCLUDE_DIR=$(shell echo ~)/local/include
LIBRARY_DIR=$(shell echo ~)/local/lib
DESDTIR=/
PREFIX=/usr

CXX=g++
CXXFLAGS=-L${LIBRARY_DIR} -I${INCLUDE_DIR} -march=native -O3 -flto -std=c++11 -fPIC -Wall -Wextra -g

all: mux demux

install: all
	mkdir -p ${DESTDIR}/${PREFIX}/lib
	cp mux ${DESTDIR}/${PREFIX}/bin
	cp demux ${DESTDIR}/${PREFIX}/bin

mux: src/mux.cc
	${CXX} ${CXXFLAGS} -o mux src/mux.cc -lboost_program_options -lpthread

demux: src/demux.cc
	${CXX} ${CXXFLAGS} -o demux src/demux.cc -lboost_program_options -lpthread

clean:
	rm -rf mux
	rm -rf demux
