//===----------------------------------------------------------------------===//
// YieldAwaiter Batch Query Example using OxCo
// Demonstrates batching multiple collection operations with custom YieldAwaiter
// and caller-provided result containers
//===----------------------------------------------------------------------===//

#include <coroutine>
#include <functional>
#include <iostream>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>

#include "OxCo/BatchExecution/Common.h"

using namespace oxygen::co;

//===----------------------------------------------------------------------===//
// Custom YieldAwaiter for control flow
//===----------------------------------------------------------------------===//

struct YieldAwaiter {
  BatchExecutionEventLoop* loop;

  bool await_ready() const noexcept { return false; }
  void await_suspend(std::coroutine_handle<> handle)
  {
    loop->Schedule([handle]() { handle.resume(); });
  }
  void await_resume() { }
};

//===----------------------------------------------------------------------===//
// Batch Processor with Caller Result Population
//===----------------------------------------------------------------------===//

class YieldAwaiterBatchProcessor {
public:
  explicit YieldAwaiterBatchProcessor(BatchExecutionEventLoop& loop)
    : loop_(loop)
    , collection_(kTestCollection)
  {
  }

  // Find first element matching predicate and store in provided optional
  template <typename Predicate>
  void FindFirst(Predicate predicate, std::optional<int>& result)
  {
    operations_.emplace_back([this, predicate, &result]() -> Co<> {
      std::cout << "  [FindFirst] Starting sequential search\n";
      result = std::nullopt;

      for (size_t i = 0; i < collection_.size(); ++i) {
        std::cout << "    [FindFirst] Checking element " << i << " ("
                  << collection_[i] << ")\n";

        if (predicate(collection_[i])) {
          std::cout << "    [FindFirst] Found match: " << collection_[i]
                    << "\n";
          result = collection_[i];
          co_return; // Early termination
        }

        // Yield between elements for interleaved processing
        co_await YieldAwaiter { &loop_ };
      }

      std::cout << "  [FindFirst] Completed - no match found\n";
      co_return;
    });
  }

  // Find all elements matching predicate and store in provided vector
  template <typename Predicate>
  void FindAll(Predicate predicate, std::vector<int>& results)
  {
    operations_.emplace_back([this, predicate, &results]() -> Co<> {
      std::cout << "  [FindAll] Starting complete search\n";
      results.clear();

      for (size_t i = 0; i < collection_.size(); ++i) {
        std::cout << "    [FindAll] Checking element " << i << " ("
                  << collection_[i] << ")\n";

        if (predicate(collection_[i])) {
          results.push_back(collection_[i]);
          std::cout << "    [FindAll] Added match: " << collection_[i]
                    << " (total: " << results.size() << ")\n";
        }

        // Yield between elements for interleaved processing
        co_await YieldAwaiter { &loop_ };
      }

      std::cout << "  [FindAll] Completed with " << results.size()
                << " matches\n";
      co_return;
    });
  }

  // Count elements matching predicate and store in provided reference
  template <typename Predicate> void Count(Predicate predicate, size_t& count)
  {
    operations_.emplace_back([this, predicate, &count]() -> Co<> {
      std::cout << "  [Count] Starting count operation\n";
      count = 0;

      for (size_t i = 0; i < collection_.size(); ++i) {
        std::cout << "    [Count] Checking element " << i << " ("
                  << collection_[i] << ")\n";

        if (predicate(collection_[i])) {
          count++;
          std::cout << "    [Count] Match found - count now: " << count << "\n";
        }

        // Yield between elements for interleaved processing
        co_await YieldAwaiter { &loop_ };
      }

      std::cout << "  [Count] Completed with count: " << count << "\n";
      co_return;
    });
  }

