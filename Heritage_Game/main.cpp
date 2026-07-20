#include "Creature.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <random>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

namespace {

constexpr int kRosterSize = 5;
constexpr int kRoundLineupSize = 3;
constexpr int kMaxRounds = 3;

constexpr int kXpParticipation = 40;
constexpr int kXpDuelWin = 30;
constexpr int kXpRoundWin = 25;
constexpr int kXpFlawlessBonus = 35;

constexpr std::array<int, 4> kXpThresholds = {0, 100, 220, 380};
constexpr int kMaxLevel = 4;

struct CreatureProgress {
	int xp = 0;
	int level = 1;
};

struct Player {
	std::string name;
	std::vector<std::unique_ptr<Creature>> roster;
	std::vector<CreatureProgress> progression;
	int roundsWon = 0;
};

struct BoutResult {
	int winner; // 0 draw, 1 player one, 2 player two
	double scoreOne;
	double scoreTwo;
};

struct RoundResult {
	int winner; // 0 draw, 1 player one, 2 player two
	int boutWinsOne;
	int boutWinsTwo;
};

thread_local std::mt19937 g_rng(std::random_device{}());

std::string normalize(std::string value) {
	std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return value;
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

double levelMultiplier(int level) {
	return 1.0 + (level - 1) * 0.05;
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

void printRoster(const Player &player) {
	std::cout << player.name << " roster:\n";
	for (std::size_t i = 0; i < player.roster.size(); ++i) {
		std::cout << "  " << (i + 1) << ") " << player.roster[i]->summary() << " | level " << player.progression[i].level << " | xp "
				  << player.progression[i].xp << '\n';
	}
}

std::vector<int> chooseLineupIndices(const Player &player, int roundNumber, const std::vector<std::string> &revealedOpponentTypes) {
	std::vector<int> picks;
	picks.reserve(kRoundLineupSize);
	std::unordered_set<int> used;

	std::cout << "\n" << player.name << " choose your lineup for round " << roundNumber << " (" << kRoundLineupSize
			  << " unique numbers from your " << kRosterSize << " creatures).\n";
	if (!revealedOpponentTypes.empty()) {
		std::cout << "Known enemy picks (2 revealed types): " << revealedOpponentTypes[0] << ", " << revealedOpponentTypes[1] << "\n";
	}
	printRoster(player);

	while (picks.size() < kRoundLineupSize) {
		std::cout << "Pick slot " << (picks.size() + 1) << " index [1-5]: ";
		int value = 0;
		if (!(std::cin >> value)) {
			std::cin.clear();
			std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
			std::cout << "Invalid number. Try again.\n";
			continue;
		}
		if (value < 1 || value > kRosterSize) {
			std::cout << "Please choose a number between 1 and " << kRosterSize << ".\n";
			continue;
		}
		if (used.count(value) > 0) {
			std::cout << "That creature is already in your lineup this round.\n";
			continue;
		}
		used.insert(value);
		picks.push_back(value - 1);
	}

	return picks;
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

double computeMaxHp(const Creature &creature, int level) {
	return creature.stats().life * 5.5 + 35.0 + (level - 1) * 7.0;
}

double computeDamage(const Creature &attacker,
					const Creature &defender,
					int attackerLevel,
					int defenderLevel,
					bool attackerHasTacticalEdge,
					bool defenderHasTacticalEdge,
					int turnNumber,
					double defenderCurrentHp,
					double defenderMaxHp) {
	std::uniform_real_distribution<double> variance(0.94, 1.06);
	std::uniform_real_distribution<double> chance(0.0, 1.0);
	const double evasionChance = std::clamp(defender.stats().evasion + (defenderLevel - 1) * 0.01, 0.05, 0.35);
	if (chance(g_rng) < evasionChance) {
		return 0.0;
	}

	const double matchupMultiplier = attacker.matchupMultiplierAgainst(defender);
	const double attackPower = (attacker.stats().attack * 2.05 + attackerLevel * 1.8) * levelMultiplier(attackerLevel) * matchupMultiplier;
	const double defensePower = (defender.stats().defense * 1.45 + defenderLevel * 1.5) * levelMultiplier(defenderLevel);
	const double edgeOut = attackerHasTacticalEdge ? 1.06 : 1.0;
	const double edgeMitigation = defenderHasTacticalEdge ? 0.94 : 1.0;

	const double raw = (attackPower * edgeOut * variance(g_rng) - defensePower) * edgeMitigation;
	const double overtimeBonus = std::min(0.12, turnNumber * 0.003);
	const double minHit = defenderMaxHp * (0.07 + overtimeBonus);
	const double maxHit = std::max(defenderCurrentHp * 0.33, defenderMaxHp * 0.18);
	const double bounded = std::clamp(raw, minHit, maxHit);
	return std::max(0.0, bounded);
}

void applyXpGains(Player &player, const std::vector<int> &gains) {
	for (std::size_t i = 0; i < gains.size(); ++i) {
		if (gains[i] <= 0) {
			continue;
		}
		CreatureProgress &progress = player.progression[i];
		const int previousLevel = progress.level;
		progress.xp += gains[i];
		progress.level = levelFromXp(progress.xp);
		if (progress.level > previousLevel) {
			std::cout << player.name << " level up: " << player.roster[i]->name() << " reached level " << progress.level << " (xp " << progress.xp
					  << ")\n";
		}
	}
}

void printXpSummary(const Player &player, const std::vector<int> &gains) {
	std::cout << "\n" << player.name << " XP gains this round:\n";
	for (std::size_t i = 0; i < gains.size(); ++i) {
		if (gains[i] <= 0) {
			continue;
		}
		std::cout << "  " << player.roster[i]->name() << ": +" << gains[i] << " xp (total " << player.progression[i].xp << ", level "
				  << player.progression[i].level << ")\n";
	}
}

BoutResult resolveBout(const Creature &creatureOne,
					   const Creature &creatureTwo,
					   int levelOne,
					   int levelTwo,
					   bool firstAttackByPlayerOne,
					   bool edgeForPlayerOne,
					   bool edgeForPlayerTwo) {
	double hpOne = computeMaxHp(creatureOne, levelOne);
	double hpTwo = computeMaxHp(creatureTwo, levelTwo);
	const double maxHpOne = hpOne;
	const double maxHpTwo = hpTwo;

	bool oneTurn = firstAttackByPlayerOne;
	for (int turn = 0; turn < 512 && hpOne > 0.0 && hpTwo > 0.0; ++turn) {
		if (oneTurn) {
			const double damage = computeDamage(creatureOne, creatureTwo, levelOne, levelTwo, edgeForPlayerOne, edgeForPlayerTwo, turn, hpTwo, maxHpTwo);
			hpTwo = std::max(0.0, hpTwo - damage);
		} else {
			const double damage = computeDamage(creatureTwo, creatureOne, levelTwo, levelOne, edgeForPlayerTwo, edgeForPlayerOne, turn, hpOne, maxHpOne);
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

RoundResult runRound(Player &playerOne, Player &playerTwo, int roundNumber, int firstPicker, int edgePlayer) {
	Player &picker = firstPicker == 1 ? playerOne : playerTwo;
	Player &responder = firstPicker == 1 ? playerTwo : playerOne;
	const int responderId = firstPicker == 1 ? 2 : 1;
	const bool loserEdgeActive = edgePlayer == responderId;

	std::cout << "\n=== Round " << roundNumber << " ===\n";
	std::cout << picker.name << " draws first this round.\n";
	std::cout << picker.name << " also attacks first in each duel this round.\n";
	if (loserEdgeActive) {
		std::cout << responder.name << " gets loser advantage this round: sees 2 picks and gains slight combat edge.\n";
	}

	const std::vector<int> pickerLineup = chooseLineupIndices(picker, roundNumber, {});
	std::vector<std::string> revealed;
	if (loserEdgeActive) {
		revealed = revealTwoTypes(picker, pickerLineup);
		std::cout << responder.name << " is informed that " << picker.name << " picked: " << revealed[0] << " and " << revealed[1] << "\n";
	}
	const std::vector<int> responderLineup = chooseLineupIndices(responder, roundNumber, revealed);

	const std::vector<int> &lineupOne = firstPicker == 1 ? pickerLineup : responderLineup;
	const std::vector<int> &lineupTwo = firstPicker == 1 ? responderLineup : pickerLineup;
	std::vector<int> xpOne(kRosterSize, 0);
	std::vector<int> xpTwo(kRosterSize, 0);
	for (int idx : lineupOne) {
		xpOne[idx] += kXpParticipation;
	}
	for (int idx : lineupTwo) {
		xpTwo[idx] += kXpParticipation;
	}

	int winsOne = 0;
	int winsTwo = 0;
	const bool firstAttackByPlayerOne = firstPicker == 1;
	const bool edgeForPlayerOne = edgePlayer == 1;
	const bool edgeForPlayerTwo = edgePlayer == 2;

	for (int duel = 0; duel < kRoundLineupSize; ++duel) {
		const Creature &creatureOne = *playerOne.roster[lineupOne[duel]];
		const Creature &creatureTwo = *playerTwo.roster[lineupTwo[duel]];
		const int levelOne = playerOne.progression[lineupOne[duel]].level;
		const int levelTwo = playerTwo.progression[lineupTwo[duel]].level;
		const BoutResult bout = resolveBout(creatureOne, creatureTwo, levelOne, levelTwo, firstAttackByPlayerOne, edgeForPlayerOne, edgeForPlayerTwo);

		std::cout << "\nDuel " << (duel + 1) << ": " << creatureOne.name() << " (" << archetypeName(creatureOne.archetype()) << ")"
				  << " vs " << creatureTwo.name() << " (" << archetypeName(creatureTwo.archetype()) << ")\n";
		std::cout << "Remaining HP => " << playerOne.name << ": " << std::fixed << std::setprecision(1) << bout.scoreOne << " | " << playerTwo.name
				  << ": " << bout.scoreTwo << '\n';

		if (bout.winner == 1) {
			++winsOne;
			xpOne[lineupOne[duel]] += kXpDuelWin;
			std::cout << "Duel winner: " << playerOne.name << '\n';
		} else if (bout.winner == 2) {
			++winsTwo;
			xpTwo[lineupTwo[duel]] += kXpDuelWin;
			std::cout << "Duel winner: " << playerTwo.name << '\n';
		} else {
			std::cout << "Duel result: draw\n";
		}
	}

	std::cout << "\nRound " << roundNumber << " score: " << playerOne.name << " " << winsOne << " - " << winsTwo << " " << playerTwo.name << '\n';
	if (winsOne > winsTwo) {
		++playerOne.roundsWon;
		for (int idx : lineupOne) {
			xpOne[idx] += kXpRoundWin;
		}
		if (winsOne == kRoundLineupSize) {
			for (int idx : lineupOne) {
				xpOne[idx] += kXpFlawlessBonus;
			}
			std::cout << "Flawless round bonus for " << playerOne.name << " (+" << kXpFlawlessBonus << " xp to round lineup).\n";
		}

		applyXpGains(playerOne, xpOne);
		applyXpGains(playerTwo, xpTwo);
		printXpSummary(playerOne, xpOne);
		printXpSummary(playerTwo, xpTwo);
		std::cout << "Round winner: " << playerOne.name << '\n';
		return RoundResult{1, winsOne, winsTwo};
	}
	if (winsTwo > winsOne) {
		++playerTwo.roundsWon;
		for (int idx : lineupTwo) {
			xpTwo[idx] += kXpRoundWin;
		}
		if (winsTwo == kRoundLineupSize) {
			for (int idx : lineupTwo) {
				xpTwo[idx] += kXpFlawlessBonus;
			}
			std::cout << "Flawless round bonus for " << playerTwo.name << " (+" << kXpFlawlessBonus << " xp to round lineup).\n";
		}

		applyXpGains(playerOne, xpOne);
		applyXpGains(playerTwo, xpTwo);
		printXpSummary(playerOne, xpOne);
		printXpSummary(playerTwo, xpTwo);
		std::cout << "Round winner: " << playerTwo.name << '\n';
		return RoundResult{2, winsOne, winsTwo};
	}

	applyXpGains(playerOne, xpOne);
	applyXpGains(playerTwo, xpTwo);
	printXpSummary(playerOne, xpOne);
	printXpSummary(playerTwo, xpTwo);
	std::cout << "Round result: draw\n";
	return RoundResult{0, winsOne, winsTwo};
}

Player buildPlayer(const std::string &name) {
	Player player;
	player.name = name;
	player.roster.reserve(kRosterSize);
	player.progression.reserve(kRosterSize);

	std::cout << "\n=== " << player.name << " roster setup ===\n";
	std::cout << "Choose " << kRosterSize << " creatures. Allowed types: ranger (r), warrior (w), tank (t).\n";

	for (int slot = 1; slot <= kRosterSize; ++slot) {
		while (true) {
			std::cout << player.name << " pick " << slot << "/" << kRosterSize << ": ";
			std::string input;
			if (!(std::cin >> input)) {
				std::cerr << "Input error. Exiting.\n";
				std::exit(1);
			}

			auto creature = createBaseCreature(input, player.name, slot);
			if (!creature) {
				std::cout << "Invalid type. Use ranger/r, warrior/w, or tank/t.\n";
				continue;
			}
			player.roster.push_back(std::move(creature));
			player.progression.push_back(CreatureProgress{});
			break;
		}
	}

	return player;
}

void printPlayerProgress(const Player &player) {
	std::cout << "\n" << player.name << " progression:\n";
	for (std::size_t i = 0; i < player.roster.size(); ++i) {
		std::cout << "  " << player.roster[i]->name() << " -> level " << player.progression[i].level << " | xp " << player.progression[i].xp << '\n';
	}
}

} // namespace

int main() {
	std::cout << "=== Heritage Game: Classic Duel (2 Players) ===\n";
	std::cout << "Both players create a 5-creature roster, then play up to 3 rounds of 3v3.\n";
	std::cout << "Balance goals: winner gets first attack, loser gets tactical information advantage.\n";

	Player playerOne = buildPlayer("Player1");
	Player playerTwo = buildPlayer("Player2");

	std::uniform_int_distribution<int> firstPickRandom(1, 2);
	int firstPicker = firstPickRandom(g_rng);
	int previousRoundWinner = 0;
	std::cout << "\nRandom first draw for round 1: " << (firstPicker == 1 ? playerOne.name : playerTwo.name) << "\n";

	for (int round = 1; round <= kMaxRounds; ++round) {
		const int edgePlayer = previousRoundWinner == 0 ? 0 : (previousRoundWinner == 1 ? 2 : 1);
		const RoundResult result = runRound(playerOne, playerTwo, round, firstPicker, edgePlayer);
		if (result.winner == 1 || result.winner == 2) {
			firstPicker = result.winner;
			previousRoundWinner = result.winner;
		} else {
			previousRoundWinner = 0;
		}

		if (playerOne.roundsWon == 2 || playerTwo.roundsWon == 2) {
			break;
		}
	}

	std::cout << "\n=== Final Match Result ===\n";
	std::cout << playerOne.name << " rounds won: " << playerOne.roundsWon << '\n';
	std::cout << playerTwo.name << " rounds won: " << playerTwo.roundsWon << '\n';

	if (playerOne.roundsWon > playerTwo.roundsWon) {
		std::cout << "Match winner: " << playerOne.name << '\n';
	} else if (playerTwo.roundsWon > playerOne.roundsWon) {
		std::cout << "Match winner: " << playerTwo.name << '\n';
	} else {
		std::cout << "Match result: draw\n";
	}

	printPlayerProgress(playerOne);
	printPlayerProgress(playerTwo);

	std::cout << "Rule reminder: round 1 first draw is random; later first draws go to the previous round winner.\n";
	std::cout << "Loser advantage: the previous round loser sees 2 picked opponent types and gets a small tactical combat edge.\n";
	std::cout << "XP note: participation grants xp, duel and round wins grant more, flawless rounds add bonus xp needed for max level.\n";

	return 0;
}