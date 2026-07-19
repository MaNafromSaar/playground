#ifndef QUATERNION_HPP
#define QUATERNION_HPP
#include <string>
#include <ostream>

class Quaternion {
public:
	Quaternion();
	Quaternion(double real, double i = 0.0, double j = 0.0, double k = 0.0);

	double real() const;
	double i() const;
	double j() const;
	double k() const;

	Quaternion conjugate() const;
	double normSquared() const;
	double norm() const;
	Quaternion inverse() const;

	std::string toString() const;

	Quaternion operator+(const Quaternion &other) const;
	Quaternion operator-(const Quaternion &other) const;
	Quaternion operator*(const Quaternion &other) const;
	Quaternion operator/(const Quaternion &other) const;

	Quaternion operator*(double scalar) const;
	Quaternion operator/(double scalar) const;

	Quaternion &operator+=(const Quaternion &other);
	Quaternion &operator-=(const Quaternion &other);
	Quaternion &operator*=(const Quaternion &other);
	Quaternion &operator/=(const Quaternion &other);

	bool isReal(double epsilon = 1e-12) const;

private:
	double realPart_;
	double iPart_;
	double jPart_;
	double kPart_;
};

Quaternion operator*(double scalar, const Quaternion &value);
std::ostream &operator<<(std::ostream &stream, const Quaternion &value);

#endif
