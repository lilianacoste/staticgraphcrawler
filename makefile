# Compiler
CXX = g++

# Compiler flags
CXXFLAGS = -std=c++11 -Wall -I"C:/Users/liliana/rapidjson/include"

# Target executable
TARGET = graph

# Source files
SRC = graph.cpp

# Build rule
all: $(TARGET)

$(TARGET): $(SRC)
	$(CXX) $(CXXFLAGS) -o $(TARGET) $(SRC)

# Clean rule (Windows-compatible)
clean:
	del /f $(TARGET)
