//===----------------------------------------------------------------------===//
// RepeatableShared Per-Item Processing Example using OxCo
// Demonstrates using RepeatableShared for sequential per-item batch processing
// Each item is processed by ALL operations before moving to the next item
//===----------------------------------------------------------------------===//

#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/EventLoop.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/RepeatableShared.h>
#include <Oxygen/OxCo/Run.h>

#include "OxCo/BatchExecution/Common.h"

using namespace oxygen::co;

//===----------------------------------------------------------------------===//
// RepeatableShared Batch Processor with Result Population
//===----------------------------------------------------------------------===//

class RepeatableSharedBatchProcessor {
public:
  explicit RepeatableSharedBatchProcessor(BatchExecutionEventLoop& loop)
    : loop_(loop)
    , collection_(kTestCollection)
  {
  }

  // Find first element matching predicate and store in provided optional
  template <typename Predicate>
  void FindFirst(Predicate predicate, std::optional<int>& result)
  {
    operations_.emplace_back([this, predicate, &result](
                               RepeatableShared<ElementData>& source) -> Co<> {
      std::cout << "  [FindFirst] Starting RepeatableShared search\n";
      result = std::nullopt;

      for (size_t i = 0; i < collection_.size(); ++i) {
        auto element = co_await source.Next();
        auto lock = co_await source.Lock();

        std::cout << "    [FindFirst] Checking element " << element.index
                  << " (" << element.value << ")\n";

        if (predicate(element.value)) {
          std::cout << "    [FindFirst] Found match: " << element.value << "\n";
          result = element.value;
          std::cout << "    [FindFirst] Terminating early - match found\n";
          break; // Early termination
        }

        if (element.is_last) {
          std::cout
            << "    [FindFirst] Reached last element - no match found\n";
          break;
        }
      }

      co_return;
    });
  }

  // Find all elements matching predicate and store in provided vector
  template <typename Predicate>
  void FindAll(Predicate predicate, std::vector<int>& results)
  {
    operations_.emplace_back([this, predicate, &results](
                               RepeatableShared<ElementData>& source) -> Co<> {
      std::cout << "  [FindAll] Starting RepeatableShared search\n";
      results.clear();

      for (size_t i = 0; i < collection_.size(); ++i) {
        auto element = co_await source.Next();
        auto lock = co_await source.Lock();

        std::cout << "    [FindAll] Checking element " << element.index << " ("
                  << element.value << ")\n";

        if (predicate(element.value)) {
          std::cout << "      [FindAll] Match found: " << element.value << "\n";
          results.push_back(element.value);
        }

        if (element.is_last) {
          std::cout << "    [FindAll] Finished - found " << results.size()
                    << " total matches\n";
          break;
        }
      }

      co_return;
    });
  }

  // Count elements matching predicate and store in provided reference
  template <typename Predicate> void Count(Predicate predicate, size_t& count)
  {
    operations_.emplace_back(
      [this, predicate, &count](RepeatableShared<ElementData>& source) -> Co<> {
        std::cout << "  [Count] Starting RepeatableShared count\n";
        count = 0;

        for (size_t i = 0; i < collection_.size(); ++i) {
          auto element = co_await source.Next();
          auto lock = co_await source.Lock();

          std::cout << "    [Count] Checking element " << element.index << " ("
                    << element.value << ")\n";

          if (predicate(element.value)) {
            std::cout << "      [Count] Match found: " << element.value << "\n";
            ++count;
          }

          if (element.is_last) {
            std::cout << "    [Count] Finished - count: " << count << "\n";
            break;
          }
        }

        co_return;
      });
  }

  // Find indices of matching elements
  template <typename Predicate>
  void FindIndices(Predicate predicate, std::vector<size_t>& indices)
  {
    operations_.emplace_back([this, predicate, &indices](
                               RepeatableShared<ElementData>& source) -> Co<> {
      std::cout << "  [FindIndices] Starting RepeatableShared index search\n";
      indices.clear();

      for (size_t i = 0; i < collection_.size(); ++i) {
        auto element = co_await source.Next();
        auto lock = co_await source.Lock();

        std::cout << "    [FindIndices] Checking element " << element.index
                  << " (" << element.value << ")\n";

        if (predicate(element.value)) {
          indices.push_back(element.index);
          std::cout << "    [FindIndices] Added index: " << element.index
                    << "\n";
        }

        if (element.is_last) {
          std::cout << "    [FindIndices] Finished - found " << indices.size()
                    << " matching indices\n";
          break;
        }
      }

      co_return;
    });
  }

