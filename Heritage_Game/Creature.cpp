#include "Creature.hpp"

#include <algorithm>
#include <iomanip>
#include <random>
#include <sstream>
#include <utility>

namespace {
constexpr int kMaxTier = 8;
constexpr int kSpecializeTier = 5;
constexpr int kBaseMaxTier = 4;

thread_local std::mt19937 g_rng(std::random_device{}());

Stats makeStats(double attack, double defense, double evasion, double life) {
	return Stats{attack, defense, evasion, life};
}

Stats clampStats(Stats stats) {
	stats.attack = std::max(0.0, stats.attack);
	stats.defense = std::max(0.0, stats.defense);
	stats.evasion = std::clamp(stats.evasion, 0.0, 0.65);
	stats.life = std::max(1.0, stats.life);
	return stats;
}

int clampBaseTier(int tier) {
	return std::clamp(tier, 1, kBaseMaxTier);
}

int specializationTierFrom(int tier) {
	return std::max(kSpecializeTier, tier + 1);
}

Stats scaleStats(const Stats &value, double factor) {
	return makeStats(value.attack * factor, value.defense * factor, value.evasion * factor, value.life * factor);
}

double randomEvolutionMultiplier() {
	std::uniform_int_distribution<int> distribution(0, 2);
	const int outcome = distribution(g_rng);
	if (outcome == 0) {
		return 0.85;
	}
	if (outcome == 1) {
		return 1.0;
	}
	return 1.15;
}

bool isBaseArchetype(Archetype archetype) {
	return archetype == Archetype::Ranger || archetype == Archetype::Warrior || archetype == Archetype::Tank;
}

std::array<Archetype, 2> archetypeComponents(Archetype archetype) {
	switch (archetype) {
	case Archetype::Ranger:
	case Archetype::EliteRanger:
		return {Archetype::Ranger, Archetype::Ranger};
	case Archetype::Warrior:
	case Archetype::EliteWarrior:
		return {Archetype::Warrior, Archetype::Warrior};
	case Archetype::Tank:
	case Archetype::EliteTank:
		return {Archetype::Tank, Archetype::Tank};
	case Archetype::RangerWarrior:
		return {Archetype::Ranger, Archetype::Warrior};
	case Archetype::RangerTank:
		return {Archetype::Ranger, Archetype::Tank};
	case Archetype::WarriorTank:
		return {Archetype::Warrior, Archetype::Tank};
	}
	return {Archetype::Ranger, Archetype::Ranger};
}

double directMultiplier(Archetype attacker, Archetype defender) {
	if (attacker == defender) {
		return 1.0;
	}
	if (attacker == Archetype::Ranger && defender == Archetype::Warrior) {
		return 1.15;
	}
	if (attacker == Archetype::Warrior && defender == Archetype::Tank) {
		return 1.15;
	}
	if (attacker == Archetype::Tank && defender == Archetype::Ranger) {
		return 1.15;
	}
	if (attacker == Archetype::Warrior && defender == Archetype::Ranger) {
		return 0.85;
	}
	if (attacker == Archetype::Tank && defender == Archetype::Warrior) {
		return 0.85;
	}
	if (attacker == Archetype::Ranger && defender == Archetype::Tank) {
		return 0.85;
	}
	return 1.0;
}

bool elementAffinityForBase(Archetype baseArchetype, Element element) {
	switch (baseArchetype) {
	case Archetype::Tank:
		return element == Element::Earth || element == Element::Water;
	case Archetype::Warrior:
		return element == Element::Fire || element == Element::Air;
	case Archetype::Ranger:
		return element == Element::Fire || element == Element::Water;
	default:
		return false;
	}
}

Stats elementDelta(Element element) {
	switch (element) {
	case Element::Fire:
		return makeStats(2.0, 0.0, 0.0, 0.0);
	case Element::Earth:
		return makeStats(0.0, 2.0, 0.0, 0.0);
	case Element::Air:
		return makeStats(0.0, 0.0, 0.02, 0.0);
	case Element::Water:
		return makeStats(0.0, 0.0, 0.0, 3.0);
	}
	return makeStats(0.0, 0.0, 0.0, 0.0);
}

std::unique_ptr<Creature> makeHybrid(Archetype left, Archetype right, std::string name, int tier, Stats mergedStats);

} // namespace

Stats operator+(const Stats &lhs, const Stats &rhs) {
	return Stats{lhs.attack + rhs.attack, lhs.defense + rhs.defense, lhs.evasion + rhs.evasion, lhs.life + rhs.life};
}

