#include "Creature.hpp"

#include <iomanip>
#include <iostream>
#include <memory>

namespace {

void printFight(const Creature &left, const Creature &right) {
	const double leftScore = left.combatScoreAgainst(right);
	const double rightScore = right.combatScoreAgainst(left);

	std::cout << left << '\n';
	std::cout << right << '\n';
	std::cout << left.name() << " score: " << leftScore << '\n';
	std::cout << right.name() << " score: " << rightScore << '\n';
	if (leftScore > rightScore) {
		std::cout << "Winner: " << left.name() << "\n";
	} else if (rightScore > leftScore) {
		std::cout << "Winner: " << right.name() << "\n";
	} else {
		std::cout << "Winner: draw\n";
	}
}

void printUpgradeResult(const std::string &label, const UpgradeAttempt &attempt) {
	std::cout << label << " | chance " << std::fixed << std::setprecision(2) << (attempt.successChance * 100.0) << "% | "
			  << (attempt.success ? "success" : "fail") << '\n';
}

} // namespace

int main() {
	Ranger ranger("Aster", 4);
	Tank tank("Boulder", 4);
	Warrior warrior("Blade", 4);

	auto rangerTank = ranger.crossEvolve(tank);
	auto rangerWarrior = ranger.crossEvolve(warrior);
	auto eliteRanger = ranger.eliteEvolve();
	auto eliteWarrior = warrior.eliteEvolve();

	std::cout << "Base creatures\n";
	std::cout << ranger << '\n';
	std::cout << tank << '\n';
	std::cout << warrior << '\n';

	std::cout << "\nTier 5 specialization choices\n";
	if (rangerTank) {
		std::cout << "Hybrid option: " << *rangerTank << '\n';
	}
	if (rangerWarrior) {
		std::cout << "Hybrid option: " << *rangerWarrior << '\n';
	}
	if (eliteRanger) {
		std::cout << "Elite option: " << *eliteRanger << '\n';
	}
	if (eliteWarrior) {
		std::cout << "Elite option: " << *eliteWarrior << '\n';
	}

	std::cout << "\nBattle sample\n";
	if (rangerTank && eliteWarrior) {
		printFight(*rangerTank, *eliteWarrior);
	}

	std::cout << "\nTier progression sample\n";
	if (eliteRanger) {
		auto eliteTier6 = eliteRanger->evolve();
		auto eliteTier7 = eliteTier6->evolve();
		auto eliteTier8 = eliteTier7->evolve();
		std::cout << *eliteTier8 << '\n';
	}
	if (rangerTank) {
		auto hybridTier6 = rangerTank->evolve();
		auto hybridTier7 = hybridTier6->evolve();
		auto hybridTier8 = hybridTier7->evolve();
		std::cout << *hybridTier8 << '\n';
	}

	std::cout << "\nElement upgrade sample\n";
	printUpgradeResult("Tank + Earth", tank.applyElementUpgrade(Element::Earth));
	printUpgradeResult("Tank + Fire", tank.applyElementUpgrade(Element::Fire));
	if (rangerTank) {
		printUpgradeResult("Ranger-Tank + Water", rangerTank->applyElementUpgrade(Element::Water));
		printUpgradeResult("Ranger-Tank + Earth", rangerTank->applyElementUpgrade(Element::Earth));
	}

	return 0;
}