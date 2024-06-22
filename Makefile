# Directories
NH_DIR=neushoorn2
BIN_DIR=bin
SRC_DIR=src

# Compiler and linker flags
CFLAGS = -ansi -Wpedantic -Wall -Wextra -Werror -Wno-unused-function
CFLAGS += -DNH_INCLUDE_STDLIB -DDEBUG -I$(NH_DIR)/include
LDFLAGS = -lm -ggdb
LDFLAGS += -lvulkan
LDFLAGS += -lSDL2

# Neushoorn
$(NH_DIR):
	git clone https://github.com/alexydens/neushoorn2.git
# Build to here
$(BIN_DIR):
	mkdir -p $(BIN_DIR)

# Test binary
$(BIN_DIR)/test: $(SRC_DIR)/main.c $(BIN_DIR) $(NH_DIR)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ src/*.c

# Shader binaries
$(BIN_DIR)/vert.spv: shader.vert
	glslc $< -o $@
$(BIN_DIR)/frag.spv: shader.frag
	glslc $< -o $@

# Phony targets
.PHONY: test clean

clean:
	rm -rf $(BIN_DIR) $(NH_DIR)

test: $(BIN_DIR)/test $(BIN_DIR)/vert.spv $(BIN_DIR)/frag.spv
	$(BIN_DIR)/test