Stats &operator+=(Stats &lhs, const Stats &rhs) {
	lhs = lhs + rhs;
	return lhs;
}

Creature::Creature(std::string name, Stats stats, int tier)
	: name_(std::move(name)), tier_(std::clamp(tier, 1, kMaxTier)), stats_(clampStats(stats)) {}

const std::string &Creature::name() const { return name_; }
int Creature::tier() const { return tier_; }
const Stats &Creature::stats() const { return stats_; }

void Creature::setName(std::string name) { name_ = std::move(name); }

void Creature::setTier(int tier) {
	tier_ = std::clamp(tier, 1, kMaxTier);
}

void Creature::setStats(Stats stats) {
	stats_ = clampStats(stats);
}

void Creature::addStats(const Stats &delta) {
	stats_ += delta;
	stats_ = clampStats(stats_);
}

std::array<Archetype, 2> Creature::lineage() const {
	return archetypeComponents(archetype());
}

bool Creature::hasElementAffinity(Element element) const {
	const auto parts = lineage();
	return elementAffinityForBase(parts[0], element) || elementAffinityForBase(parts[1], element);
}

double Creature::elementSuccessChance(Element element) const {
	return hasElementAffinity(element) ? 1.0 : 0.9;
}

UpgradeAttempt Creature::applyElementUpgrade(Element element) {
	const double chance = elementSuccessChance(element);
	std::uniform_real_distribution<double> distribution(0.0, 1.0);
	const bool success = distribution(g_rng) <= chance;
	const Stats delta = success ? elementDelta(element) : makeStats(0.0, 0.0, 0.0, 0.0);
	if (success) {
		addStats(delta);
	}
	return UpgradeAttempt{element, chance, success, delta};
}

double Creature::matchupMultiplierAgainst(const Creature &other) const {
	const auto mine = archetypeComponents(archetype());
	const auto theirs = archetypeComponents(other.archetype());
	double total = 0.0;
	for (Archetype minePart : mine) {
		for (Archetype theirPart : theirs) {
			total += directMultiplier(minePart, theirPart);
		}
	}
	return total / 4.0;
}

double Creature::combatScoreAgainst(const Creature &other) const {
	const double base = stats_.attack * 1.6 + stats_.defense * 1.15 + stats_.life * 0.45 + stats_.evasion * 30.0 + tier_ * 2.0;
	return base * matchupMultiplierAgainst(other);
}

std::string Creature::summary() const {
	std::ostringstream out;
	out << name_ << " [" << className() << "]"
		<< " tier " << tier_ << " | "
		<< "atk " << std::fixed << std::setprecision(1) << stats_.attack
		<< " def " << stats_.defense
		<< " eva " << std::setprecision(2) << stats_.evasion
		<< " life " << std::setprecision(1) << stats_.life;
	return out.str();
}

Ranger::Ranger(std::string name, int tier)
	: Creature(std::move(name), baseStats(clampBaseTier(tier)), clampBaseTier(tier)) {}

Archetype Ranger::archetype() const { return Archetype::Ranger; }
std::string Ranger::className() const { return "Ranger"; }
std::unique_ptr<Creature> Ranger::clone() const { return std::make_unique<Ranger>(*this); }

Stats Ranger::baseStats(int tier) {
	return clampStats(makeStats(8.0 + (tier - 1) * 1.8, 4.0 + (tier - 1) * 0.8, 0.16 + (tier - 1) * 0.01, 20.0 + (tier - 1) * 1.2));
}

Stats Ranger::evolutionBoost() { return makeStats(2.0, 0.0, 0.02, 1.0); }

std::unique_ptr<Creature> Ranger::evolve() const {
	if (tier() >= kBaseMaxTier) {
		return clone();
	}
	auto evolved = std::make_unique<Ranger>(*this);
	evolved->setTier(tier() + 1);
	evolved->addStats(scaleStats(evolutionBoost(), randomEvolutionMultiplier()));
	return evolved;
}

std::unique_ptr<Creature> Ranger::eliteEvolve() const {
	if (tier() < kBaseMaxTier || !isBaseArchetype(archetype())) {
		return nullptr;
	}
	auto elite = std::make_unique<EliteRanger>(name(), specializationTierFrom(tier()));
	elite->setStats(stats() + makeStats(2.0, 1.0, 0.02, 2.0));
	return elite;
}

std::unique_ptr<Creature> Ranger::crossEvolve(const Creature &partner) const {
	if (tier() < kBaseMaxTier || partner.tier() < kBaseMaxTier) {
		return nullptr;
	}
	if (!isBaseArchetype(archetype()) || !isBaseArchetype(partner.archetype())) {
		return nullptr;
	}
	return makeHybrid(archetype(), partner.archetype(), name() + "-" + partner.name(), specializationTierFrom(std::max(tier(), partner.tier())), stats() + partner.stats());
}

