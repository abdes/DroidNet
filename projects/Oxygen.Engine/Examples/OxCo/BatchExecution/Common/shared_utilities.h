//===----------------------------------------------------------------------===//
// Shared Utilities for OxCo Batch Processing Examples
// Common data structures, utilities, and predicates used across all examples
//===----------------------------------------------------------------------===//

#pragma once

#include <iostream>
#include <string>
#include <vector>

//===----------------------------------------------------------------------===//
// Common Data Structures
//===----------------------------------------------------------------------===//

// Element data structure used across all examples
struct ElementData {
  int value;
  size_t index;
  bool is_last;

  ElementData(int v, size_t i, bool last = false)
    : value(v)
    , index(i)
    , is_last(last)
  {
  }
};

// Common test collection used across all examples
inline const std::vector<int> kTestCollection { 1, 3, 5, 7, 8, 9, 12, 15, 18,
  20 };

//===----------------------------------------------------------------------===//
// Utility Functions
//===----------------------------------------------------------------------===//

// Print vector contents with a label
inline void PrintVector(const std::vector<int>& vec, const std::string& name)
{
  std::cout << name << ": [";
  for (size_t i = 0; i < vec.size(); ++i) {
    if (i > 0)
      std::cout << ", ";
    std::cout << vec[i];
  }
  std::cout << "]\n";
}

// Print index vector contents with a label
inline void PrintIndices(
  const std::vector<size_t>& indices, const std::string& name)
{
  std::cout << name << ": [";
  for (size_t i = 0; i < indices.size(); ++i) {
    if (i > 0)
      std::cout << ", ";
    std::cout << indices[i];
  }
  std::cout << "]\n";
}

// Print collection contents with a label
inline void PrintCollection(const std::vector<int>& collection)
{
  std::cout << "Collection: [";
  for (size_t i = 0; i < collection.size(); ++i) {
    if (i > 0)
      std::cout << ", ";
    std::cout << collection[i];
  }
  std::cout << "]\n";
}

//===----------------------------------------------------------------------===//
// Common Predicates and Lambda Functions
//===----------------------------------------------------------------------===//

namespace predicates {

// Prime number test
inline auto is_prime = [](int x) {
  if (x < 2)
    return false;
  for (int i = 2; i * i <= x; ++i) {
    if (x % i == 0)
      return false;
  }
  return true;
};

// Even number test
inline auto is_even = [](int x) { return x % 2 == 0; };

// Odd number test
inline auto is_odd = [](int x) { return x % 2 == 1; };

// Greater than 10 test
inline auto greater_than_10 = [](int x) { return x > 10; };

// Greater than 5 test
inline auto greater_than_5 = [](int x) { return x > 5; };

// Range predicates
inline auto small_range = [](int x) { return x < 8; };
inline auto medium_range = [](int x) { return x >= 8 && x < 15; };
inline auto large_range = [](int x) { return x >= 15; };

// Less than 10 test
inline auto less_than_10 = [](int x) { return x < 10; };

// Prime greater than 10 test
inline auto prime_gt_10 = [](int x) { return x > 10 && is_prime(x); };

// Exception throwing predicate for testing error handling
inline auto exception_on_7 = [](int x) {
  if (x == 7)
    throw std::runtime_error("Predicate exception on value 7");
  return x % 2 == 0;
};

} // namespace predicates
