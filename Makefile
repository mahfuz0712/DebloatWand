APP      := DebloatWand
SRC_DIR  := src
OBJ_DIR  := build/obj
DIST_DIR := dist
TARGET   := $(DIST_DIR)/$(APP).exe

CXX      := g++
WINDRES  := windres
CXXFLAGS := -std=c++17 -O2 -Wall -Wextra -DUNICODE -D_UNICODE -D_WIN32_WINNT=0x0A00 -DWINVER=0x0A00 -MMD -MP
LDFLAGS  := -static -static-libgcc -static-libstdc++ -mwindows -municode -s
LDLIBS   := -lcomctl32 -lcomdlg32 -lshell32

SRCS := $(wildcard $(SRC_DIR)/*.cpp)
OBJS := $(patsubst $(SRC_DIR)/%.cpp,$(OBJ_DIR)/%.o,$(SRCS)) $(OBJ_DIR)/resource.o

.PHONY: all run clean rebuild
all: $(TARGET)

$(TARGET): $(OBJS) | $(DIST_DIR)
	$(CXX) $(OBJS) -o $@ $(LDFLAGS) $(LDLIBS)

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(OBJ_DIR)/resource.o: $(SRC_DIR)/resource.rc $(SRC_DIR)/app.manifest | $(OBJ_DIR)
	cd $(SRC_DIR) && $(WINDRES) -O coff -i resource.rc -o ../$(OBJ_DIR)/resource.o

$(OBJ_DIR) $(DIST_DIR):
	mkdir -p $@

run: all
	./$(TARGET)
clean:
	rm -rf build dist
rebuild: clean all
-include $(OBJS:.o=.d)
