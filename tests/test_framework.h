// Minimal single-header test framework for the MultiTimer host tests.
// Standard library only; no external test dependency.
#ifndef TESTS_TEST_FRAMEWORK_H_
#define TESTS_TEST_FRAMEWORK_H_

#include <cstddef>
#include <cstdint>
#include <sstream>
#include <string>
#include <vector>

namespace testfw {

struct Failure {
	std::string file;
	int line;
	std::string message;
};

class TestCase {
public:
	TestCase(const std::string& name, void (*fn)());
	void run();

	const std::string& name() const { return mName; }
	bool passed() const { return mFailures.empty(); }
	const std::vector<Failure>& failures() const { return mFailures; }
	void addFailure(const char* file, int line, const std::string& message);

private:
	std::string mName;
	void (*mFn)();
	std::vector<Failure> mFailures;
};

std::vector<TestCase*>& registry();
int runAll(const char* suiteName);

// Thrown after the first failed assertion so the rest of that case is skipped
// instead of running on possibly invalid state.
struct AbortCase {
};

void reportFailure(const char* file, int line, const std::string& message);

class Registrar {
public:
	Registrar(const char* name, void (*fn)()) {
		registry().push_back(new TestCase(name, fn));
	}
};

template <typename T>
std::string str(const T& value) {
	std::ostringstream os;
	os << value;
	return os.str();
}

inline std::string str(const std::string& value) {
	return std::string("\"") + value + "\"";
}

inline std::string str(const char* value) {
	return std::string("\"") + (value != NULL ? value : "(null)") + "\"";
}

inline std::string str(bool value) {
	return value ? "true" : "false";
}

inline std::string str(uint8_t value) {
	return str(static_cast<unsigned int>(value));
}

inline std::string str(int8_t value) {
	return str(static_cast<int>(value));
}

}  // namespace testfw

#define TF_TEST(name)                                            \
	static void name();                                          \
	static ::testfw::Registrar name##_registrar(#name, &name);   \
	static void name()

#define TF_FAIL(message)                                         \
	do {                                                         \
		::testfw::reportFailure(__FILE__, __LINE__, (message));  \
	} while (0)

#define TF_CHECK(condition)                                      \
	do {                                                         \
		if (!(condition)) {                                      \
			::testfw::reportFailure(__FILE__, __LINE__,          \
					"CHECK failed: " #condition);                \
		}                                                        \
	} while (0)

#define TF_CHECK_MSG(condition, message)                         \
	do {                                                         \
		if (!(condition)) {                                      \
			::testfw::reportFailure(__FILE__, __LINE__,          \
					std::string("CHECK failed: " #condition      \
							" | ") + (message));                 \
		}                                                        \
	} while (0)

#define TF_EQ(actual, expected)                                  \
	do {                                                         \
		const auto& tf_actual = (actual);                        \
		const auto& tf_expected = (expected);                    \
		if (!(tf_actual == tf_expected)) {                       \
			::testfw::reportFailure(__FILE__, __LINE__,          \
					std::string("EQ failed: " #actual " == "     \
							#expected " | actual=") +            \
							::testfw::str(tf_actual) +           \
							" expected=" +                       \
							::testfw::str(tf_expected));         \
		}                                                        \
	} while (0)

#endif  // TESTS_TEST_FRAMEWORK_H_
