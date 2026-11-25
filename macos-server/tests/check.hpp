#pragma once

#include <string>

namespace penbridge::test {

void check(bool passed, const char* file, int line, const std::string& expression);
int  failureCount();

bool near(double a, double b, double epsilon = 0.01);

void protocolTests();
void streamFilterTests();
void penStateTests();
void jitterBufferTests();
void strokeSmootherTests();
void tabletEventTests();

} // namespace penbridge::test

// Not assert(): Release builds define NDEBUG, which would silently remove every check.
#define CHECK(expression) ::penbridge::test::check((expression), __FILE__, __LINE__, #expression)
