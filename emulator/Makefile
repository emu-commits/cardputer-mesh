CXX      ?= g++
# -MMD -MP emit per-source header deps so editing a .h rebuilds its dependents
# (avoids stale-object link errors).
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Isrc -MMD -MP
SRC      := $(shell find src -name '*.cpp')
OBJ      := $(SRC:.cpp=.o)
DEP      := $(OBJ:.o=.d)
BIN      := emu

all: $(BIN)

$(BIN): $(OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

run: all
	./$(BIN)

clean:
	rm -f $(OBJ) $(DEP) $(BIN)

-include $(DEP)

.PHONY: all run clean
