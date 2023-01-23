PROTO_DIR = ../../../install

PROTO_INC_DIR = $(PROTO_DIR)/include
PROTO_LIB_DIR = $(PROTO_DIR)/lib64
PROTOC = $(PROTO_DIR)/bin/protoc
CXXFLAGS = -Wall -std=c++17 -m64 -I../.. -I$(PROTO_INC_DIR)

ifndef PROTO_DIR
  $(error Set PROTO_DIR variable to the path of the protocol buffer installation)
endif

# In case of using clang, pick the right std library that is compatible with
# the one used in building the protocol buffer

ifeq ($(CXX), clang++)
    #STDLIB = --stdlib=libc++
    STDLIB = --gcc-toolchain=/usr/tce/packages/gcc/gcc-8.1.0
endif

LDFLAGS = $(STDLIB) -m64 -Wl,-rpath=$(PROTO_LIB_DIR),--no-undefined -L$(PROTO_LIB_DIR) -lprotobuf

all: $(PROTOC) dr_evt_proto_test dr_evt_params.protobin

$(PROTOC):
	@echo "Checking if '$(PROTOC)' exists ..."
	@test -x $(PROTOC) || { echo "'$(PROTOC)' does not exist!"; echo "Set PROTO_DIR to a correct path."; }
	@exit 1

dr_evt_params.pb.cc: $(PROTOC) ../dr_evt_params.proto
	$(PROTOC) --proto_path=.. --cpp_out=. dr_evt_params.proto

dr_evt_params.pb.o: dr_evt_params.pb.cc
	$(CXX) $(CXXFLAGS) $(STDLIB) -c dr_evt_params.pb.cc -o dr_evt_params.pb.o

dr_evt_proto_test: dr_evt_proto_test.cpp dr_evt_params.pb.o
	$(CXX) $(CXXFLAGS) $(LDFLAGS) -o dr_evt_proto_test dr_evt_proto_test.cpp dr_evt_params.pb.o

# Convert the prototext file into a binary file
dr_evt_params.protobin: $(PROTOC) dr_evt_params.prototext
	$(PROTOC) --proto_path=.. --encode="dr_evt_proto.DR_EVT_Params" dr_evt_params.proto < dr_evt_params.prototext > dr_evt_params.protobin

# Try reading both the prototext input and its binary counterpart
test: dr_evt_proto_test dr_evt_params.protobin
	./dr_evt_proto_test dr_evt_params.prototext > readtext.txt
	./dr_evt_proto_test dr_evt_params.protobin 1 > readbin.txt
	@egrep -v '^[[:blank:]]*#' dr_evt_params.prototext | diff readtext.txt -
	@egrep -v '^[[:blank:]]*#' dr_evt_params.prototext | diff readbin.txt -
	
clean:
	@rm -f *.o dr_evt_proto_test
	@rm -f dr_evt_params.protobin
	@rm -f dr_evt_params.pb.cc  dr_evt_params.pb.h
	@rm -f readtext.txt readbin.txt
