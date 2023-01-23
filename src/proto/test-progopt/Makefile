BOOST_DIR = /usr/tce/packages/boost/boost-1.69.0-mvapich2-2.3-gcc-8.1.0
PROTO_DIR = ../../../install

BOOST_INC_DIR = $(BOOST_DIR)/include
BOOST_LIB_DIR = $(BOOST_DIR)/lib

PROTO_INC_DIR = $(PROTO_DIR)/include
PROTO_LIB_DIR = $(PROTO_DIR)/lib64
PROTOC = $(PROTO_DIR)/bin/protoc

CXXFLAGS = -Wall -std=c++17 -m64 -I../.. -I$(BOOST_INC_DIR) -I$(PROTO_INC_DIR)

ifndef PROTO_DIR
  $(error Set PROTO_DIR variable to the path of the protocol buffer installation)
endif

# In case of using clang, pick the right std library that is compatible with
# the one used in building the protocol buffer

ifeq ($(CXX), clang++)
    #STDLIB = --stdlib=libc++
    STDLIB = --gcc-toolchain=/usr/tce/packages/gcc/gcc-8.1.0
endif

LDFLAGS = $(STDLIB) -m64 -Wl,-rpath=$(BOOST_LIB_DIR),--no-undefined -L$(BOOST_LIB_DIR) -lboost_program_options -Wl,-rpath=$(PROTO_LIB_DIR),--no-undefined -L$(PROTO_LIB_DIR) -lprotobuf

all: $(PROTOC) dr_evt_proto_progopt_test

$(PROTOC):
	@echo "Checking if '$(PROTOC)' exists ..."
	@test -x $(PROTOC) || { echo "'$(PROTOC)' does not exist!"; echo "Set PROTO_DIR to a correct path."; }
	@exit 1

dr_evt_params.pb.cc: $(PROTOC) ../dr_evt_params.proto
	$(PROTOC) --proto_path=.. --cpp_out=. dr_evt_params.proto

dr_evt_params.pb.o: dr_evt_params.pb.cc
	$(CXX) $(CXXFLAGS) $(STDLIB) -c dr_evt_params.pb.cc -o dr_evt_params.pb.o

dr_evt_proto_progopt_test: dr_evt_proto_progopt_test.cpp dr_evt_params.pb.o dr_evt_params.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o dr_evt_proto_progopt_test dr_evt_proto_progopt_test.cpp dr_evt_params.pb.o dr_evt_params.o

dr_evt_params.o: ../../params/dr_evt_params.cpp ../../params/dr_evt_params.hpp
	$(CXX) $(CXXFLAGS) -c ../../params/dr_evt_params.cpp

test: dr_evt_proto_progopt_test
	@echo ----------------------------------------------------
	./dr_evt_proto_progopt_test --setup-all ../test/dr_evt_params.prototext --input-model model
	@echo ----------------------------------------------------
	./dr_evt_proto_progopt_test --setup-all ../test/dr_evt_params.prototext --input-model model --setup-sim sim_setup.prototext
	@echo ----------------------------------------------------
	./dr_evt_proto_progopt_test --input-model model --setup-sim sim_setup.prototext
	@echo ----------------------------------------------------

clean:
	@rm -f *.o dr_evt_proto_progopt_test
	@rm -f dr_evt_params.pb.cc  dr_evt_params.pb.h
