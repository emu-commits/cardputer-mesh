CXX      ?= g++
CC       ?= cc
# -MMD -MP emit per-source header deps so editing a .h rebuilds its dependents
# (avoids stale-object link errors). -Ithird_party/sqlite for the wiki backing.
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Isrc -I../third_party/sqlite -MMD -MP
LDLIBS   ?= -lm
SRC      := $(shell find src -name '*.cpp')
OBJ      := $(SRC:.cpp=.o)
DEP      := $(OBJ:.o=.d)
BIN      := emu

# Vendored SQLite amalgamation (FTS5), shared with the device build. Auto-fetched
# on first build via tools/fetch_sqlite.sh; compiled minimal + thread-unsafe.
SQLITE_DIR  := ../third_party/sqlite
SQLITE_OBJ  := $(SQLITE_DIR)/sqlite3.o
SQLITE_DEFS := -DSQLITE_ENABLE_FTS5 -DSQLITE_THREADSAFE=0 -DSQLITE_OMIT_LOAD_EXTENSION -DSQLITE_DQS=0

all: $(BIN)

$(BIN): $(OBJ) $(SQLITE_OBJ)
	$(CXX) $(CXXFLAGS) -o $@ $(OBJ) $(SQLITE_OBJ) $(LDLIBS)

%.o: %.cpp
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(SQLITE_DIR)/sqlite3.c:
	../tools/fetch_sqlite.sh

$(SQLITE_OBJ): $(SQLITE_DIR)/sqlite3.c
	$(CC) -O2 $(SQLITE_DEFS) -c $< -o $@

run: all
	./$(BIN)

clean:
	rm -f $(OBJ) $(DEP) $(BIN)

# note: SQLITE_OBJ is intentionally NOT removed by clean (slow to rebuild)
.PHONY: all run clean

-include $(DEP)
