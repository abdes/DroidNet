//===----------------------------------------------------------------------===//
// Common Example Functions for OxCo Batch Processing
// Shared example implementations that work with any batch processor
//===----------------------------------------------------------------------===//

#pragma once

#include "OxCo/BatchExecution/Common/shared_utilities.h"
#include <iostream>
#include <optional>
#include <vector>

//===----------------------------------------------------------------------===//
// Forward Declarations
//===----------------------------------------------------------------------===//

namespace examples {

// Forward declaration
void PrintExceptionBehavior(const std::string& approach_name);

//===----------------------------------------------------------------------===//
// Example Test Cases
//===----------------------------------------------------------------------===//

// Example 1: Multiple result types
template <typename BatchProcessor>
void RunExample1(BatchProcessor& processor, const std::string& approach_name)
{
  std::cout << "\nExample 1: Multiple result types with " << approach_name
            << "\n";

  std::optional<int> first_even;
  std::vector<int> all_even_numbers;
  size_t count_greater_than_10;
  std::vector<size_t> indices_of_odd;
  int min_val, max_val;

  processor.ExecuteBatch([&](auto& p) {
    p.FindFirst(predicates::is_even, first_even);
    p.FindAll(predicates::is_even, all_even_numbers);
    p.Count(predicates::greater_than_10, count_greater_than_10);
    p.FindIndices(predicates::is_odd, indices_of_odd);
    p.FindMinMax(min_val, max_val);
  });

  std::cout << "\nResults populated in caller's containers:\n";
  if (first_even) {
    std::cout << "First even number: " << *first_even << "\n";
  } else {
    std::cout << "First even number: not found\n";
  }
  PrintVector(all_even_numbers, "All even numbers");
  std::cout << "Count > 10: " << count_greater_than_10 << "\n";
  PrintIndices(indices_of_odd, "Indices of odd numbers");
  std::cout << "Min: " << min_val << ", Max: " << max_val << "\n";
}

// Example 2: Prime number analysis
template <typename BatchProcessor>
void RunExample2(BatchProcessor& processor, const std::string& approach_name)
{
  std::cout << "\nExample 2: Prime number analysis with " << approach_name
            << "\n";

  std::vector<int> primes;
  std::vector<size_t> prime_indices;
  size_t prime_count;
  std::optional<int> first_prime_gt_10;

  processor.ExecuteBatch([&](auto& p) {
    p.FindAll(predicates::is_prime, primes);
    p.FindIndices(predicates::is_prime, prime_indices);
    p.Count(predicates::is_prime, prime_count);
    p.FindFirst(predicates::prime_gt_10, first_prime_gt_10);
  });

  std::cout << "\nPrime analysis results:\n";
  PrintVector(primes, "Prime numbers");
  PrintIndices(prime_indices, "Prime indices");
  std::cout << "Prime count: " << prime_count << "\n";
  if (first_prime_gt_10) {
    std::cout << "First prime > 10: " << *first_prime_gt_10 << "\n";
  } else {
    std::cout << "First prime > 10: not found\n";
  }
}

// Example 3: Range analysis
template <typename BatchProcessor>
void RunExample3(BatchProcessor& processor, const std::string& approach_name)
{
  std::cout << "\nExample 3: Range analysis with " << approach_name << "\n";

  size_t small_count, medium_count, large_count;
  std::vector<int> small_numbers, medium_numbers, large_numbers;

  processor.ExecuteBatch([&](auto& p) {
    p.Count(predicates::small_range, small_count);
    p.Count(predicates::medium_range, medium_count);
    p.Count(predicates::large_range, large_count);

    p.FindAll(predicates::small_range, small_numbers);
    p.FindAll(predicates::medium_range, medium_numbers);
    p.FindAll(predicates::large_range, large_numbers);
  });

  std::cout << "\nRange analysis results:\n";
  std::cout << "Small count (< 8): " << small_count << "\n";
  std::cout << "Medium count (8-14): " << medium_count << "\n";
  std::cout << "Large count (>= 15): " << large_count << "\n";

  PrintVector(small_numbers, "Small numbers");
  PrintVector(medium_numbers, "Medium numbers");
  PrintVector(large_numbers, "Large numbers");
}

// Example 4: Exception handling
template <typename BatchProcessor>
void RunExample4(BatchProcessor& processor, const std::string& approach_name)
{
  std::cout << "\nExample 4: Exception handling with " << approach_name << "\n";

  std::optional<int> result_before_exception;
  std::vector<int> all_results;
  size_t count_before_exception;

  try {
    processor.ExecuteBatch([&](auto& p) {
      // This should work fine for the first few elements
      p.FindFirst(predicates::greater_than_5, result_before_exception);

      // This will throw an exception on element with value 7 (index 3)
      p.FindAll(predicates::exception_on_7, all_results);

      // This should also work for elements before the exception
      p.Count(predicates::less_than_10, count_before_exception);
    });

    std::cout << "\nUnexpected: No exception was thrown!\n";
  } catch (const std::exception& e) {
    std::cout << "\nCaught exception: " << e.what() << "\n";
    PrintExceptionBehavior(approach_name);
  }

  std::cout << "\nResults from operations before exception:\n";
  if (result_before_exception) {
    std::cout << "First > 5: " << *result_before_exception << "\n";
  } else {
    std::cout << "First > 5: not found/failed\n";
  }
  PrintVector(all_results, "Even numbers collected before exception");
  std::cout << "Count < 10 before exception: " << count_before_exception
            << "\n";
}

// Print approach-specific exception behavior
inline void PrintExceptionBehavior(const std::string& approach_name)
{
  std::cout << approach_name << " behavior:\n";

  if (approach_name == "YieldAwaiter") {
    std::cout << "- Sequential processing stops at first exception\n";
    std::cout << "- All operations process elements in order together\n";
    std::cout << "- Results before exception: preserved\n";
    std::cout << "- Subsequent elements: not processed\n";
  } else if (approach_name == "BroadcastChannel") {
    std::cout << "- Operations that completed before exception: preserved\n";
    std::cout << "- Failing operation: terminated\n";
    std::cout << "- Other operations: may continue or be terminated\n";
  } else if (approach_name == "RepeatableShared") {
    std::cout
      << "- Element-wise coordination ensures consistent failure point\n";
    std::cout
      << "- All operations process same element when exception occurs\n";
    std::cout << "- Results before exception: preserved\n";
    std::cout << "- Exception propagates to main coroutine immediately\n";
  }
}

// Run all examples for a given processor
template <typename BatchProcessor>
void RunAllExamples(BatchProcessor& processor, const std::string& approach_name)
{
  RunExample1(processor, approach_name);
  RunExample2(processor, approach_name);
  RunExample3(processor, approach_name);
  RunExample4(processor, approach_name);
}

} // namespace examples
