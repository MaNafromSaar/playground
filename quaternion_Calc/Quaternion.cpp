#include "Quaternion.hpp"

#include <cmath>
#include <iomanip>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>

Quaternion::Quaternion()
	: realPart_(0.0), iPart_(0.0), jPart_(0.0), kPart_(0.0) {}

Quaternion::Quaternion(double real, double i, double j, double k)
	: realPart_(real), iPart_(i), jPart_(j), kPart_(k) {}

double Quaternion::real() const { return realPart_; }
double Quaternion::i() const { return iPart_; }
double Quaternion::j() const { return jPart_; }
double Quaternion::k() const { return kPart_; }

Quaternion Quaternion::conjugate() const {
	return Quaternion(realPart_, -iPart_, -jPart_, -kPart_);
}

double Quaternion::normSquared() const {
	return realPart_ * realPart_ + iPart_ * iPart_ + jPart_ * jPart_ + kPart_ * kPart_;
}

double Quaternion::norm() const { return std::sqrt(normSquared()); }

Quaternion Quaternion::inverse() const {
	const double squaredNorm = normSquared();
	if (squaredNorm == 0.0) {
		throw std::runtime_error("cannot invert a zero quaternion");
	}
	return conjugate() / squaredNorm;
}

std::string Quaternion::toString() const {
	std::ostringstream out;
	out << std::setprecision(12);

	bool firstTerm = true;
	auto appendTerm = [&](double value, const char *suffix) {
		if (std::fabs(value) <= 1e-12) {
			return;
		}
		if (!firstTerm) {
			out << (value < 0.0 ? " - " : " + ");
		} else if (value < 0.0) {
			out << "-";
		}
		const double magnitude = std::fabs(value);
		out << magnitude << suffix;
		firstTerm = false;
	};

	if (std::fabs(realPart_) > 1e-12 || (std::fabs(iPart_) <= 1e-12 && std::fabs(jPart_) <= 1e-12 && std::fabs(kPart_) <= 1e-12)) {
		out << realPart_;
		firstTerm = false;
	}

	appendTerm(iPart_, "i");
	appendTerm(jPart_, "j");
	appendTerm(kPart_, "k");

	return out.str();
}

Quaternion Quaternion::operator+(const Quaternion &other) const {
	return Quaternion(realPart_ + other.realPart_, iPart_ + other.iPart_, jPart_ + other.jPart_, kPart_ + other.kPart_);
}

Quaternion Quaternion::operator-(const Quaternion &other) const {
	return Quaternion(realPart_ - other.realPart_, iPart_ - other.iPart_, jPart_ - other.jPart_, kPart_ - other.kPart_);
}

Quaternion Quaternion::operator*(const Quaternion &other) const {
	return Quaternion(
		realPart_ * other.realPart_ - iPart_ * other.iPart_ - jPart_ * other.jPart_ - kPart_ * other.kPart_,
		realPart_ * other.iPart_ + iPart_ * other.realPart_ + jPart_ * other.kPart_ - kPart_ * other.jPart_,
		realPart_ * other.jPart_ - iPart_ * other.kPart_ + jPart_ * other.realPart_ + kPart_ * other.iPart_,
		realPart_ * other.kPart_ + iPart_ * other.jPart_ - jPart_ * other.iPart_ + kPart_ * other.realPart_);
}

Quaternion Quaternion::operator/(const Quaternion &other) const {
	return (*this) * other.inverse();
}

Quaternion Quaternion::operator*(double scalar) const {
	return Quaternion(realPart_ * scalar, iPart_ * scalar, jPart_ * scalar, kPart_ * scalar);
}

Quaternion Quaternion::operator/(double scalar) const {
	if (scalar == 0.0) {
		throw std::runtime_error("cannot divide by zero");
	}
	return (*this) * (1.0 / scalar);
}

Quaternion &Quaternion::operator+=(const Quaternion &other) {
	*this = *this + other;
	return *this;
}

Quaternion &Quaternion::operator-=(const Quaternion &other) {
	*this = *this - other;
	return *this;
}

Quaternion &Quaternion::operator*=(const Quaternion &other) {
	*this = *this * other;
	return *this;
}

Quaternion &Quaternion::operator/=(const Quaternion &other) {
	*this = *this / other;
	return *this;
}

bool Quaternion::isReal(double epsilon) const {
	return std::fabs(iPart_) <= epsilon && std::fabs(jPart_) <= epsilon && std::fabs(kPart_) <= epsilon;
}

Quaternion operator*(double scalar, const Quaternion &value) {
	return value * scalar;
}

std::ostream &operator<<(std::ostream &stream, const Quaternion &value) {
	stream << value.toString();
	return stream;
}

namespace {

double readDouble(const std::string &prompt) {
	for (;;) {
		std::cout << prompt;
		double value;
		if (std::cin >> value) {
			return value;
		}
		std::cin.clear();
		std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
		std::cout << "Invalid number, try again.\n";
	}
}

Quaternion readQuaternion(const std::string &label) {
	std::cout << "Enter " << label << " as four components (real i j k).\n";
	const double real = readDouble("  real: ");
	const double i = readDouble("  i: ");
	const double j = readDouble("  j: ");
	const double k = readDouble("  k: ");
	return Quaternion(real, i, j, k);
}

void printMenu() {
	std::cout << "\nQuaternion calculator\n"
			  << "1) add\n"
			  << "2) subtract\n"
			  << "3) multiply\n"
			  << "4) divide\n"
			  << "5) conjugate\n"
			  << "6) norm\n"
			  << "7) inverse\n"
			  << "0) quit\n";
}

} // namespace

int main() {
	std::cout << "Quaternion calculator\n";

	for (;;) {
		printMenu();
		std::cout << "Choice: ";

		int choice;
		if (!(std::cin >> choice)) {
			std::cin.clear();
			std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
			std::cout << "Invalid choice.\n";
			continue;
		}

		if (choice == 0) {
			break;
		}

		try {
			switch (choice) {
			case 1: {
				const Quaternion lhs = readQuaternion("the first quaternion");
				const Quaternion rhs = readQuaternion("the second quaternion");
				std::cout << "Result: " << (lhs + rhs) << '\n';
				break;
			}
			case 2: {
				const Quaternion lhs = readQuaternion("the first quaternion");
				const Quaternion rhs = readQuaternion("the second quaternion");
				std::cout << "Result: " << (lhs - rhs) << '\n';
				break;
			}
			case 3: {
				const Quaternion lhs = readQuaternion("the first quaternion");
				const Quaternion rhs = readQuaternion("the second quaternion");
				std::cout << "Result: " << (lhs * rhs) << '\n';
				break;
			}
			case 4: {
				const Quaternion lhs = readQuaternion("the first quaternion");
				const Quaternion rhs = readQuaternion("the second quaternion");
				std::cout << "Result: " << (lhs / rhs) << '\n';
				break;
			}
			case 5: {
				const Quaternion value = readQuaternion("a quaternion");
				std::cout << "Result: " << value.conjugate() << '\n';
				break;
			}
			case 6: {
				const Quaternion value = readQuaternion("a quaternion");
				std::cout << "Norm: " << value.norm() << '\n';
				break;
			}
			case 7: {
				const Quaternion value = readQuaternion("a quaternion");
				std::cout << "Result: " << value.inverse() << '\n';
				break;
			}
			default:
				std::cout << "Unknown option.\n";
				break;
			}
		} catch (const std::exception &error) {
			std::cout << "Error: " << error.what() << '\n';
		}
	}

	return 0;
}
