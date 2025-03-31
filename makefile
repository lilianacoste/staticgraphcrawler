# Compiler
CXX = g++

# Compiler flags
CXXFLAGS =  -std=c++17 -Wall -I./rapidjson/include



#Linker flags
LDFLAGS = -lcurl -lpthread
# Target executable
TARGET = graph

# Source files
SRC = graph.cpp

# Build rule
all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC) $(LDFLAGS)

# Clean rule (Windows-compatible)
clean:
	rm -f $(TARGET)
