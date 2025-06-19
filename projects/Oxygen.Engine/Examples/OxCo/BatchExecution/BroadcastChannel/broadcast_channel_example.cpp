//===----------------------------------------------------------------------===//
// BroadcastChannel Batch Query Example using OxCo
// Demonstrates using OxCo's BroadcastChannel for batch processing
// with caller-provided result containers
//===----------------------------------------------------------------------===//

#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <Oxygen/OxCo/BroadcastChannel.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/EventLoop.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/Yield.h>

#include "../Common.h"

using namespace oxygen::co;
using namespace oxygen::co::detail::channel;

//===----------------------------------------------------------------------===//
// BroadcastChannel Batch Processor with Result Population
//===----------------------------------------------------------------------===//

class BroadcastChannelBatchProcessor {
public:
  explicit BroadcastChannelBatchProcessor(BatchExecutionEventLoop& loop)
    : loop_(loop)
    , collection_(kTestCollection)
  {
  }

  // Find first element matching predicate and store in provided optional
  template <typename Predicate>
  void FindFirst(Predicate predicate, std::optional<int>& result)
  {
    operations_.emplace_back([this, predicate, &result](
                               BroadcastChannel<ElementData>& channel) -> Co<> {
      std::cout << "  [FindFirst] Starting BroadcastChannel search\n";
      auto reader = channel.ForRead();
      result = std::nullopt;

      while (true) {
        auto element = co_await reader.Receive();
        if (!element) {
          std::cout << "  [FindFirst] Channel closed - no match found\n";
          break;
        }

        std::cout << "    [FindFirst] Checking element " << element->index
                  << " (" << element->value << ")\n";

        if (predicate(element->value)) {
          std::cout << "    [FindFirst] Found match: " << element->value
                    << "\n";
          result = element->value;
          break; // Early termination
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
                               BroadcastChannel<ElementData>& channel) -> Co<> {
      std::cout << "  [FindAll] Starting BroadcastChannel search\n";
      auto reader = channel.ForRead();
      results.clear();

      while (true) {
        auto element = co_await reader.Receive();
        if (!element) {
          std::cout << "  [FindAll] Channel closed - found " << results.size()
                    << " total matches\n";
          break;
        }

        std::cout << "    [FindAll] Checking element " << element->index << " ("
                  << element->value << ")\n";

        if (predicate(element->value)) {
          results.push_back(element->value);
          std::cout << "    [FindAll] Added match: " << element->value
                    << " (total: " << results.size() << ")\n";
        }
      }

      co_return;
    });
  }

  // Count elements matching predicate and store in provided reference
  template <typename Predicate> void Count(Predicate predicate, size_t& count)
  {
    operations_.emplace_back([this, predicate, &count](
                               BroadcastChannel<ElementData>& channel) -> Co<> {
      std::cout << "  [Count] Starting BroadcastChannel count\n";
      auto reader = channel.ForRead();
      count = 0;

      while (true) {
        auto element = co_await reader.Receive();
        if (!element) {
          std::cout << "  [Count] Channel closed - final count: " << count
                    << "\n";
          break;
        }

        std::cout << "    [Count] Checking element " << element->index << " ("
                  << element->value << ")\n";

        if (predicate(element->value)) {
          count++;
          std::cout << "    [Count] Match found - count now: " << count << "\n";
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
                               BroadcastChannel<ElementData>& channel) -> Co<> {
      std::cout << "  [FindIndices] Starting BroadcastChannel index search\n";
      auto reader = channel.ForRead();
      indices.clear();

      while (true) {
        auto element = co_await reader.Receive();
        if (!element) {
          std::cout << "  [FindIndices] Channel closed - found "
                    << indices.size() << " matching indices\n";
          break;
        }

        std::cout << "    [FindIndices] Checking element " << element->index
                  << " (" << element->value << ")\n";

        if (predicate(element->value)) {
          indices.push_back(element->index);
          std::cout << "    [FindIndices] Added index: " << element->index
                    << "\n";
        }
      }

      co_return;
    });
  }

  // Get min and max values and store in provided references
  void FindMinMax(int& min_value, int& max_value)
  {
    operations_.emplace_back([this, &min_value, &max_value](
                               BroadcastChannel<ElementData>& channel) -> Co<> {
      std::cout << "  [FindMinMax] Starting BroadcastChannel min/max search\n";
      auto reader = channel.ForRead();
      bool first = true;

      while (true) {
        auto element = co_await reader.Receive();
        if (!element) {
          std::cout << "  [FindMinMax] Channel closed - min: " << min_value
                    << ", max: " << max_value << "\n";
          break;
        }

        std::cout << "    [FindMinMax] Processing element " << element->index
                  << " (" << element->value << ")\n";

        if (first) {
          min_value = max_value = element->value;
          first = false;
          std::cout << "    [FindMinMax] Initial min/max: " << element->value
                    << "\n";
        } else {
          if (element->value < min_value) {
            min_value = element->value;
            std::cout << "    [FindMinMax] New min: " << min_value << "\n";
          }
          if (element->value > max_value) {
            max_value = element->value;
            std::cout << "    [FindMinMax] New max: " << max_value << "\n";
          }
        }
      }
      co_return;
    });
  }

  // Execute all registered operations using BroadcastChannel
  void ExecuteBatch(
    std::function<void(BroadcastChannelBatchProcessor&)> batch_operations)
  {
    std::cout
      << "\n=== ExecuteBatch: Starting BroadcastChannel Batch Processing ===\n";
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
      << "=== ExecuteBatch: BroadcastChannel Batch Processing Completed ===\n";
  }

private:
  auto ExecuteBatchAsync() -> Co<>
  {
    std::cout << "Setting up nursery and BroadcastChannel\n";

    // Create broadcast channel for element distribution
    BroadcastChannel<ElementData> element_channel;
    auto& writer = element_channel.ForWrite();

    OXCO_WITH_NURSERY(nursery)
    {
      // Start a coroutine for each operation - each manages its own reader
      std::cout << "Starting " << operations_.size()
                << " BroadcastChannel operations\n";
      for (auto& operation : operations_) {
        nursery.Start([&operation, &element_channel]() -> Co<> {
          co_await operation(element_channel);
          co_return;
        });
      }

      // Start the collection traversal coroutine
      nursery.Start([this, &writer]() -> Co<> {
        co_await TraverseCollection(writer);
        co_return;
      });

      std::cout << "Waiting for all BroadcastChannel operations to complete\n";
      co_return kJoin; // Wait for all nursery tasks to finish
    };

    co_return;
  }

  auto TraverseCollection(BroadcastingWriter<ElementData>& writer) -> Co<>
  {
    std::cout << "  [Async] Starting BroadcastChannel traversal (size: "
              << collection_.size() << ")\n";

    // Send each element through the broadcast channel
    for (size_t i = 0; i < collection_.size(); ++i) {
      bool is_last = (i == collection_.size() - 1);
      ElementData element(collection_[i], i, is_last);

      std::cout << "\n  Broadcasting element " << i << ": " << element.value
                << "\n";

      // Send element to all operations via broadcast channel
      bool sent = co_await writer.Send(std::move(element));

      if (!sent) {
        std::cout << "  [Async] Failed to send element (channel closed)\n";
        break;
      }

      // Yield to allow operations to process this element interleaved
      co_await Yield {};
    }

    // Close the channel to signal completion
    writer.Close();
    std::cout
      << "  [Async] BroadcastChannel traversal completed, channel closed\n";
    co_return;
  }

  BatchExecutionEventLoop& loop_;
  std::vector<int> collection_;
  std::vector<std::function<Co<>(BroadcastChannel<ElementData>&)>> operations_;
};

//===----------------------------------------------------------------------===//
// Example Usage
//===----------------------------------------------------------------------===//

extern "C" void MainImpl(std::span<const char*> /*args*/)
{
  std::cout
    << "=== BroadcastChannel Batch Processing with Result Population ===\n";
  std::cout
    << "This example demonstrates the BroadcastChannel approach where:\n";
  std::cout << "- Elements are broadcast to all operations simultaneously\n";
  std::cout
    << "- Each operation runs as independent coroutine with own reader\n";
  std::cout << "- Operations process elements concurrently via channel\n";
  std::cout << "- Results are populated in caller-provided containers\n";
  std::cout << "- Processing is truly parallel and interleaved\n\n";

  BatchExecutionEventLoop loop;
  BroadcastChannelBatchProcessor processor(loop);

  // Run all shared examples
  examples::RunAllExamples(processor, "BroadcastChannel");

  std::cout << "\n=== BroadcastChannel Examples Completed Successfully ===\n";
  std::cout << "\nKey Characteristics of BroadcastChannel Approach:\n";
  std::cout << "- Parallel processing via broadcast channel communication\n";
  std::cout << "- Each operation has independent execution flow\n";
  std::cout << "- Elements are broadcast once and received by all operations\n";
  std::cout
    << "- Built-in OxCo synchronization primitives (BroadcastChannel)\n";
  std::cout << "- More scalable for complex operations and larger datasets\n";
  std::cout << "- Natural support for early termination via channel closure\n";
}
