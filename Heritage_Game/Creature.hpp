#ifndef CREATURE_HPP
#define CREATURE_HPP

#include <array>
#include <iosfwd>
#include <memory>
#include <string>

enum class Archetype {
	Ranger,
	Warrior,
	Tank,
	RangerWarrior,
	RangerTank,
	WarriorTank,
	EliteRanger,
	EliteWarrior,
	EliteTank
};

enum class Element {
	Fire,
	Earth,
	Air,
	Water
};

struct Stats {
	double attack;
	double defense;
	double evasion;
	double life;
};

struct UpgradeAttempt {
	Element element;
	double successChance;
	bool success;
	Stats appliedDelta;
};

Stats operator+(const Stats &lhs, const Stats &rhs);
Stats &operator+=(Stats &lhs, const Stats &rhs);

class Creature {
public:
	virtual ~Creature() = default;

	const std::string &name() const;
	int tier() const;
	const Stats &stats() const;

	virtual Archetype archetype() const = 0;
	virtual std::string className() const = 0;
	virtual std::unique_ptr<Creature> clone() const = 0;
	virtual std::unique_ptr<Creature> evolve() const = 0;
	virtual std::unique_ptr<Creature> eliteEvolve() const = 0;
	virtual std::unique_ptr<Creature> crossEvolve(const Creature &partner) const = 0;

	std::array<Archetype, 2> lineage() const;
	bool hasElementAffinity(Element element) const;
	double elementSuccessChance(Element element) const;
	UpgradeAttempt applyElementUpgrade(Element element);
	double matchupMultiplierAgainst(const Creature &other) const;
	double combatScoreAgainst(const Creature &other) const;
	std::string summary() const;

protected:
	Creature(std::string name, Stats stats, int tier);

	void setName(std::string name);
	void setTier(int tier);
	void setStats(Stats stats);
	void addStats(const Stats &delta);

private:
	std::string name_;
	int tier_;
	Stats stats_;
};

class Ranger : virtual public Creature {
public:
	explicit Ranger(std::string name, int tier = 1);

	Archetype archetype() const override;
	std::string className() const override;
	std::unique_ptr<Creature> clone() const override;
	std::unique_ptr<Creature> evolve() const override;
	std::unique_ptr<Creature> eliteEvolve() const override;
	std::unique_ptr<Creature> crossEvolve(const Creature &partner) const override;

	static Stats baseStats(int tier);
	static Stats evolutionBoost();
};

class Tank : virtual public Creature {
public:
	explicit Tank(std::string name, int tier = 1);

	Archetype archetype() const override;
	std::string className() const override;
	std::unique_ptr<Creature> clone() const override;
	std::unique_ptr<Creature> evolve() const override;
	std::unique_ptr<Creature> eliteEvolve() const override;
	std::unique_ptr<Creature> crossEvolve(const Creature &partner) const override;

	static Stats baseStats(int tier);
	static Stats evolutionBoost();
};

class Warrior : virtual public Creature {
public:
	explicit Warrior(std::string name, int tier = 1);

	Archetype archetype() const override;
	std::string className() const override;
	std::unique_ptr<Creature> clone() const override;
	std::unique_ptr<Creature> evolve() const override;
	std::unique_ptr<Creature> eliteEvolve() const override;
	std::unique_ptr<Creature> crossEvolve(const Creature &partner) const override;

	static Stats baseStats(int tier);
	static Stats evolutionBoost();
};

class RangerTank : public Ranger, public Tank {
public:
	explicit RangerTank(std::string name, int tier = 5);

	Archetype archetype() const override;
	std::string className() const override;
	std::unique_ptr<Creature> clone() const override;
	std::unique_ptr<Creature> evolve() const override;
	std::unique_ptr<Creature> eliteEvolve() const override;
	std::unique_ptr<Creature> crossEvolve(const Creature &partner) const override;

	static Stats baseStats(int tier);
	static Stats evolutionBoost();
};

class RangerWarrior : public Ranger, public Warrior {
public:
	explicit RangerWarrior(std::string name, int tier = 5);

	Archetype archetype() const override;
	std::string className() const override;
	std::unique_ptr<Creature> clone() const override;
	std::unique_ptr<Creature> evolve() const override;
	std::unique_ptr<Creature> eliteEvolve() const override;
	std::unique_ptr<Creature> crossEvolve(const Creature &partner) const override;

	static Stats baseStats(int tier);
	static Stats evolutionBoost();
};

class WarriorTank : public Warrior, public Tank {
public:
	explicit WarriorTank(std::string name, int tier = 5);

	Archetype archetype() const override;
	std::string className() const override;
	std::unique_ptr<Creature> clone() const override;
	std::unique_ptr<Creature> evolve() const override;
	std::unique_ptr<Creature> eliteEvolve() const override;
	std::unique_ptr<Creature> crossEvolve(const Creature &partner) const override;

	static Stats baseStats(int tier);
	static Stats evolutionBoost();
};

class EliteRanger : public Ranger {
public:
	explicit EliteRanger(std::string name, int tier = 5);

	Archetype archetype() const override;
	std::string className() const override;
	std::unique_ptr<Creature> clone() const override;
	std::unique_ptr<Creature> evolve() const override;
	std::unique_ptr<Creature> eliteEvolve() const override;
	std::unique_ptr<Creature> crossEvolve(const Creature &partner) const override;

	static Stats baseStats(int tier);
	static Stats evolutionBoost();
};

class EliteWarrior : public Warrior {
public:
	explicit EliteWarrior(std::string name, int tier = 5);

	Archetype archetype() const override;
	std::string className() const override;
	std::unique_ptr<Creature> clone() const override;
	std::unique_ptr<Creature> evolve() const override;
	std::unique_ptr<Creature> eliteEvolve() const override;
	std::unique_ptr<Creature> crossEvolve(const Creature &partner) const override;

	static Stats baseStats(int tier);
	static Stats evolutionBoost();
};

class EliteTank : public Tank {
public:
	explicit EliteTank(std::string name, int tier = 5);

	Archetype archetype() const override;
	std::string className() const override;
	std::unique_ptr<Creature> clone() const override;
	std::unique_ptr<Creature> evolve() const override;
	std::unique_ptr<Creature> eliteEvolve() const override;
	std::unique_ptr<Creature> crossEvolve(const Creature &partner) const override;

	static Stats baseStats(int tier);
	static Stats evolutionBoost();
};

std::ostream &operator<<(std::ostream &stream, const Creature &creature);

#endif