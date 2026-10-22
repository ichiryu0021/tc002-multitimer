// Runner for the MultiTimer host unit tests.
#include "test_framework.h"

#include <cstdio>
#include <exception>

namespace testfw {
namespace {

TestCase* gCurrent = NULL;

}  // namespace

std::vector<TestCase*>& registry() {
	static std::vector<TestCase*> cases;
	return cases;
}

TestCase::TestCase(const std::string& name, void (*fn)())
	: mName(name), mFn(fn) {
}

void TestCase::addFailure(const char* file, int line,
		const std::string& message) {
	Failure failure;
	failure.file = (file != NULL) ? file : "";
	failure.line = line;
	failure.message = message;
	mFailures.push_back(failure);
}

void TestCase::run() {
	mFailures.clear();
	try {
		mFn();
	} catch (const AbortCase&) {
		// Failure already recorded by reportFailure.
	} catch (const std::exception& e) {
		addFailure(__FILE__, __LINE__,
				std::string("unexpected std::exception: ") + e.what());
	} catch (...) {
		addFailure(__FILE__, __LINE__, "unexpected unknown exception");
	}
}

void reportFailure(const char* file, int line, const std::string& message) {
	if (gCurrent != NULL) {
		gCurrent->addFailure(file, line, message);
	}
	throw AbortCase();
}

int runAll(const char* suiteName) {
	std::vector<TestCase*>& cases = registry();
	std::printf("Suite: %s\n", (suiteName != NULL) ? suiteName : "(unnamed)");
	std::printf("Host C++ tests for native/MultiTimer/src pure modules\n\n");

	int failed = 0;
	for (size_t i = 0; i < cases.size(); ++i) {
		TestCase* testCase = cases[i];
		gCurrent = testCase;
		testCase->run();
		gCurrent = NULL;

		if (testCase->passed()) {
			std::printf("[PASS] %s\n", testCase->name().c_str());
			continue;
		}
		++failed;
		std::printf("[FAIL] %s\n", testCase->name().c_str());
		const std::vector<Failure>& failures = testCase->failures();
		for (size_t j = 0; j < failures.size(); ++j) {
			std::printf("       %s:%d: %s\n", failures[j].file.c_str(),
					failures[j].line, failures[j].message.c_str());
		}
	}

	const int total = static_cast<int>(cases.size());
	const int passed = total - failed;
	std::printf("\nSummary: %d cases | passed %d | failed %d\n", total,
			passed, failed);
	std::printf("RESULT: %s\n", (failed == 0) ? "ALL TESTS PASSED"
											 : "FAILURES DETECTED");
	return failed;
}

}  // namespace testfw

int main() {
	return testfw::runAll("ULanzi MultiTimer host unit tests");
}
