# Compiler
CC  = clang
CXX = clang++

# Compile-time flags
FLAGS += -Xclang -disable-O0-optnone -w -emit-llvm -S -DNDEBUG

# Source files
CSRCS = $(wildcard *.c)
CXXSRCS = $(wildcard *.cc)

# LLVM files
CLLS   = $(CSRCS:.c=.ll)
CXXLLS = $(CXXSRCS:.cc=.ll)

#
# BUILD
#
.PHONY: depend clean

all:    $(CLLS) $(CXXLLS)

%.ll: %.c
	$(CC) $(FLAGS) $< -o $@

%.ll: %.cc
	$(CXX) $(FLAGS) $< -o $@

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