  // Find indices of matching elements
  template <typename Predicate>
  void FindIndices(Predicate predicate, std::vector<size_t>& indices)
  {
    operations_.emplace_back([this, predicate, &indices]() -> Co<> {
      std::cout << "  [FindIndices] Starting index search\n";
      indices.clear();

      for (size_t i = 0; i < collection_.size(); ++i) {
        std::cout << "    [FindIndices] Checking element " << i << " ("
                  << collection_[i] << ")\n";

        if (predicate(collection_[i])) {
          indices.push_back(i);
          std::cout << "    [FindIndices] Added index: " << i << "\n";
        }

        // Yield between elements for interleaved processing
        co_await YieldAwaiter { &loop_ };
      }

      std::cout << "  [FindIndices] Completed with " << indices.size()
                << " indices\n";
      co_return;
    });
  }

  // Get min and max values
  void FindMinMax(int& min_value, int& max_value)
  {
    operations_.emplace_back([this, &min_value, &max_value]() -> Co<> {
      std::cout << "  [FindMinMax] Starting min/max search\n";
      bool first = true;

      for (size_t i = 0; i < collection_.size(); ++i) {
        std::cout << "    [FindMinMax] Processing element " << i << " ("
                  << collection_[i] << ")\n";

        if (first) {
          min_value = max_value = collection_[i];
          first = false;
          std::cout << "    [FindMinMax] Initial min/max: " << collection_[i]
                    << "\n";
        } else {
          if (collection_[i] < min_value) {
            min_value = collection_[i];
            std::cout << "    [FindMinMax] New min: " << min_value << "\n";
          }
          if (collection_[i] > max_value) {
            max_value = collection_[i];
            std::cout << "    [FindMinMax] New max: " << max_value << "\n";
          }
        }

        // Yield between elements for interleaved processing
        co_await YieldAwaiter { &loop_ };
      }

      std::cout << "  [FindMinMax] Completed - min: " << min_value
                << ", max: " << max_value << "\n";
      co_return;
    });
  }

  // Execute all registered operations using YieldAwaiter approach
  void ExecuteBatch(
    std::function<void(YieldAwaiterBatchProcessor&)> batch_operations)
  {
    std::cout
      << "\n=== ExecuteBatch: Starting YieldAwaiter Batch Processing ===\n";
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
      << "=== ExecuteBatch: YieldAwaiter Batch Processing Completed ===\n";
  }

private:
  Co<> ExecuteBatchAsync()
  {
    std::cout << "Setting up nursery for YieldAwaiter batch operations\n";

    OXCO_WITH_NURSERY(nursery)
    {
      // Start a coroutine for each operation
      for (auto& operation : operations_) {
        nursery.Start([&operation]() -> Co<> {
          co_await operation();
          co_return;
        });
      }

      std::cout << "Waiting for all YieldAwaiter operations to complete\n";
      co_return kJoin; // Wait for all nursery tasks to finish
    };

    co_return;
  }

  BatchExecutionEventLoop& loop_;
  std::vector<int> collection_;
  std::vector<std::function<Co<>()>> operations_;
};

//===----------------------------------------------------------------------===//
// Example Usage
//===----------------------------------------------------------------------===//

extern "C" void MainImpl(std::span<const char*> /*args*/)
{
  std::cout << "=== YieldAwaiter Batch Processing with Result Population ===\n";
  std::cout << "This example demonstrates the YieldAwaiter approach where:\n";
  std::cout << "- Each operation runs as a separate coroutine\n";
  std::cout << "- Operations yield control using custom YieldAwaiter\n";
  std::cout << "- Results are populated in caller-provided containers\n";
  std::cout << "- Processing is interleaved but follows sequential pattern\n\n";

  BatchExecutionEventLoop loop;
  YieldAwaiterBatchProcessor processor(loop);

  // Run all shared examples
  examples::RunAllExamples(processor, "YieldAwaiter");

  std::cout << "\n=== YieldAwaiter Examples Completed Successfully ===\n";
  std::cout << "\nKey Characteristics of YieldAwaiter Approach:\n";
  std::cout << "- Sequential processing with explicit yielding\n";
  std::cout << "- Custom awaiter provides fine control over scheduling\n";
  std::cout
    << "- Each operation processes the entire collection independently\n";
  std::cout << "- Simple and predictable execution pattern\n";
  std::cout << "- Lower overhead compared to channel-based approaches\n";
}
