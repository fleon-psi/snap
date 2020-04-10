#
# Copyright 2017 International Business Machines
# Copyright 2020 Paul Scherrer Institute
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

HOSTNAME := $(shell hostname)
UNAME_P := $(shell uname -p)

# Finding $SNAP_ROOT
ifndef SNAP_ROOT
# check if we are in sw folder of an action (three directories below snap root)
ifneq ("$(wildcard ../../../ActionTypes.md)","")
SNAP_ROOT=$(abspath ../../../)
else
$(info You are not building your software from the default directory (/path/to/snap/actions/<action_name>/sw) or specified a wrong $$SNAP_ROOT.)
$(error Please make sure that $$SNAP_ROOT is set up correctly.)
endif
endif

SNAP_LIBS=${SNAP_ROOT}/software/lib/libsnap.a -lcxl 
SNAP_INCLUDE=-I${SNAP_ROOT}/software/include -I/home/jungfrau/git/pslse/common

ifeq ($(UNAME_P), ppc64le)
    CXX=xlC_r
    CC=xlc_r
    CFLAGS= -std=c99 -qipa -O5 -Wall -mcpu=power9 -qarch=pwr9 -DUSE_ZSTD
    CXXFLAGS= -std=c++11 -qipa -O5 -Wall -mcpu=power9 -qarch=pwr9 -DUSE_ZSTD
    LDFLAGS= -qipa -O5 -mcpu=power9 -qarch=pwr9 -llz4 -lm -lpthread -lz -libverbs
    EXECS=JFReceiver
endif

ifeq ($(UNAME_P), x86_64)
    CXX=icpc
    CC=icc
    CFLAGS=-g -std=c99 -Ofast -ipo -Wall -DUSE_ZSTD
    CXXFLAGS= -std=c++11 -Ofast -xHost -ipo -Wall -DUSE_ZSTD
    LDFLAGS= -Ofast -xHost -ipo -llz4 -lm -lpthread -lz -libverbs
    EXECS=JFWriter
endif

HDF5_PATH=/opt/hdf5-1.10.5
HDF5_LIBS=$(HDF5_PATH)/lib/libhdf5.a

CPPFLAGS= -I. -I../include -I${HDF5_PATH}/include ${SNAP_INCLUDE}

RCV_SRCS=JFReceiver.o sharedVariables.o SendThread.o IB_Transport.o SnapThread.o
WR_SRCS=JFWriter.o HDF5_plugin.o IB_Transport.o bitshuffle/bitshuffle.o bitshuffle/bitshuffle_core.o bitshuffle/iochain.o

#all: JFReceiver JFWriter
all: $(EXECS)

JFWriter: $(WR_SRCS)
	$(CXX) $(WR_SRCS) -o JFWriter $(JF_LDLIBS) $(HDF5_LIBS) $(LDFLAGS) -lzstd

JFReceiver: $(RCV_SRCS)
	$(CXX) $(RCV_SRCS) -o JFReceiver $(JF_LDLIBS) $(LDFLAGS) $(SNAP_LIBS)

clean:
	rm -f *.o bitshuffle/*.o JFReceiver JFWriter
 
