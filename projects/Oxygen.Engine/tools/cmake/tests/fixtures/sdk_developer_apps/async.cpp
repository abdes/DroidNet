#include <array>
#include <chrono>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>

#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/asio.h>

// Timers simulate asset latency; the coroutine scheduling and completion are real.
auto LoadLevel(asio::io_context& io) -> oxygen::co::Co<int>
{
  constexpr std::array stages {"Geometry", "Textures", "Audio", "Gameplay"};
  int completed = 0;
  for (const auto* stage : stages) {
    std::cout << "  Awaiting " << stage << "..." << std::flush;
    co_await oxygen::co::SleepFor(io, std::chrono::milliseconds(650));
    ++completed;
    std::cout << " ready (" << completed * 25 << "%)\n";
  }
  co_return completed;
}

int main(int argc, char** argv)
{
  try {
    const bool verify = argc > 1 && std::string_view(argv[1]) == "--verify";
    std::cout << "OXYGEN SDK | ASYNCHRONOUS LEVEL LOADING\n"
                 "OxCo coroutines + Oxygen's Asio integration\n"
                 "Simulated asset work; real timer suspension and completion.\n\n";
    do {
      asio::io_context io;
      const auto completed = oxygen::co::Run(io, LoadLevel(io));
      if (completed != 4) return 1;
      std::cout << "PASS: all four asynchronous stages completed; event loop returned.\n";
      if (verify) break;
      std::cout << "\nEnter to load again; q then Enter to quit: " << std::flush;
      std::string command;
      if (!std::getline(std::cin, command) || command == "q") break;
    } while (true);
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