Warrior::Warrior(std::string name, int tier)
	: Creature(std::move(name), baseStats(clampBaseTier(tier)), clampBaseTier(tier)) {}

Archetype Warrior::archetype() const { return Archetype::Warrior; }
std::string Warrior::className() const { return "Warrior"; }
std::unique_ptr<Creature> Warrior::clone() const { return std::make_unique<Warrior>(*this); }

Stats Warrior::baseStats(int tier) {
	return clampStats(makeStats(10.0 + (tier - 1) * 2.0, 3.0 + (tier - 1) * 0.6, 0.14 + (tier - 1) * 0.012, 18.0 + (tier - 1) * 1.0));
}

Stats Warrior::evolutionBoost() { return makeStats(3.0, 0.0, 0.01, 0.5); }

std::unique_ptr<Creature> Warrior::evolve() const {
	if (tier() >= kBaseMaxTier) {
		return clone();
	}
	auto evolved = std::make_unique<Warrior>(*this);
	evolved->setTier(tier() + 1);
	evolved->addStats(scaleStats(evolutionBoost(), randomEvolutionMultiplier()));
	return evolved;
}

std::unique_ptr<Creature> Warrior::eliteEvolve() const {
	if (tier() < kBaseMaxTier || !isBaseArchetype(archetype())) {
		return nullptr;
	}
	auto elite = std::make_unique<EliteWarrior>(name(), specializationTierFrom(tier()));
	elite->setStats(stats() + makeStats(3.0, 0.5, 0.02, 1.0));
	return elite;
}

std::unique_ptr<Creature> Warrior::crossEvolve(const Creature &partner) const {
	if (tier() < kBaseMaxTier || partner.tier() < kBaseMaxTier) {
		return nullptr;
	}
	if (!isBaseArchetype(archetype()) || !isBaseArchetype(partner.archetype())) {
		return nullptr;
	}
	return makeHybrid(archetype(), partner.archetype(), name() + "-" + partner.name(), specializationTierFrom(std::max(tier(), partner.tier())), stats() + partner.stats());
}

Tank::Tank(std::string name, int tier)
	: Creature(std::move(name), baseStats(clampBaseTier(tier)), clampBaseTier(tier)) {}

Archetype Tank::archetype() const { return Archetype::Tank; }
std::string Tank::className() const { return "Tank"; }
std::unique_ptr<Creature> Tank::clone() const { return std::make_unique<Tank>(*this); }

Stats Tank::baseStats(int tier) {
	return clampStats(makeStats(5.0 + (tier - 1) * 1.0, 8.0 + (tier - 1) * 1.8, 0.08 + (tier - 1) * 0.008, 26.0 + (tier - 1) * 2.0));
}

Stats Tank::evolutionBoost() { return makeStats(1.0, 2.0, 0.01, 3.0); }

std::unique_ptr<Creature> Tank::evolve() const {
	if (tier() >= kBaseMaxTier) {
		return clone();
	}
	auto evolved = std::make_unique<Tank>(*this);
	evolved->setTier(tier() + 1);
	evolved->addStats(scaleStats(evolutionBoost(), randomEvolutionMultiplier()));
	return evolved;
}

std::unique_ptr<Creature> Tank::eliteEvolve() const {
	if (tier() < kBaseMaxTier || !isBaseArchetype(archetype())) {
		return nullptr;
	}
	auto elite = std::make_unique<EliteTank>(name(), specializationTierFrom(tier()));
	elite->setStats(stats() + makeStats(1.0, 3.0, 0.01, 4.0));
	return elite;
}

std::unique_ptr<Creature> Tank::crossEvolve(const Creature &partner) const {
	if (tier() < kBaseMaxTier || partner.tier() < kBaseMaxTier) {
		return nullptr;
	}
	if (!isBaseArchetype(archetype()) || !isBaseArchetype(partner.archetype())) {
		return nullptr;
	}
	return makeHybrid(archetype(), partner.archetype(), name() + "-" + partner.name(), specializationTierFrom(std::max(tier(), partner.tier())), stats() + partner.stats());
}

RangerWarrior::RangerWarrior(std::string name, int tier)
	: Creature(name, baseStats(tier), tier), Ranger(name, tier), Warrior(name, tier) {}

Archetype RangerWarrior::archetype() const { return Archetype::RangerWarrior; }
std::string RangerWarrior::className() const { return "Ranger-Warrior"; }
std::unique_ptr<Creature> RangerWarrior::clone() const { return std::make_unique<RangerWarrior>(*this); }

