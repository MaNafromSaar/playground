CXX ?= c++
CXXFLAGS ?= -Wall -Wextra -Werror -std=c++17
CC ?= cc
CFLAGS ?= -Wall -Wextra -Werror

TARGET := heritage_game
SOURCES := main.cpp Creature.cpp
SERVER_TARGET := network_duel_server
SERVER_SOURCES := network_duel_server.cpp Creature.cpp
CLIENT_TARGET := network_duel_client
CLIENT_SOURCES := network_duel_client.c

all: $(TARGET) $(SERVER_TARGET) $(CLIENT_TARGET)

$(TARGET): $(SOURCES) Creature.hpp
	$(CXX) $(CXXFLAGS) $(SOURCES) -o $(TARGET)

$(SERVER_TARGET): $(SERVER_SOURCES) Creature.hpp
	$(CXX) $(CXXFLAGS) $(SERVER_SOURCES) -o $(SERVER_TARGET)

$(CLIENT_TARGET): $(CLIENT_SOURCES)
	$(CC) $(CFLAGS) $(CLIENT_SOURCES) -o $(CLIENT_TARGET)

visual-client:
	python3 visual_duel_client.py

clean:
	rm -f $(TARGET) $(SERVER_TARGET) $(CLIENT_TARGET)

.PHONY: all clean visual-client