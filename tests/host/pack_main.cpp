#include "test_util.hpp"
void RunPackTests();
void RunPackDiscoveryTests();
int main() {
  RunPackTests();
  RunPackDiscoveryTests();
  if (orcmap::test::g_failures) return 1;
  std::puts("PACK_VALIDATION_DISCOVERY_PASS");
}
