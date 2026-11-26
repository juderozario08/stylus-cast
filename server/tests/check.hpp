#pragma once

#include <string>

namespace stylus_cast::test {

void check(bool passed, const char* file, int line, const std::string& expression);
int  failureCount();

bool isNear(double a, double b, double epsilon = 0.01);

void protocolTests();
void streamFilterTests();
void penStateTests();
void jitterBufferTests();
void strokeSmootherTests();
void tabletEventTests();
void serverTransportTests();

} // namespace stylus_cast::test

// Not assert(): Release builds define NDEBUG, which would silently remove every check.
#define CHECK(expression) ::stylus_cast::test::check((expression), __FILE__, __LINE__, #expression)
