CXX ?= c++
CXXFLAGS ?= -Wall -Wextra -Werror -std=c++17
TARGET := quaternion_calculator
SOURCES := Quaternion.cpp

all: $(TARGET)

$(TARGET): $(SOURCES) Quaternion.hpp
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean