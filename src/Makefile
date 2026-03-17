CXX = clang
CXXWFLAGS = -Wall -Wextra -Wdeprecated-declarations -Wpointer-arith -Wwrite-strings -Wmissing-noreturn -Wno-unused-parameter -Wunused-variable -Winit-self -Wshadow -Wconversion -Wold-style-cast
CXXFLAGS = -c -O3 -std=c++23
INCLUDES = 
LD = clang
LDFLAGS = -shared -Wl,--dll,--enable-stdcall-fixup
LIBS = -static -lstdc++ -lwinpthread
RM = rm -f
SH = bash

LIBNAME = resize_ks
TARGET = $(LIBNAME).auf2
OBJ = $(LIBNAME).o
SRC = $(LIBNAME).cpp
HDRS = filter2.hpp version.hpp
DEF = $(LIBNAME).def

.SUFFIXES: .cpp .o

all: $(TARGET)

$(TARGET): $(OBJ) $(RES) $(DEF)
	$(LD) $(LDFLAGS) -o $(TARGET) $(OBJ) $(DEF) $(LIBS)
	strip $(TARGET)

$(OBJ): $(SRCS) $(HDRS) $(SUBSRCS)
	$(CXX) $(CXXWFLAGS) $(CXXFLAGS) $(INCLUDES) -o $(OBJ) $(SRC)

clean:
	$(RM) $(TARGET) $(OBJ) $(RES)

release:
	make all
	cp -f $(TARGET) ../
	$(SH) makezip.sh $(LIBNAME)
	make clean
