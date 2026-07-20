#include "Creature.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <memory>
#include <netinet/in.h>
#include <random>
#include <sstream>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <unordered_set>
#include <vector>

namespace {

constexpr int kRosterSize = 5;
constexpr int kRoundLineupSize = 3;
constexpr int kSafetyMaxRounds = 21;

constexpr int kMaxLevel = 9;
constexpr int kInitialUpgradePoints = 13;
constexpr int kUpgradePointsPerLevel = 5;
constexpr int kMaxTotalUpgradePoints = kInitialUpgradePoints + (kMaxLevel - 1) * kUpgradePointsPerLevel;

constexpr int kXpParticipation = 40;
constexpr int kXpDuelWin = 30;
constexpr int kXpRoundWin = 25;
constexpr int kXpFlawlessBonus = 35;

constexpr std::array<int, kMaxLevel> kXpThresholds = {0, 80, 165, 255, 350, 450, 555, 665, 780};

bool g_emitEvents = true;

struct UpgradeAllocation {
	int attack = 0;
	int defense = 0;
	int life = 0;
	int evasion = 0;
};

struct StartVariance {
	double attackMul = 1.0;
	double defenseMul = 1.0;
	double lifeMul = 1.0;
	double evasionShift = 0.0;
};

struct Player {
	int fd = -1;
	std::string name;
	std::vector<std::unique_ptr<Creature>> roster;
	std::vector<UpgradeAllocation> upgrades;
	std::vector<StartVariance> startVariance;
	int roundsWon = 0;
	int matchPoints = 0;
	int flawlessWins = 0;
	int xp = 0;
	int level = 1;
	int unspentUpgradePoints = kInitialUpgradePoints;
};

struct EffectiveStats {
	double attack;
	double defense;
	double evasion;
	double life;
};

struct BoutResult {
	int winner; // 0 draw, 1 p1, 2 p2
	double hpOne;
	double hpTwo;
};

struct RoundResult {
	int winner; // 0 draw, 1 p1, 2 p2
	int boutWinsOne;
	int boutWinsTwo;
};

enum class MatchMode {
	Quick1,
	Standard3,
	Extended7,
	Max
};

struct MatchConfig {
	MatchMode mode;
	int maxRounds;
	int targetRoundWins;
};

void emitEventFd(int fd, const std::string &type, const std::string &payloadJson);
void emitEventPlayer(const Player &player, const std::string &type, const std::string &payloadJson);
void emitEventBoth(const Player &one, const Player &two, const std::string &type, const std::string &payloadJson);

thread_local std::mt19937 g_rng(std::random_device{}());

[[noreturn]] void fatal(const std::string &msg) {
	std::cerr << msg << '\n';
	std::exit(1);
}

bool sendAll(int fd, const std::string &text) {
	std::size_t sent = 0;
	while (sent < text.size()) {
		const ssize_t rc = send(fd, text.data() + sent, text.size() - sent, 0);
		if (rc <= 0) {
			return false;
		}
		sent += static_cast<std::size_t>(rc);
	}
	return true;
}

bool sendLine(int fd, const std::string &text) {
	return sendAll(fd, text + "\n");
}

bool recvLine(int fd, std::string &line) {
	line.clear();
	char c = 0;
	while (true) {
		const ssize_t rc = recv(fd, &c, 1, 0);
		if (rc <= 0) {
			return false;
		}
		if (c == '\r') {
			continue;
		}
		if (c == '\n') {
			return true;
		}
		line.push_back(c);
		if (line.size() > 1024) {
			return false;
		}
	}
}

std::string normalize(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
	return value;
}

std::string escapeJson(const std::string &value) {
	std::string escaped;
	escaped.reserve(value.size() + 8);
	for (char ch : value) {
		switch (ch) {
		case '\\':
			escaped += "\\\\";
			break;
		case '"':
			escaped += "\\\"";
			break;
		case '\n':
			escaped += "\\n";
			break;
		case '\r':
			escaped += "\\r";
			break;
		case '\t':
			escaped += "\\t";
			break;
		default:
			escaped += ch;
			break;
		}
	}
	return escaped;
}

void sendPlayer(const Player &player, const std::string &message) {
	if (!sendLine(player.fd, message)) {
		fatal("connection lost while sending to " + player.name);
	}
}

void sendBoth(const Player &one, const Player &two, const std::string &message) {
	sendPlayer(one, message);
	sendPlayer(two, message);
}

std::string archetypeName(Archetype archetype) {
	switch (archetype) {
	case Archetype::Ranger:
		return "Ranger";
	case Archetype::Warrior:
		return "Warrior";
	case Archetype::Tank:
		return "Tank";
	case Archetype::RangerWarrior:
		return "Ranger-Warrior";
	case Archetype::RangerTank:
		return "Ranger-Tank";
	case Archetype::WarriorTank:
		return "Warrior-Tank";
	case Archetype::EliteRanger:
		return "Elite-Ranger";
	case Archetype::EliteWarrior:
		return "Elite-Warrior";
	case Archetype::EliteTank:
		return "Elite-Tank";
	}
	return "Unknown";
}

std::string matchModeKey(MatchMode mode) {
	switch (mode) {
	case MatchMode::Quick1:
		return "1";
	case MatchMode::Standard3:
		return "3";
	case MatchMode::Extended7:
		return "7";
	case MatchMode::Max:
		return "max";
	}
	return "3";
}

std::string matchModeLabel(MatchMode mode) {
	switch (mode) {
	case MatchMode::Quick1:
		return "Quick (1 round)";
	case MatchMode::Standard3:
		return "Standard (best of 3)";
	case MatchMode::Extended7:
		return "Extended (best of 7)";
	case MatchMode::Max:
		return "Max (3 flawless wins or both players at level cap)";
	}
	return "Standard (best of 3)";
}

MatchConfig configForMode(MatchMode mode) {
	switch (mode) {
	case MatchMode::Quick1:
		return MatchConfig{mode, 1, 1};
	case MatchMode::Standard3:
		return MatchConfig{mode, 3, 2};
	case MatchMode::Extended7:
		return MatchConfig{mode, 7, 4};
	case MatchMode::Max:
		return MatchConfig{mode, kSafetyMaxRounds, 0};
	}
	return MatchConfig{MatchMode::Standard3, 3, 2};
}

bool isMaxMode(const MatchConfig &config) {
	return config.mode == MatchMode::Max;
}

bool bothPlayersAtLevelCap(const Player &playerOne, const Player &playerTwo) {
	return playerOne.level >= kMaxLevel && playerTwo.level >= kMaxLevel;
}

MatchConfig chooseMatchConfig(Player &selector, const Player &otherPlayer) {
	while (true) {
		sendPlayer(selector, "Choose match length: 1, 3, 7, or max");
		sendPlayer(selector, "1 = one round | 3 = best of 3 | 7 = best of 7 | max = until 3 flawless wins or both players hit level cap");
		sendPlayer(otherPlayer, selector.name + " is choosing the match length...");
		emitEventPlayer(selector, "prompt", "\"phase\":\"match_mode\"");

		std::string line;
		if (!recvLine(selector.fd, line)) {
			fatal("connection lost while choosing match mode for " + selector.name);
		}
		const std::string choice = normalize(line);
		if (choice == "1" || choice == "quick") {
			return configForMode(MatchMode::Quick1);
		}
		if (choice == "3" || choice == "standard") {
			return configForMode(MatchMode::Standard3);
		}
		if (choice == "7" || choice == "extended") {
			return configForMode(MatchMode::Extended7);
		}
		if (choice == "max") {
			return configForMode(MatchMode::Max);
		}
		sendPlayer(selector, "Invalid choice. Enter 1, 3, 7, or max.");
	}
}

int levelFromXp(int xp) {
	int level = 1;
	for (int i = 0; i < kMaxLevel; ++i) {
		if (xp >= kXpThresholds[i]) {
			level = i + 1;
		}
	}
	return level;
}

double playerLevelScale(int level) {
	return 1.0 + (level - 1) * 0.035;
}

std::unique_ptr<Creature> createBaseCreature(const std::string &rawType, const std::string &ownerName, int index) {
	const std::string type = normalize(rawType);
	if (type == "ranger" || type == "r") {
		return std::make_unique<Ranger>(ownerName + "-Ranger-" + std::to_string(index), 1);
	}
	if (type == "warrior" || type == "w") {
		return std::make_unique<Warrior>(ownerName + "-Warrior-" + std::to_string(index), 1);
	}
	if (type == "tank" || type == "t") {
		return std::make_unique<Tank>(ownerName + "-Tank-" + std::to_string(index), 1);
	}
	return nullptr;
}

StartVariance rollStartVariance() {
	std::uniform_real_distribution<double> scaleRoll(0.93, 1.07);
	std::uniform_real_distribution<double> evasionRoll(-0.012, 0.012);
	return StartVariance{scaleRoll(g_rng), scaleRoll(g_rng), scaleRoll(g_rng), evasionRoll(g_rng)};
}

EffectiveStats effectiveStatsFor(const Player &player, int creatureIndex) {
	const Stats &base = player.roster[creatureIndex]->stats();
	const UpgradeAllocation &up = player.upgrades[creatureIndex];
	const StartVariance &variance = player.startVariance[creatureIndex];
	const double scale = playerLevelScale(player.level);

	const double attack = (base.attack * variance.attackMul + up.attack * 1.4) * scale;
	const double defense = (base.defense * variance.defenseMul + up.defense * 1.2) * scale;
	const double life = (base.life * variance.lifeMul + up.life * 3.0) * scale;
	const double evasion = std::clamp(base.evasion + variance.evasionShift + up.evasion * 0.006 + (player.level - 1) * 0.003, 0.05, 0.45);
	return EffectiveStats{attack, defense, evasion, life};
}

bool inTopTenPercent(double value, double maxValue) {
	return maxValue > 0.0 && value >= maxValue * 0.9;
}

double maxPossibleAttack(const Player &player, int creatureIndex) {
	const Stats &base = player.roster[creatureIndex]->stats();
	const double scale = playerLevelScale(kMaxLevel);
	return (base.attack + kMaxTotalUpgradePoints * 1.4) * scale;
}

double maxPossibleDefense(const Player &player, int creatureIndex) {
	const Stats &base = player.roster[creatureIndex]->stats();
	const double scale = playerLevelScale(kMaxLevel);
	return (base.defense + kMaxTotalUpgradePoints * 1.2) * scale;
}

double maxPossibleLife(const Player &player, int creatureIndex) {
	const Stats &base = player.roster[creatureIndex]->stats();
	const double scale = playerLevelScale(kMaxLevel);
	return (base.life + kMaxTotalUpgradePoints * 3.0) * scale;
}

double maxPossibleEvasion(const Player &player, int creatureIndex) {
	const Stats &base = player.roster[creatureIndex]->stats();
	const double evasion = base.evasion + kMaxTotalUpgradePoints * 0.006 + (kMaxLevel - 1) * 0.003;
	return std::clamp(evasion, 0.05, 0.45);
}

std::string markTop10(bool top) {
	return top ? " [TOP10]" : "";
}

std::string formatCreatureStateJson(const Player &player, int index) {
	const EffectiveStats eff = effectiveStatsFor(player, index);
	const Stats &base = player.roster[index]->stats();
	const StartVariance &variance = player.startVariance[index];
	const UpgradeAllocation &up = player.upgrades[index];
	const double rolledAttack = base.attack * variance.attackMul;
	const double rolledDefense = base.defense * variance.defenseMul;
	const double rolledLife = base.life * variance.lifeMul;
	const double rolledEvasion = std::clamp(base.evasion + variance.evasionShift, 0.05, 0.45);
	std::ostringstream out;
	out << '{'
		<< "\"slot\":" << (index + 1)
		<< ",\"name\":\"" << escapeJson(player.roster[index]->name()) << "\""
		<< ",\"archetype\":\"" << escapeJson(archetypeName(player.roster[index]->archetype())) << "\""
		<< ",\"reference\":{" 
		<< "\"attack\":" << std::fixed << std::setprecision(2) << base.attack
		<< ",\"defense\":" << base.defense
		<< ",\"evasion\":" << std::setprecision(3) << base.evasion
		<< ",\"life\":" << std::setprecision(2) << base.life << "}"
		<< ",\"rolled\":{" 
		<< "\"attack\":" << std::setprecision(2) << rolledAttack
		<< ",\"defense\":" << rolledDefense
		<< ",\"evasion\":" << std::setprecision(3) << rolledEvasion
		<< ",\"life\":" << std::setprecision(2) << rolledLife << "}"
		<< ",\"upgrades\":{" 
		<< "\"attack\":" << up.attack
		<< ",\"defense\":" << up.defense
		<< ",\"evasion\":" << up.evasion
		<< ",\"life\":" << up.life << "}"
		<< ",\"attack\":" << std::fixed << std::setprecision(2) << eff.attack
		<< ",\"defense\":" << eff.defense
		<< ",\"evasion\":" << eff.evasion
		<< ",\"life\":" << eff.life
		<< ",\"top10\":{"
		<< "\"attack\":" << (inTopTenPercent(eff.attack, maxPossibleAttack(player, index)) ? "true" : "false")
		<< ",\"defense\":" << (inTopTenPercent(eff.defense, maxPossibleDefense(player, index)) ? "true" : "false")
		<< ",\"evasion\":" << (inTopTenPercent(eff.evasion, maxPossibleEvasion(player, index)) ? "true" : "false")
		<< ",\"life\":" << (inTopTenPercent(eff.life, maxPossibleLife(player, index)) ? "true" : "false")
		<< "}}";
	return out.str();
}

std::string formatPlayerStateJson(const Player &player) {
	std::ostringstream out;
	out << '{'
		<< "\"name\":\"" << escapeJson(player.name) << "\""
		<< ",\"level\":" << player.level
		<< ",\"xp\":" << player.xp
		<< ",\"roundsWon\":" << player.roundsWon
		<< ",\"matchPoints\":" << player.matchPoints
		<< ",\"flawlessWins\":" << player.flawlessWins
		<< ",\"unspentUpgradePoints\":" << player.unspentUpgradePoints
		<< ",\"party\":[";
	for (std::size_t i = 0; i < player.roster.size(); ++i) {
		if (i > 0) {
			out << ',';
		}
		out << formatCreatureStateJson(player, static_cast<int>(i));
	}
	out << "]}";
	return out.str();
}

void emitEventFd(int fd, const std::string &type, const std::string &payloadJson) {
	if (!g_emitEvents) {
		return;
	}
	const std::string line = "@event {\"type\":\"" + escapeJson(type) + "\"," + payloadJson + "}";
	if (!sendLine(fd, line)) {
		fatal("connection lost while sending event stream");
	}
}

void emitEventPlayer(const Player &player, const std::string &type, const std::string &payloadJson) {
	emitEventFd(player.fd, type, payloadJson);
}

void emitEventBoth(const Player &one, const Player &two, const std::string &type, const std::string &payloadJson) {
	emitEventPlayer(one, type, payloadJson);
	emitEventPlayer(two, type, payloadJson);
}

void printRoster(const Player &player) {
	std::ostringstream header;
	header << player.name << " party | level " << player.level << " | xp " << player.xp << " | unspent upgrade points " << player.unspentUpgradePoints;
	sendPlayer(player, header.str());
	for (std::size_t i = 0; i < player.roster.size(); ++i) {
		const EffectiveStats eff = effectiveStatsFor(player, static_cast<int>(i));
		const bool atkTop = inTopTenPercent(eff.attack, maxPossibleAttack(player, static_cast<int>(i)));
		const bool defTop = inTopTenPercent(eff.defense, maxPossibleDefense(player, static_cast<int>(i)));
		const bool lifeTop = inTopTenPercent(eff.life, maxPossibleLife(player, static_cast<int>(i)));
		const bool evaTop = inTopTenPercent(eff.evasion, maxPossibleEvasion(player, static_cast<int>(i)));

		std::ostringstream out;
		out << "  " << (i + 1) << ") " << player.roster[i]->name() << " [" << archetypeName(player.roster[i]->archetype()) << "]"
			<< " atk " << std::fixed << std::setprecision(1) << eff.attack << markTop10(atkTop)
			<< " def " << eff.defense << markTop10(defTop)
			<< " eva " << std::setprecision(3) << eff.evasion << markTop10(evaTop)
			<< " life " << std::setprecision(1) << eff.life << markTop10(lifeTop);
		sendPlayer(player, out.str());
	}
	emitEventPlayer(player, "party_state", "\"player\":" + formatPlayerStateJson(player));
}

void printRosterTo(const Player &target, const Player &owner) {
	std::ostringstream header;
	header << owner.name << " party | level " << owner.level << " | xp " << owner.xp << " | unspent upgrade points "
		   << owner.unspentUpgradePoints;
	sendPlayer(target, header.str());
	for (std::size_t i = 0; i < owner.roster.size(); ++i) {
		const EffectiveStats eff = effectiveStatsFor(owner, static_cast<int>(i));
		const bool atkTop = inTopTenPercent(eff.attack, maxPossibleAttack(owner, static_cast<int>(i)));
		const bool defTop = inTopTenPercent(eff.defense, maxPossibleDefense(owner, static_cast<int>(i)));
		const bool lifeTop = inTopTenPercent(eff.life, maxPossibleLife(owner, static_cast<int>(i)));
		const bool evaTop = inTopTenPercent(eff.evasion, maxPossibleEvasion(owner, static_cast<int>(i)));

		std::ostringstream out;
		out << "  " << (i + 1) << ") " << owner.roster[i]->name() << " [" << archetypeName(owner.roster[i]->archetype()) << "]"
			<< " atk " << std::fixed << std::setprecision(1) << eff.attack << markTop10(atkTop)
			<< " def " << eff.defense << markTop10(defTop)
			<< " eva " << std::setprecision(3) << eff.evasion << markTop10(evaTop)
			<< " life " << std::setprecision(1) << eff.life << markTop10(lifeTop);
		sendPlayer(target, out.str());
	}
	emitEventPlayer(target, "party_state", "\"player\":" + formatPlayerStateJson(owner));
}

void setupRoster(Player &player) {
	player.roster.reserve(kRosterSize);
	player.upgrades.reserve(kRosterSize);
	player.startVariance.reserve(kRosterSize);
	sendPlayer(player, "Choose 5 creatures. Allowed types: ranger (r), warrior (w), tank (t).");

	for (int slot = 1; slot <= kRosterSize; ++slot) {
		while (true) {
			sendPlayer(player, "Pick " + std::to_string(slot) + "/5:");
			emitEventPlayer(player, "prompt", "\"phase\":\"roster_pick\",\"slot\":" + std::to_string(slot));
			std::string input;
			if (!recvLine(player.fd, input)) {
				fatal("connection lost during roster setup for " + player.name);
			}
			auto creature = createBaseCreature(input, player.name, slot);
			if (!creature) {
				sendPlayer(player, "Invalid type. Use ranger/r, warrior/w, or tank/t.");
				continue;
			}
			player.roster.push_back(std::move(creature));
			player.upgrades.push_back(UpgradeAllocation{});
			player.startVariance.push_back(rollStartVariance());
			emitEventPlayer(player,
				"roster_pick_locked",
				"\"playerName\":\"" + escapeJson(player.name) + "\",\"slot\":" + std::to_string(slot) + ",\"creature\":"
					+ formatCreatureStateJson(player, slot - 1));
			break;
		}
		printRoster(player);
	}
}

bool parseLineup(const std::string &line, std::vector<int> &outIndices) {
	std::istringstream in(line);
	std::unordered_set<int> used;
	outIndices.clear();
	outIndices.reserve(kRoundLineupSize);

	for (int i = 0; i < kRoundLineupSize; ++i) {
		int value = 0;
		if (!(in >> value)) {
			return false;
		}
		if (value < 1 || value > kRosterSize || used.count(value) > 0) {
			return false;
		}
		used.insert(value);
		outIndices.push_back(value - 1);
	}

	int extra = 0;
	if (in >> extra) {
		return false;
	}
	return true;
}

std::vector<int> chooseLineupIndices(const Player &player, int roundNumber, const std::vector<std::string> &revealedOpponentTypes) {
	while (true) {
		sendPlayer(player, "");
		sendPlayer(player, "Round " + std::to_string(roundNumber) + ": choose lineup as 3 unique indices, e.g. '1 3 5'.");
		if (!revealedOpponentTypes.empty()) {
			sendPlayer(player, "Known enemy picks (2 revealed types): " + revealedOpponentTypes[0] + ", " + revealedOpponentTypes[1]);
		}
		printRoster(player);
		sendPlayer(player, "Enter lineup indices:");
		emitEventPlayer(player, "prompt", "\"phase\":\"lineup_pick\",\"round\":" + std::to_string(roundNumber));

		std::string line;
		if (!recvLine(player.fd, line)) {
			fatal("connection lost while reading lineup from " + player.name);
		}

		std::vector<int> picks;
		if (parseLineup(line, picks)) {
			std::ostringstream payload;
			payload << "\"playerName\":\"" << escapeJson(player.name) << "\",\"round\":" << roundNumber << ",\"lineup\":[";
			for (std::size_t i = 0; i < picks.size(); ++i) {
				if (i > 0) {
					payload << ',';
				}
				payload << (picks[i] + 1);
			}
			payload << "]";
			emitEventPlayer(player, "lineup_locked", payload.str());
			return picks;
		}
		sendPlayer(player, "Invalid lineup. Use exactly 3 unique numbers between 1 and 5.");
	}
}

std::vector<std::string> revealTwoTypes(const Player &player, const std::vector<int> &lineupIndices) {
	std::vector<int> sample = lineupIndices;
	std::shuffle(sample.begin(), sample.end(), g_rng);
	std::vector<std::string> revealed;
	revealed.reserve(2);
	for (int i = 0; i < 2; ++i) {
		revealed.push_back(archetypeName(player.roster[sample[i]]->archetype()));
	}
	return revealed;
}

double computeMaxHp(const EffectiveStats &stats, int level) {
	return stats.life * 5.2 + 32.0 + (level - 1) * 5.0;
}

double computeDamage(const Creature &attacker,
					const Creature &defender,
					const EffectiveStats &attackerStats,
					const EffectiveStats &defenderStats,
					int attackerLevel,
					int defenderLevel,
					bool attackerHasTacticalEdge,
					bool defenderHasTacticalEdge,
					int turnNumber,
					double defenderCurrentHp,
					double defenderMaxHp) {
	std::uniform_real_distribution<double> variance(0.94, 1.06);
	std::uniform_real_distribution<double> chance(0.0, 1.0);
	if (chance(g_rng) < defenderStats.evasion) {
		return 0.0;
	}

	const double matchupMultiplier = attacker.matchupMultiplierAgainst(defender);
	const double attackPower = (attackerStats.attack * 2.00 + attackerLevel * 1.6) * matchupMultiplier;
	const double defensePower = (defenderStats.defense * 1.45 + defenderLevel * 1.4);
	const double edgeOut = attackerHasTacticalEdge ? 1.06 : 1.0;
	const double edgeMitigation = defenderHasTacticalEdge ? 0.94 : 1.0;
	const double raw = (attackPower * edgeOut * variance(g_rng) - defensePower) * edgeMitigation;

	const double overtimeBonus = std::min(0.12, turnNumber * 0.003);
	const double minHit = defenderMaxHp * (0.07 + overtimeBonus);
	const double maxHit = std::max(defenderCurrentHp * 0.33, defenderMaxHp * 0.18);
	const double bounded = std::clamp(raw, minHit, maxHit);
	return std::max(0.0, bounded);
}

BoutResult resolveBout(const Player &playerOne,
				   int indexOne,
				   const Player &playerTwo,
				   int indexTwo,
				   bool firstAttackByPlayerOne,
				   bool edgeForPlayerOne,
				   bool edgeForPlayerTwo) {
	const Creature &creatureOne = *playerOne.roster[indexOne];
	const Creature &creatureTwo = *playerTwo.roster[indexTwo];
	const EffectiveStats statsOne = effectiveStatsFor(playerOne, indexOne);
	const EffectiveStats statsTwo = effectiveStatsFor(playerTwo, indexTwo);

	double hpOne = computeMaxHp(statsOne, playerOne.level);
	double hpTwo = computeMaxHp(statsTwo, playerTwo.level);
	const double maxHpOne = hpOne;
	const double maxHpTwo = hpTwo;

	bool oneTurn = firstAttackByPlayerOne;
	for (int turn = 0; turn < 512 && hpOne > 0.0 && hpTwo > 0.0; ++turn) {
		if (oneTurn) {
			const double damage = computeDamage(creatureOne, creatureTwo, statsOne, statsTwo, playerOne.level, playerTwo.level, edgeForPlayerOne,
					edgeForPlayerTwo, turn, hpTwo, maxHpTwo);
			hpTwo = std::max(0.0, hpTwo - damage);
		} else {
			const double damage = computeDamage(creatureTwo, creatureOne, statsTwo, statsOne, playerTwo.level, playerOne.level, edgeForPlayerTwo,
					edgeForPlayerOne, turn, hpOne, maxHpOne);
			hpOne = std::max(0.0, hpOne - damage);
		}
		oneTurn = !oneTurn;
	}

	if (hpOne <= 0.0 && hpTwo <= 0.0) {
		return BoutResult{0, hpOne, hpTwo};
	}
	if (hpTwo <= 0.0) {
		return BoutResult{1, hpOne, hpTwo};
	}
	if (hpOne <= 0.0) {
		return BoutResult{2, hpOne, hpTwo};
	}

	return hpOne > hpTwo ? BoutResult{1, hpOne, hpTwo} : BoutResult{2, hpOne, hpTwo};
}

void applyPlayerXp(Player &player, int gainedXp) {
	if (gainedXp <= 0) {
		return;
	}
	const int previousLevel = player.level;
	player.xp += gainedXp;
	player.level = levelFromXp(player.xp);
	if (player.level > previousLevel) {
		const int levelsGained = player.level - previousLevel;
		const int pointsGained = levelsGained * kUpgradePointsPerLevel;
		player.unspentUpgradePoints += pointsGained;
		sendPlayer(player, "Level up to " + std::to_string(player.level) + ". You gained " + std::to_string(pointsGained)
					  + " upgrade points.");
		emitEventPlayer(player,
			"level_up",
			"\"playerName\":\"" + escapeJson(player.name) + "\",\"level\":" + std::to_string(player.level)
				+ ",\"xp\":" + std::to_string(player.xp) + ",\"pointsGained\":" + std::to_string(pointsGained));
	}
}

void allocateUpgradePoints(Player &player) {
	if (player.unspentUpgradePoints <= 0) {
		return;
	}

	sendPlayer(player, "");
	sendPlayer(player, "Upgrade phase: you have " + std::to_string(player.unspentUpgradePoints) + " unspent points.");
	sendPlayer(player, "Command format: <creature_index> <stat> <points>");
	sendPlayer(player, "Stats: atk, def, life, eva. Type 'done' to finish.");
	emitEventPlayer(player, "upgrade_phase_started", "\"playerName\":\"" + escapeJson(player.name) + "\",\"unspentPoints\":"
		+ std::to_string(player.unspentUpgradePoints));

	while (player.unspentUpgradePoints > 0) {
		printRoster(player);
		sendPlayer(player, "Spend points or type 'done':");
		emitEventPlayer(player, "prompt", "\"phase\":\"upgrade\",\"unspentPoints\":" + std::to_string(player.unspentUpgradePoints));

		std::string line;
		if (!recvLine(player.fd, line)) {
			fatal("connection lost while allocating upgrades for " + player.name);
		}

		if (normalize(line) == "done") {
			emitEventPlayer(player, "upgrade_phase_finished", "\"playerName\":\"" + escapeJson(player.name) + "\",\"unspentPoints\":"
				+ std::to_string(player.unspentUpgradePoints));
			break;
		}

		std::istringstream in(line);
		int idx = 0;
		std::string stat;
		int points = 0;
		if (!(in >> idx >> stat >> points)) {
			sendPlayer(player, "Invalid command. Example: 3 atk 2");
			continue;
		}
		if (idx < 1 || idx > kRosterSize) {
			sendPlayer(player, "Creature index must be 1..5.");
			continue;
		}
		if (points <= 0 || points > player.unspentUpgradePoints) {
			sendPlayer(player, "Points must be positive and <= your unspent points.");
			continue;
		}
		stat = normalize(stat);
		UpgradeAllocation &up = player.upgrades[idx - 1];
		if (stat == "atk") {
			up.attack += points;
		} else if (stat == "def") {
			up.defense += points;
		} else if (stat == "life") {
			up.life += points;
		} else if (stat == "eva") {
			up.evasion += points;
		} else {
			sendPlayer(player, "Unknown stat. Use atk, def, life, eva.");
			continue;
		}
		player.unspentUpgradePoints -= points;
		sendPlayer(player, "Upgrade applied. Remaining points: " + std::to_string(player.unspentUpgradePoints));
		emitEventPlayer(player,
			"upgrade_applied",
			"\"playerName\":\"" + escapeJson(player.name) + "\",\"creatureIndex\":" + std::to_string(idx)
				+ ",\"stat\":\"" + escapeJson(stat) + "\",\"points\":" + std::to_string(points) + ",\"unspentPoints\":"
				+ std::to_string(player.unspentUpgradePoints));
	}

	if (player.unspentUpgradePoints > 0) {
		sendPlayer(player, "You kept " + std::to_string(player.unspentUpgradePoints) + " points for later.");
	}
}

RoundResult runRound(Player &playerOne, Player &playerTwo, int roundNumber, int firstPicker, int edgePlayer) {
	Player &picker = firstPicker == 1 ? playerOne : playerTwo;
	Player &responder = firstPicker == 1 ? playerTwo : playerOne;
	const int responderId = firstPicker == 1 ? 2 : 1;
	const bool loserEdgeActive = edgePlayer == responderId;

	sendBoth(playerOne, playerTwo, "");
	sendBoth(playerOne, playerTwo, "=== Round " + std::to_string(roundNumber) + " ===");
	sendBoth(playerOne, playerTwo, picker.name + " picks first and attacks first in each duel.");
	emitEventBoth(playerOne,
		playerTwo,
		"round_started",
		"\"round\":" + std::to_string(roundNumber) + ",\"firstPicker\":\"" + escapeJson(picker.name) + "\",\"loserEdgeActive\":"
			+ std::string(loserEdgeActive ? "true" : "false"));
	if (loserEdgeActive) {
		sendBoth(playerOne, playerTwo, responder.name + " gets loser advantage this round.");
	}

	sendPlayer(responder, "Waiting while opponent locks lineup...");
	const std::vector<int> pickerLineup = chooseLineupIndices(picker, roundNumber, {});

	std::vector<std::string> revealed;
	if (loserEdgeActive) {
		revealed = revealTwoTypes(picker, pickerLineup);
		sendPlayer(responder, "Revealed enemy types: " + revealed[0] + ", " + revealed[1]);
		emitEventPlayer(responder,
			"lineup_reveal",
			"\"round\":" + std::to_string(roundNumber) + ",\"opponentName\":\"" + escapeJson(picker.name)
				+ "\",\"revealedTypes\":[\"" + escapeJson(revealed[0]) + "\",\"" + escapeJson(revealed[1]) + "\"]");
	}

	sendPlayer(picker, "Waiting while opponent locks lineup...");
	const std::vector<int> responderLineup = chooseLineupIndices(responder, roundNumber, revealed);

	const std::vector<int> &lineupOne = firstPicker == 1 ? pickerLineup : responderLineup;
	const std::vector<int> &lineupTwo = firstPicker == 1 ? responderLineup : pickerLineup;

	int winsOne = 0;
	int winsTwo = 0;
	int xpGainOne = kXpParticipation * kRoundLineupSize;
	int xpGainTwo = kXpParticipation * kRoundLineupSize;

	const bool firstAttackByPlayerOne = firstPicker == 1;
	const bool edgeForPlayerOne = edgePlayer == 1;
	const bool edgeForPlayerTwo = edgePlayer == 2;

	for (int duel = 0; duel < kRoundLineupSize; ++duel) {
		const int idxOne = lineupOne[duel];
		const int idxTwo = lineupTwo[duel];
		emitEventBoth(playerOne,
			playerTwo,
			"duel_started",
			"\"round\":" + std::to_string(roundNumber) + ",\"duel\":" + std::to_string(duel + 1) + ",\"playerOne\":"
				+ formatCreatureStateJson(playerOne, idxOne) + ",\"playerTwo\":" + formatCreatureStateJson(playerTwo, idxTwo));
		const BoutResult bout = resolveBout(playerOne, idxOne, playerTwo, idxTwo, firstAttackByPlayerOne, edgeForPlayerOne, edgeForPlayerTwo);

		std::ostringstream title;
		title << "Duel " << (duel + 1) << ": " << playerOne.roster[idxOne]->name() << " ("
			  << archetypeName(playerOne.roster[idxOne]->archetype()) << ") vs " << playerTwo.roster[idxTwo]->name() << " ("
			  << archetypeName(playerTwo.roster[idxTwo]->archetype()) << ")";
		sendBoth(playerOne, playerTwo, "");
		sendBoth(playerOne, playerTwo, title.str());

		std::ostringstream hp;
		hp << std::fixed << std::setprecision(1) << "Remaining HP => " << playerOne.name << ": " << bout.hpOne << " | " << playerTwo.name << ": "
		   << bout.hpTwo;
		sendBoth(playerOne, playerTwo, hp.str());
		std::ostringstream duelResultPayload;
		duelResultPayload << "\"round\":" << roundNumber << ",\"duel\":" << (duel + 1) << ",\"winner\":" << bout.winner
			<< ",\"hpOne\":" << std::fixed << std::setprecision(2) << bout.hpOne << ",\"hpTwo\":" << bout.hpTwo;
		emitEventBoth(playerOne, playerTwo, "duel_resolved", duelResultPayload.str());

		if (bout.winner == 1) {
			++winsOne;
			++playerOne.matchPoints;
			xpGainOne += kXpDuelWin;
			sendBoth(playerOne, playerTwo, "Duel winner: " + playerOne.name);
		} else if (bout.winner == 2) {
			++winsTwo;
			++playerTwo.matchPoints;
			xpGainTwo += kXpDuelWin;
			sendBoth(playerOne, playerTwo, "Duel winner: " + playerTwo.name);
		} else {
			sendBoth(playerOne, playerTwo, "Duel result: draw");
		}
	}

	if (winsOne > winsTwo) {
		++playerOne.roundsWon;
		++playerOne.matchPoints;
		xpGainOne += kXpRoundWin;
		if (winsOne == kRoundLineupSize) {
			++playerOne.flawlessWins;
			playerOne.matchPoints += 2;
			xpGainOne += kXpFlawlessBonus;
			sendBoth(playerOne, playerTwo, "Flawless round bonus for " + playerOne.name + ": +" + std::to_string(kXpFlawlessBonus) + " xp.");
		}
	} else if (winsTwo > winsOne) {
		++playerTwo.roundsWon;
		++playerTwo.matchPoints;
		xpGainTwo += kXpRoundWin;
		if (winsTwo == kRoundLineupSize) {
			++playerTwo.flawlessWins;
			playerTwo.matchPoints += 2;
			xpGainTwo += kXpFlawlessBonus;
			sendBoth(playerOne, playerTwo, "Flawless round bonus for " + playerTwo.name + ": +" + std::to_string(kXpFlawlessBonus) + " xp.");
		}
	}

	applyPlayerXp(playerOne, xpGainOne);
	applyPlayerXp(playerTwo, xpGainTwo);
	emitEventBoth(playerOne,
		playerTwo,
		"xp_awarded",
		"\"round\":" + std::to_string(roundNumber) + ",\"playerOne\":{\"name\":\"" + escapeJson(playerOne.name) + "\",\"xpGained\":"
			+ std::to_string(xpGainOne) + ",\"level\":" + std::to_string(playerOne.level) + "},\"playerTwo\":{\"name\":\""
			+ escapeJson(playerTwo.name) + "\",\"xpGained\":" + std::to_string(xpGainTwo) + ",\"level\":"
			+ std::to_string(playerTwo.level) + "}}");

	std::ostringstream score;
	score << "Round " << roundNumber << " score: " << playerOne.name << " " << winsOne << " - " << winsTwo << " " << playerTwo.name;
	sendBoth(playerOne, playerTwo, score.str());
	sendBoth(playerOne, playerTwo, playerOne.name + " gained " + std::to_string(xpGainOne) + " xp this round.");
	sendBoth(playerOne, playerTwo, playerTwo.name + " gained " + std::to_string(xpGainTwo) + " xp this round.");
	sendBoth(playerOne, playerTwo, playerOne.name + " points: " + std::to_string(playerOne.matchPoints) + " | flawless wins: " + std::to_string(playerOne.flawlessWins));
	sendBoth(playerOne, playerTwo, playerTwo.name + " points: " + std::to_string(playerTwo.matchPoints) + " | flawless wins: " + std::to_string(playerTwo.flawlessWins));

	if (winsOne > winsTwo) {
		sendBoth(playerOne, playerTwo, "Round winner: " + playerOne.name);
	} else if (winsTwo > winsOne) {
		sendBoth(playerOne, playerTwo, "Round winner: " + playerTwo.name);
	} else {
		sendBoth(playerOne, playerTwo, "Round result: draw");
	}
	emitEventBoth(playerOne,
		playerTwo,
		"round_resolved",
		"\"round\":" + std::to_string(roundNumber) + ",\"winner\":"
			+ std::to_string(winsOne > winsTwo ? 1 : (winsTwo > winsOne ? 2 : 0)) + ",\"scoreOne\":" + std::to_string(winsOne)
			+ ",\"scoreTwo\":" + std::to_string(winsTwo) + ",\"playerOne\":" + formatPlayerStateJson(playerOne) + ",\"playerTwo\":"
			+ formatPlayerStateJson(playerTwo));

	printRoster(playerOne);
	printRoster(playerTwo);

	return RoundResult{winsOne > winsTwo ? 1 : (winsTwo > winsOne ? 2 : 0), winsOne, winsTwo};
}

void sendProgression(const Player &target, const Player &owner) {
	sendPlayer(target, "");
	sendPlayer(target, owner.name + " final progression:");
	sendPlayer(target,
		"  level " + std::to_string(owner.level) + " | xp " + std::to_string(owner.xp) + " | unspent upgrade points "
			+ std::to_string(owner.unspentUpgradePoints));
	printRosterTo(target, owner);
}

int makeListeningSocket(int port) {
	const int serverFd = socket(AF_INET, SOCK_STREAM, 0);
	if (serverFd < 0) {
		fatal("socket failed");
	}

	int opt = 1;
	if (setsockopt(serverFd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
		fatal("setsockopt failed");
	}

	sockaddr_in addr;
	std::memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
	addr.sin_port = htons(static_cast<uint16_t>(port));

	if (bind(serverFd, reinterpret_cast<const sockaddr *>(&addr), sizeof(addr)) < 0) {
		fatal("bind failed");
	}
	if (listen(serverFd, 10) < 0) {
		fatal("listen failed");
	}
	return serverFd;
}

Player acceptPlayer(int serverFd, int number) {
	const int fd = accept(serverFd, nullptr, nullptr);
	if (fd < 0) {
		fatal("accept failed");
	}

	Player player;
	player.fd = fd;
	player.name = "Player" + std::to_string(number);
	if (!sendLine(fd, "Connected to Heritage duel server.")) {
		fatal("failed to greet player");
	}
	if (!sendLine(fd, "Enter your display name:")) {
		fatal("failed to request player name");
	}

	std::string name;
	if (!recvLine(fd, name)) {
		fatal("connection lost while reading player name");
	}
	if (name.empty()) {
		name = "Player" + std::to_string(number);
	}
	player.name = name;
	if (!sendLine(fd, "Welcome, " + player.name + ". Waiting for opponent...")) {
		fatal("failed to send welcome");
	}
	emitEventFd(fd, "player_connected", "\"playerName\":\"" + escapeJson(player.name) + "\",\"playerNumber\":" + std::to_string(number));
	return player;
}

} // namespace

int main(int argc, char **argv) {
	if (argc != 2 && argc != 3) {
		std::cerr << "Usage: ./network_duel_server <port> [--text-only]\n";
		return 1;
	}
	if (argc == 3) {
		if (std::string(argv[2]) != "--text-only") {
			std::cerr << "Unknown option: " << argv[2] << "\n";
			return 1;
		}
		g_emitEvents = false;
	}

	const int port = std::atoi(argv[1]);
	if (port <= 0 || port > 65535) {
		std::cerr << "Invalid port\n";
		return 1;
	}

	const int serverFd = makeListeningSocket(port);
	std::cout << "Network duel server listening on 127.0.0.1:" << port << '\n';
	std::cout << "Waiting for two players...\n";

	Player playerOne = acceptPlayer(serverFd, 1);
	Player playerTwo = acceptPlayer(serverFd, 2);
	MatchConfig matchConfig = chooseMatchConfig(playerOne, playerTwo);

	sendBoth(playerOne, playerTwo, "Both players connected. Starting network duel.");
	sendBoth(playerOne, playerTwo, "Selected mode: " + matchModeLabel(matchConfig.mode));
	sendBoth(playerOne, playerTwo, "Upgrade economy: start with 13 points, +5 points per level, max level 9.");
	sendBoth(playerOne, playerTwo, "TOP10 marker means value is in top 10% of that creature's max achievable value.");
	if (g_emitEvents) {
		sendBoth(playerOne, playerTwo, "Event stream enabled. Structured lines are prefixed with @event.");
		emitEventBoth(playerOne,
			playerTwo,
			"match_started",
			"\"mode\":\"classic_duel\",\"matchLength\":\"" + matchModeKey(matchConfig.mode) + "\",\"maxRounds\":" + std::to_string(matchConfig.maxRounds) + ",\"playerOneName\":\""
				+ escapeJson(playerOne.name) + "\",\"playerTwoName\":\"" + escapeJson(playerTwo.name) + "\"");
	}

	sendPlayer(playerOne, "\n=== " + playerOne.name + " roster setup ===");
	setupRoster(playerOne);
	allocateUpgradePoints(playerOne);

	sendPlayer(playerTwo, "\n=== " + playerTwo.name + " roster setup ===");
	setupRoster(playerTwo);
	allocateUpgradePoints(playerTwo);

	std::uniform_int_distribution<int> firstPickRandom(1, 2);
	int firstPicker = firstPickRandom(g_rng);
	int previousRoundWinner = 0;
	sendBoth(playerOne, playerTwo, "Random first draw for round 1: " + (firstPicker == 1 ? playerOne.name : playerTwo.name));
	emitEventBoth(playerOne,
		playerTwo,
		"first_picker_selected",
		"\"playerName\":\"" + escapeJson(firstPicker == 1 ? playerOne.name : playerTwo.name) + "\",\"round\":1");

	for (int round = 1; round <= matchConfig.maxRounds; ++round) {
		const int edgePlayer = previousRoundWinner == 0 ? 0 : (previousRoundWinner == 1 ? 2 : 1);
		const RoundResult result = runRound(playerOne, playerTwo, round, firstPicker, edgePlayer);

		if (result.winner == 1 || result.winner == 2) {
			firstPicker = result.winner;
			previousRoundWinner = result.winner;
		} else {
			previousRoundWinner = 0;
		}

		allocateUpgradePoints(playerOne);
		allocateUpgradePoints(playerTwo);

		if (isMaxMode(matchConfig)) {
			if (playerOne.flawlessWins >= 3 || playerTwo.flawlessWins >= 3) {
				sendBoth(playerOne, playerTwo, "Max mode ended: a player reached 3 flawless wins.");
				break;
			}
			if (bothPlayersAtLevelCap(playerOne, playerTwo)) {
				sendBoth(playerOne, playerTwo, "Max mode ended: both players reached the level cap. Final result will be decided by points.");
				break;
			}
		} else if (playerOne.roundsWon >= matchConfig.targetRoundWins || playerTwo.roundsWon >= matchConfig.targetRoundWins) {
			break;
		}
	}

	sendBoth(playerOne, playerTwo, "\n=== Final Match Result ===");
	sendBoth(playerOne, playerTwo, playerOne.name + " rounds won: " + std::to_string(playerOne.roundsWon));
	sendBoth(playerOne, playerTwo, playerTwo.name + " rounds won: " + std::to_string(playerTwo.roundsWon));
	sendBoth(playerOne, playerTwo, playerOne.name + " points: " + std::to_string(playerOne.matchPoints) + " | flawless wins: " + std::to_string(playerOne.flawlessWins));
	sendBoth(playerOne, playerTwo, playerTwo.name + " points: " + std::to_string(playerTwo.matchPoints) + " | flawless wins: " + std::to_string(playerTwo.flawlessWins));
	int finalWinner = 0;
	if (isMaxMode(matchConfig) && bothPlayersAtLevelCap(playerOne, playerTwo) && playerOne.flawlessWins < 3 && playerTwo.flawlessWins < 3) {
		if (playerOne.matchPoints > playerTwo.matchPoints) {
			finalWinner = 1;
		} else if (playerTwo.matchPoints > playerOne.matchPoints) {
			finalWinner = 2;
		}
	} else if (playerOne.roundsWon > playerTwo.roundsWon) {
		finalWinner = 1;
	} else if (playerTwo.roundsWon > playerOne.roundsWon) {
		finalWinner = 2;
	} else if (playerOne.matchPoints > playerTwo.matchPoints) {
		finalWinner = 1;
	} else if (playerTwo.matchPoints > playerOne.matchPoints) {
		finalWinner = 2;
	}

	if (finalWinner == 1) {
		sendBoth(playerOne, playerTwo, "Match winner: " + playerOne.name);
	} else if (finalWinner == 2) {
		sendBoth(playerOne, playerTwo, "Match winner: " + playerTwo.name);
	} else {
		sendBoth(playerOne, playerTwo, "Match result: draw");
	}
	emitEventBoth(playerOne,
		playerTwo,
		"match_resolved",
		"\"playerOne\":" + formatPlayerStateJson(playerOne) + ",\"playerTwo\":" + formatPlayerStateJson(playerTwo)
			+ ",\"winner\":" + std::to_string(finalWinner) + ",\"matchLength\":\"" + matchModeKey(matchConfig.mode) + "\"");

	sendProgression(playerOne, playerOne);
	sendProgression(playerOne, playerTwo);
	sendProgression(playerTwo, playerOne);
	sendProgression(playerTwo, playerTwo);

	close(playerOne.fd);
	close(playerTwo.fd);
	close(serverFd);
	return 0;
}