  // Get min and max values and store in provided references
  void FindMinMax(int& min_value, int& max_value)
  {
    operations_.emplace_back([this, &min_value, &max_value](
                               RepeatableShared<ElementData>& source) -> Co<> {
      std::cout << "  [FindMinMax] Starting RepeatableShared min/max search\n";
      bool first = true;

      for (size_t i = 0; i < collection_.size(); ++i) {
        auto element = co_await source.Next();
        auto lock = co_await source.Lock();

        std::cout << "    [FindMinMax] Processing element " << element.index
                  << " (" << element.value << ")\n";

        if (first) {
          min_value = max_value = element.value;
          first = false;
          std::cout << "    [FindMinMax] Initial min/max: " << element.value
                    << "\n";
        } else {
          if (element.value < min_value) {
            min_value = element.value;
            std::cout << "    [FindMinMax] New min: " << min_value << "\n";
          }
          if (element.value > max_value) {
            max_value = element.value;
            std::cout << "    [FindMinMax] New max: " << max_value << "\n";
          }
        }

        if (element.is_last) {
          std::cout << "    [FindMinMax] Finished - min: " << min_value
                    << ", max: " << max_value << "\n";
          break;
        }
      }

      co_return;
    });
  }

  // Execute all registered operations using RepeatableShared
  void ExecuteBatch(
    std::function<void(RepeatableSharedBatchProcessor&)> batch_operations)
  {
    std::cout
      << "\n=== ExecuteBatch: Starting RepeatableShared Batch Processing ===\n";
    PrintCollection(collection_);

    operations_.clear();

    // Register operations via the lambda
    batch_operations(*this);

    if (operations_.empty()) {
      std::cout << "No operations registered\n";
      return;
    }

    std::cout << "Registered " << operations_.size() << " operations\n";

    // Run the async implementation
    oxygen::co::Run(loop_, ExecuteBatchAsync());

    // Clear operations after execution
    operations_.clear();
    std::cout
      << "=== ExecuteBatch: RepeatableShared Batch Processing Completed ===\n";
  }

private:
  Co<> ExecuteBatchAsync()
  {
    std::cout << "Setting up RepeatableShared element source\n";

    // Create RepeatableShared for element distribution
    size_t current_index = 0;
    RepeatableShared<ElementData> element_source { [this, &current_index]()
                                                     -> Co<ElementData> {
      if (current_index >= collection_.size()) {
        // This shouldn't happen if operations respect is_last
        std::cout
          << "  [ElementSource] Warning: Requested element beyond collection\n";
        co_return ElementData(0, current_index, true);
      }

      auto element = ElementData(collection_[current_index], current_index,
        current_index == collection_.size() - 1);

      std::cout << "\n--- RepeatableShared providing element " << current_index
                << " (" << element.value << ")"
                << (element.is_last ? " [LAST]" : "") << " ---\n";

      ++current_index;
      co_return element;
    } };

    OXCO_WITH_NURSERY(nursery)
    {
      // Start a coroutine for each operation
      std::cout << "Starting " << operations_.size()
                << " RepeatableShared operations\n";
      for (auto& operation : operations_) {
        nursery.Start([&operation, &element_source]() -> Co<> {
          co_await operation(element_source);
          co_return;
        });
      }

      std::cout << "Waiting for all RepeatableShared operations to complete\n";
      co_return kJoin; // Wait for all nursery tasks to finish
    };

    co_return;
  }

  BatchExecutionEventLoop& loop_;
  std::vector<int> collection_;
  std::vector<std::function<Co<>(RepeatableShared<ElementData>&)>> operations_;
};

//===----------------------------------------------------------------------===//
// Example Usage
//===----------------------------------------------------------------------===//

extern "C" void MainImpl(std::span<const char*> /*args*/)
{
  std::cout
    << "=== RepeatableShared Batch Processing with Result Population ===\n";
  std::cout
    << "This example demonstrates the RepeatableShared approach where:\n";
  std::cout
    << "- Each element is processed by ALL operations before moving to next\n";
  std::cout
    << "- Operations run concurrently but are synchronized per-element\n";
  std::cout << "- Processing ensures sequential per-item coordination\n";
  std::cout << "- Results are populated in caller-provided containers\n";
  std::cout << "- Built-in RepeatableShared synchronization primitives\n\n";

  BatchExecutionEventLoop loop;
  RepeatableSharedBatchProcessor processor(loop);

  // Run all shared examples
  examples::RunAllExamples(processor, "RepeatableShared");

  std::cout << "\n=== RepeatableShared Examples Completed Successfully ===\n";
  std::cout << "\nKey Characteristics of RepeatableShared Approach:\n";
  std::cout
    << "- Sequential per-item processing with element-wise coordination\n";
  std::cout
    << "- Each element processed by ALL operations before next element\n";
  std::cout
    << "- Built-in OxCo synchronization primitives (RepeatableShared)\n";
  std::cout << "- Simpler than BroadcastChannel but more coordinated than "
               "YieldAwaiter\n";
  std::cout << "- Natural support for per-element synchronization\n";
  std::cout << "- Ensures ordered processing while maintaining concurrency\n";
}