Stats RangerWarrior::baseStats(int tier) {
	return clampStats(makeStats(9.0 + (tier - 1) * 1.8, 3.5 + (tier - 1) * 0.7, 0.15 + (tier - 1) * 0.012, 20.0 + (tier - 1) * 1.2));
}

Stats RangerWarrior::evolutionBoost() { return makeStats(2.0, 0.5, 0.02, 1.0); }

std::unique_ptr<Creature> RangerWarrior::evolve() const {
	if (tier() >= kMaxTier) {
		return clone();
	}
	auto evolved = std::make_unique<RangerWarrior>(*this);
	evolved->setTier(tier() + 1);
	evolved->addStats(scaleStats(evolutionBoost(), randomEvolutionMultiplier()));
	return evolved;
}

std::unique_ptr<Creature> RangerWarrior::eliteEvolve() const { return nullptr; }
std::unique_ptr<Creature> RangerWarrior::crossEvolve(const Creature &partner) const {
	(void)partner;
	return nullptr;
}

RangerTank::RangerTank(std::string name, int tier)
	: Creature(name, baseStats(tier), tier), Ranger(name, tier), Tank(name, tier) {}

Archetype RangerTank::archetype() const { return Archetype::RangerTank; }
std::string RangerTank::className() const { return "Ranger-Tank"; }
std::unique_ptr<Creature> RangerTank::clone() const { return std::make_unique<RangerTank>(*this); }

Stats RangerTank::baseStats(int tier) {
	return clampStats(makeStats(7.0 + (tier - 1) * 1.5, 7.0 + (tier - 1) * 1.5, 0.12 + (tier - 1) * 0.01, 24.0 + (tier - 1) * 1.6));
}

Stats RangerTank::evolutionBoost() { return makeStats(1.0, 1.0, 0.015, 2.0); }

std::unique_ptr<Creature> RangerTank::evolve() const {
	if (tier() >= kMaxTier) {
		return clone();
	}
	auto evolved = std::make_unique<RangerTank>(*this);
	evolved->setTier(tier() + 1);
	evolved->addStats(scaleStats(evolutionBoost(), randomEvolutionMultiplier()));
	return evolved;
}

std::unique_ptr<Creature> RangerTank::eliteEvolve() const { return nullptr; }
std::unique_ptr<Creature> RangerTank::crossEvolve(const Creature &partner) const {
	(void)partner;
	return nullptr;
}

WarriorTank::WarriorTank(std::string name, int tier)
	: Creature(name, baseStats(tier), tier), Warrior(name, tier), Tank(name, tier) {}

Archetype WarriorTank::archetype() const { return Archetype::WarriorTank; }
std::string WarriorTank::className() const { return "Warrior-Tank"; }
std::unique_ptr<Creature> WarriorTank::clone() const { return std::make_unique<WarriorTank>(*this); }

Stats WarriorTank::baseStats(int tier) {
	return clampStats(makeStats(8.0 + (tier - 1) * 1.4, 6.0 + (tier - 1) * 1.4, 0.10 + (tier - 1) * 0.01, 22.0 + (tier - 1) * 1.5));
}

Stats WarriorTank::evolutionBoost() { return makeStats(1.5, 1.5, 0.01, 2.0); }

std::unique_ptr<Creature> WarriorTank::evolve() const {
	if (tier() >= kMaxTier) {
		return clone();
	}
	auto evolved = std::make_unique<WarriorTank>(*this);
	evolved->setTier(tier() + 1);
	evolved->addStats(scaleStats(evolutionBoost(), randomEvolutionMultiplier()));
	return evolved;
}

std::unique_ptr<Creature> WarriorTank::eliteEvolve() const { return nullptr; }
std::unique_ptr<Creature> WarriorTank::crossEvolve(const Creature &partner) const {
	(void)partner;
	return nullptr;
}

EliteRanger::EliteRanger(std::string name, int tier)
	: Creature(name, baseStats(tier), tier), Ranger(name, tier) {}

Archetype EliteRanger::archetype() const { return Archetype::EliteRanger; }
std::string EliteRanger::className() const { return "Elite-Ranger"; }
std::unique_ptr<Creature> EliteRanger::clone() const { return std::make_unique<EliteRanger>(*this); }

Stats EliteRanger::baseStats(int tier) {
	return clampStats(makeStats(11.0 + (tier - 5) * 2.0, 6.0 + (tier - 5) * 1.0, 0.22 + (tier - 5) * 0.01, 28.0 + (tier - 5) * 1.5));
}

