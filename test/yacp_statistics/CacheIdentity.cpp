#include <functional>
#include <iostream>
#include <string>

// Compile with the simulator's native compiler. std::hash is platform-specific;
// these identifiers must never be reused for an ESP32 migration.
int main(int argc, char** argv) {
  for (int i = 1; i < argc; ++i) {
    std::cout << "epub_" << std::hash<std::string>{}(argv[i]) << '\n';
  }
}
