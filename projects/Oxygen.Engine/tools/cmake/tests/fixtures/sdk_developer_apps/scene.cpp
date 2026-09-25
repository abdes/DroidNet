#include <cmath>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

#include <Oxygen/Scene/Scene.h>

int main(int argc, char** argv)
{
  try {
    const bool verify = argc > 1 && std::string_view(argv[1]) == "--verify";
    auto scene = std::make_shared<oxygen::scene::Scene>("Developer world", 32);
    auto player = scene->CreateNode("Player");
    auto camera = scene->CreateChildNode(player, "Follow camera");
    if (!camera || !camera->GetTransform().SetLocalPosition({0.0F, 2.0F, -5.0F})) {
      throw std::runtime_error("Could not create the camera hierarchy");
    }
    std::cout << "OXYGEN SDK | SCENE PLAYGROUND\n"
                 "Player -> Follow camera (local offset 0, 2, -5)\n"
                 "Oxygen computes the child world transform after each move.\n\n";
    float x = 0.0F;
    auto update = [&] {
      if (!player.GetTransform().SetLocalPosition({x, 0.0F, 0.0F})) {
        throw std::runtime_error("Player movement failed");
      }
      scene->Update();
      const auto world = camera->GetTransform().GetWorldPosition();
      if (!world || std::abs(world->x - x) > 0.001F
          || std::abs(world->y - 2.0F) > 0.001F
          || std::abs(world->z + 5.0F) > 0.001F) {
        throw std::runtime_error("Child world transform is incorrect");
      }
      std::cout << "Player x=" << x << "  |  Camera world=(" << world->x
                << ", " << world->y << ", " << world->z << ")  [verified]\n";
    };
    update();
    x = 10.0F;
    update();
    if (verify) {
      std::cout << "PASS: real Oxygen scene creation and parent/child transforms\n";
      return 0;
    }
    std::cout << "\nEnter a/d then Enter to move; q then Enter to quit.\n";
    for (std::string command; std::cout << "> " && std::getline(std::cin, command);) {
      if (command == "q") break;
      if (command == "a") x -= 1.0F;
      else if (command == "d") x += 1.0F;
      else { std::cout << "Use a, d, or q.\n"; continue; }
      update();
    }
    return 0;
  } catch (const std::exception& error) {
    std::cerr << "FAIL: " << error.what() << '\n';
    return 1;
  }
}
