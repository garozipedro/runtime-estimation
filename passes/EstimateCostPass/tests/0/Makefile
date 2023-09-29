#
# TARGET
#
TARGET = a.out

#
# VARIABLES
#

# Compiler
CXX = clang

# LLVM opt
OPT = opt

# LLVM linker
LINK = llvm-link

# LLVM compiler
LLC = llc

# Compile-time flags
CXXFLAGS += -Xclang -disable-O0-optnone -w -emit-llvm -DSPEC_CPU -DNDEBUG -DSPEC_CPU_LITTLE_ENDIAN -DSPEC_CPU_LP64

# Includes
INCLUDES =

# Library paths
LFLAGS +=

# Linker flags
LIBS = -lm

# Source files
SRCS = $(wildcard *.c)

# LLVM files
LLS = $(SRCS:.c=.ll)

# Object file
OBJ = program.o

#
# BUILD
#

.PHONY: depend clean

all:    $(LLS)

%.ll: %.c
	$(CXX) $(CXXFLAGS) $(INCLUDES) -S $< -o $@


#
# CLEAN
#
clean:
	$(RM) *.ll *~

#
# DEPEND
#
depend: $(SRCS)
	makedepend $(INCLUDES) $^