Stats EliteRanger::evolutionBoost() { return makeStats(2.5, 0.8, 0.02, 1.5); }

std::unique_ptr<Creature> EliteRanger::evolve() const {
	if (tier() >= kMaxTier) {
		return clone();
	}
	auto evolved = std::make_unique<EliteRanger>(*this);
	evolved->setTier(tier() + 1);
	evolved->addStats(scaleStats(evolutionBoost(), randomEvolutionMultiplier()));
	return evolved;
}

std::unique_ptr<Creature> EliteRanger::eliteEvolve() const { return nullptr; }
std::unique_ptr<Creature> EliteRanger::crossEvolve(const Creature &partner) const {
	(void)partner;
	return nullptr;
}

EliteWarrior::EliteWarrior(std::string name, int tier)
	: Creature(name, baseStats(tier), tier), Warrior(name, tier) {}

Archetype EliteWarrior::archetype() const { return Archetype::EliteWarrior; }
std::string EliteWarrior::className() const { return "Elite-Warrior"; }
std::unique_ptr<Creature> EliteWarrior::clone() const { return std::make_unique<EliteWarrior>(*this); }

Stats EliteWarrior::baseStats(int tier) {
	return clampStats(makeStats(14.0 + (tier - 5) * 2.2, 5.0 + (tier - 5) * 0.8, 0.20 + (tier - 5) * 0.01, 24.0 + (tier - 5) * 1.2));
}

Stats EliteWarrior::evolutionBoost() { return makeStats(3.0, 0.6, 0.015, 1.0); }

std::unique_ptr<Creature> EliteWarrior::evolve() const {
	if (tier() >= kMaxTier) {
		return clone();
	}
	auto evolved = std::make_unique<EliteWarrior>(*this);
	evolved->setTier(tier() + 1);
	evolved->addStats(scaleStats(evolutionBoost(), randomEvolutionMultiplier()));
	return evolved;
}

std::unique_ptr<Creature> EliteWarrior::eliteEvolve() const { return nullptr; }
std::unique_ptr<Creature> EliteWarrior::crossEvolve(const Creature &partner) const {
	(void)partner;
	return nullptr;
}

EliteTank::EliteTank(std::string name, int tier)
	: Creature(name, baseStats(tier), tier), Tank(name, tier) {}

Archetype EliteTank::archetype() const { return Archetype::EliteTank; }
std::string EliteTank::className() const { return "Elite-Tank"; }
std::unique_ptr<Creature> EliteTank::clone() const { return std::make_unique<EliteTank>(*this); }

Stats EliteTank::baseStats(int tier) {
	return clampStats(makeStats(8.0 + (tier - 5) * 1.4, 13.0 + (tier - 5) * 2.2, 0.12 + (tier - 5) * 0.008, 35.0 + (tier - 5) * 2.8));
}

Stats EliteTank::evolutionBoost() { return makeStats(1.0, 2.5, 0.01, 3.0); }

std::unique_ptr<Creature> EliteTank::evolve() const {
	if (tier() >= kMaxTier) {
		return clone();
	}
	auto evolved = std::make_unique<EliteTank>(*this);
	evolved->setTier(tier() + 1);
	evolved->addStats(scaleStats(evolutionBoost(), randomEvolutionMultiplier()));
	return evolved;
}

std::unique_ptr<Creature> EliteTank::eliteEvolve() const { return nullptr; }
std::unique_ptr<Creature> EliteTank::crossEvolve(const Creature &partner) const {
	(void)partner;
	return nullptr;
}

namespace {

std::unique_ptr<Creature> makeHybrid(Archetype left, Archetype right, std::string name, int tier, Stats mergedStats) {
	std::unique_ptr<Creature> result;
	if ((left == Archetype::Ranger && right == Archetype::Warrior) || (left == Archetype::Warrior && right == Archetype::Ranger)) {
		result = std::make_unique<RangerWarrior>(std::move(name), tier);
	} else if ((left == Archetype::Ranger && right == Archetype::Tank) || (left == Archetype::Tank && right == Archetype::Ranger)) {
		result = std::make_unique<RangerTank>(std::move(name), tier);
	} else if ((left == Archetype::Warrior && right == Archetype::Tank) || (left == Archetype::Tank && right == Archetype::Warrior)) {
		result = std::make_unique<WarriorTank>(std::move(name), tier);
	}
	(void)mergedStats;
	return result;
}

} // namespace

std::ostream &operator<<(std::ostream &stream, const Creature &creature) {
	return stream << creature.summary();
}
