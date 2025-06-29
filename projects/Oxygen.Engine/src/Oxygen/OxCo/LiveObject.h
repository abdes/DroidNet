//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#pragma once

#include <cassert>

#include <Oxygen/Base/Macros.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>

namespace oxygen::co {

//! Represents an object with a coroutine nursery, which needs to be
//! __activated__ for the nursery to be open. A live object does not do anything
//! meaningful unless it is __activated__ and its other methods are called to
//! start its async operations.
/*!
 Frequently, it is useful to have a nursery that is effectively 'associated
 with' a particular instance of a class, and thus can supervise tasks that
 provide functionality for the object it's associated with.

 Instead of requiring the user to pass a nursery to each task, the object
 follows a two-phase activation model, where the nursery is set up once and
 exposed to other methods to use later as needed.

 __Activation__ of the `LiveObject` is done by calling `ActivateAsync()` method,
 which is where the nursery is opened and stashed in a data member. For most
 cases, this can be as simple as:

 ```cpp
 auto MyLiveClass::ActivateAsync(co::TaskStarted<> started) -> co::Co<>
 {
     return OpenNursery(nursery_, std::move(started));
 }
 ```

 `OpenNursery()` ensures that the nursery pointer will be cleared when the
 nursery is actually closed (i.e., when its last task exits), not merely when
 the cancellation request is received by the `OpenNursery()` task. That means
 any tasks still running in the nursery during cleanup may continue to use the
 nursery pointer to start additional tasks to perform whatever operations are
 needed in order to cleanly shut down. Such tasks will begin execution in an
 already-cancelled state (so they probably want to make careful use of
 `co::NonCancellable()` or `co::UntilCancelledAnd()`), but in many cases that's
 preferable to not being able to start them at all.

 Using such a class typically involves opening a nursery, and submitting the
 `ActivateAsync()` task to run there in the background:

 ```cpp
 MyLiveClass obj;
 OXCO_WITH_NURSERY(n) {
     co_await n.Start(&MyLiveClass::ActivateAsync, &obj);
     obj.Run(); // Start background tasks

     // Now the object can be used in the remainder of the nursery block to do
     // other things as needed

     co_return co::kJoin; // Wait for all tasks to finish, including the ones
                          // started when `obj.Run()` was called
 };
 ```

 Note the use of the suspending version of `Nursery::Start()` here; this ensures
 that `obj.Run()` will not be called before `ActivateAsync()` starts executing,
 which is important because such a call would try to submit a task into a
 `nullptr` nursery. This is also the common pattern used for starting an y
 background tasks in the `LiveObject` by calling `Run()` immediately after
 `ActivateAsync()`.

 A `LiveObject` is considered to be __running__ for as long as its nursery is
 open. The nurserry will be closed when the last task in it exits. Explicit
 cancellation of the nursery can be triggered by calling `Stop()` on the
 `LiveObject`, which will cause all tasks in the nursery to be cancelled. The
 nursery will be closed when all tasks have exited.

 \note It is not strictly required to call `Stop()` on a `LiveObject`.
 Cancellation of the parent nursery from which the `LiveObject` was
 __activated__ will result in the cancellation of the `LiveObject` nursery as
 well, and the `LiveObject` will be closed when all tasks in it have exited.
*/
class LiveObject {
public:
  LiveObject() = default;
  virtual ~LiveObject() = default;

  OXYGEN_MAKE_NON_COPYABLE(LiveObject);
  OXYGEN_DEFAULT_MOVABLE(LiveObject);

  [[nodiscard]] virtual auto ActivateAsync(co::TaskStarted<> started = {})
    -> co::Co<>
    = 0;

  virtual void Run() { }

  virtual void Stop() = 0;

  [[nodiscard]] virtual auto IsRunning() const -> bool = 0;
};

} // namespace oxygen::co
