//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once
#pragma managed(push, off)

#include <functional>
#include <mutex>
#include <vector>

namespace oxygen::interop::module {

  //! A thread-safe queue for passing items between threads.
  /*!
   Uses a double-buffering strategy or simple mutex protection to allow
   safe enqueueing from multiple threads and draining from a single consumer
   thread.
  */
  template <typename T> class ThreadSafeQueue {
  public:
    using Consumer = std::function<void(T&)>;

    //! Enqueues an item into the queue. Thread-safe.
    void Enqueue(T item) {
      std::lock_guard<std::mutex> lock(mutex_);
      items_.push_back(std::move(item));
    }

    //! Drains the queue, calling the consumer for each item.
    /*!
     This method swaps the internal buffer to minimize lock contention time.
     The consumer is called outside the lock.
    */
    void Drain(Consumer consumer) {
      std::vector<T> current_batch;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (items_.empty()) {
          return;
        }
        current_batch.swap(items_);
      }

      for (auto& item : current_batch) {
        consumer(item);
      }
    }

    //! Drains only items matching the provided predicate, preserving the
    //! insertion order of remaining items. The consumer is invoked for each
    //! matched item outside of the lock.
    void DrainIf(std::function<bool(const T&)> predicate, Consumer consumer) {
      std::vector<T> current_batch;
      {
        std::lock_guard<std::mutex> lock(mutex_);
        if (items_.empty()) {
          return;
        }
        current_batch.swap(items_);
      }

      std::vector<T> remaining;
      remaining.reserve(current_batch.size());

      for (auto& item : current_batch) {
        if (predicate(item)) {
          consumer(item);
        }
        else {
          remaining.push_back(std::move(item));
        }
      }

      // Put remaining items back into the queue, preserving relative order
      // and appending any entries that were enqueued while we were processing.
      std::lock_guard<std::mutex> lock(mutex_);
      if (!remaining.empty()) {
        // Prepend remaining then append new items that arrived while we processed
        std::vector<T> new_items;
        new_items.reserve(remaining.size() + items_.size());
        for (auto& r : remaining)
          new_items.push_back(std::move(r));
        for (auto& i : items_)
          new_items.push_back(std::move(i));
        items_.swap(new_items);
      }
    }

    //! Clears the queue.
    void Clear() {
      std::lock_guard<std::mutex> lock(mutex_);
      items_.clear();
    }

    //! Checks if the queue is empty.
    bool IsEmpty() const {
      std::lock_guard<std::mutex> lock(mutex_);
      return items_.empty();
    }

  private:
    std::vector<T> items_;
    mutable std::mutex mutex_;
  };

} // namespace oxygen::interop::module

#pragma managed(pop)
