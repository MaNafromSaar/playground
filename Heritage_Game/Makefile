CXX ?= c++
CXXFLAGS ?= -Wall -Wextra -Werror -std=c++17

TARGET := heritage_game
SOURCES := main.cpp Creature.cpp

all: $(TARGET)

$(TARGET): $(SOURCES) Creature.hpp
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

clean:
	rm -f $(TARGET)

.PHONY: all clean