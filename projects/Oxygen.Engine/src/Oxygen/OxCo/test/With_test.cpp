//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <chrono>
#include <stdexcept>
#include <utility>

#include "Utils/OxCoTestFixture.h"
#include "Utils/TestEventLoop.h"

#include <Oxygen/OxCo/Algorithms.h>
#include <Oxygen/OxCo/Awaitables.h>
#include <Oxygen/OxCo/Co.h>
#include <Oxygen/OxCo/Nursery.h>
#include <Oxygen/OxCo/Run.h>
#include <Oxygen/OxCo/SuspendForever.h>
#include <Oxygen/OxCo/With.h>
#include <Oxygen/Testing/GTest.h>

using namespace std::chrono_literals;
using oxygen::co::AnyOf;
using oxygen::co::Co;
using oxygen::co::kJoin;
using oxygen::co::kSuspendForever;
using oxygen::co::kYield;
using oxygen::co::NoOp;
using oxygen::co::SuspendForever;
using oxygen::co::TaskStarted;
using oxygen::co::Then;
using oxygen::co::UntilCancelledAnd;
using oxygen::co::With;
using oxygen::co::testing::kNonCancellable;
using oxygen::co::testing::OxCoTestFixture;

namespace {

//! Checks that numbered stages run in order.
class StageChecker {
public:
  void Require(const int stage) { EXPECT_EQ(counter_++, stage); }

private:
  int counter_ { 0 };
};

class WithTest : public OxCoTestFixture {
protected:
  [[nodiscard]] auto Stage() -> StageChecker& { return stage_; }

private:
  StageChecker stage_;
};

//! The value passed to `started()` reaches the body, the body's result is the
//! result of `With`, and the first stage's cleanup runs after the body.
NOLINT_TEST_F(WithTest, Smoke)
{
  oxygen::co::Run(*el_, [this] -> Co<> {
    const int x = co_await With(
      [this](TaskStarted<int> started) -> Co<> {
        Stage().Require(0);
        co_await el_->Sleep(1ms);
        Stage().Require(1);
        started(42);
        co_await UntilCancelledAnd([this] -> Co<> {
          Stage().Require(3);
          co_await el_->Sleep(1ms);
        });
      },
      [this](const int v) -> Co<int> {
        Stage().Require(2);
        EXPECT_EQ(v, 42);
        co_await el_->Sleep(1ms);
        co_return 43;
      });

    EXPECT_EQ(x, 43);
    Stage().Require(4);
    EXPECT_EQ(el_->Now(), 3ms);
  });
}

//! Cancelling a with-scope cancels the body first; the first stage's cleanup
//! starts only after the body's asynchronous cleanup has finished.
NOLINT_TEST_F(WithTest, SmokeCancel)
{
  oxygen::co::Run(*el_, [this] -> Co<> {
    OXCO_WITH_NURSERY(n)
    {
      auto bg = [this](TaskStarted<> started) -> Co<> {
        Stage().Require(0);
        co_await el_->Sleep(1ms);
        Stage().Require(1);
        started();
        co_await UntilCancelledAnd([this] -> Co<> {
          Stage().Require(5);
          co_await el_->Sleep(1ms);
        });
      };
      OXCO_WITH(bg)
      {
        Stage().Require(2);
        co_await el_->Sleep(1ms);
        co_await AnyOf(UntilCancelledAnd([this] -> Co<> {
          Stage().Require(3);
          co_await el_->Sleep(1ms);
          Stage().Require(4);
        }),
          [&n] -> Co<> {
            n.Cancel();
            co_await kYield;
            ADD_FAILURE() << "should not be reached";
          });
      };

      ADD_FAILURE() << "should not be reached";
      co_return kJoin;
    };
    Stage().Require(6);
    EXPECT_EQ(el_->Now(), 4ms);
  });
}

//! The first stage signals readiness synchronously, before `With` suspends.
NOLINT_TEST_F(WithTest, ImmediatelyReady)
{
  oxygen::co::Run(*el_, [this] -> Co<> {
    co_await With(
      [this](TaskStarted<> started) -> auto {
        Stage().Require(0);
        started();
        return SuspendForever {};
      },
      [this] -> Co<> {
        Stage().Require(1);
        co_await el_->Sleep(1ms);
      });
    Stage().Require(2);
  });
}

//! Same as above, but the body is immediately ready too.
NOLINT_TEST_F(WithTest, BothImmediatelyReady)
{
  oxygen::co::Run(*el_, [this] -> Co<> {
    co_await With(
      [this](TaskStarted<> started) -> auto {
        Stage().Require(0);
        started();
        return SuspendForever {};
      },
      [this] -> auto {
        Stage().Require(1);
        return NoOp();
      });
    Stage().Require(2);
  });
}

//! A cancellation pending on the first stage when it signals readiness
//! prevents the body from ever running.
NOLINT_TEST_F(WithTest, PendingCancel)
{
  oxygen::co::Run(*el_, [this] -> Co<> {
    co_await AnyOf(el_->Sleep(1ms),
      With(
        [this](TaskStarted<> started) -> Co<> {
          Stage().Require(0);
          co_await el_->Sleep(2ms, kNonCancellable);
          Stage().Require(1);
          started();
          co_await kSuspendForever;
        },
        [] -> auto {
          ADD_FAILURE() << "should not be reached";
          return NoOp();
        }));
    Stage().Require(2);
  });
}

//! If both stages are immediately cancellable, so is `With`.
NOLINT_TEST_F(WithTest, ImmediateCancel)
{
  oxygen::co::Run(*el_, [this] -> Co<> {
    co_await AnyOf(el_->Sleep(1ms), [this] -> Co<> {
      co_await el_->Sleep(2ms, kNonCancellable);
      co_await With(
        [this](TaskStarted<> /*started*/) -> auto {
          return el_->Sleep(1ms) | Then([] -> auto {
            ADD_FAILURE() << "should not be reached";
            return NoOp();
          });
        },
        [this] -> auto {
          ADD_FAILURE() << "should not be reached";
          return el_->Sleep(1ms);
        });
    });
    EXPECT_EQ(el_->Now(), 2ms);
  });
}

//! A first stage that is not immediately cancellable makes `With` reject an
//! early cancellation; the body never runs.
NOLINT_TEST_F(WithTest, ImmediateCancelRejected)
{
  oxygen::co::Run(*el_, [this] -> Co<> {
    co_await AnyOf(el_->Sleep(1ms), [this] -> Co<> {
      co_await el_->Sleep(2ms, kNonCancellable);
      Stage().Require(0);
      co_await With(
        [this](TaskStarted<> /*started*/) -> Co<> {
          co_await el_->Sleep(1ms, kNonCancellable);
          Stage().Require(1);
          co_await kSuspendForever;
        },
        [this] -> Co<> {
          ADD_FAILURE() << "should not be reached";
          co_await el_->Sleep(1ms);
        });
    });
    Stage().Require(2);
    EXPECT_EQ(el_->Now(), 3ms);
  });
}

//! Regression (corral fcec393): an external cancellation arriving after the
//! body completed, while the first stage still runs, must wait for the first
//! stage instead of being confirmed immediately.
NOLINT_TEST_F(WithTest, CompleteCancel)
{
  oxygen::co::Run(*el_, [this] -> Co<> {
    co_await AnyOf(
      [this] -> Co<> {
        co_await el_->Sleep(2ms);
        Stage().Require(2);
      },
      With(
        [this](TaskStarted<> started) -> Co<> {
          Stage().Require(0);
          started();
          co_await el_->Sleep(3ms, kNonCancellable);
          Stage().Require(3);
          co_await kYield; // cancellation point
        },
        [this] -> Co<> {
          Stage().Require(1);
          co_await el_->Sleep(1ms);
        }));
    EXPECT_EQ(el_->Now(), 3ms);
  });
}

//! Readiness signalled synchronously while the first stage is being cancelled
//! does not start the body.
NOLINT_TEST_F(WithTest, ReadyOnCancel)
{
  oxygen::co::Run(*el_, [this] -> Co<> {
    co_await AnyOf(el_->Sleep(1ms),
      With(
        [this](TaskStarted<> started) -> auto {
          Stage().Require(0);
          return UntilCancelledAnd(
            NoOp() | Then([this, started = std::move(started)] mutable -> auto {
              Stage().Require(1);
              started();
              return NoOp();
            }));
        },
        [] -> auto {
          ADD_FAILURE() << "should not be reached";
          return NoOp();
        }));
    Stage().Require(2);
  });
}

//! The `OXCO_WITH` block form receives the value passed to `started()`.
NOLINT_TEST_F(WithTest, MacroForm)
{
  oxygen::co::Run(*el_, [this] -> Co<> {
    auto bg = [this](TaskStarted<int> started) -> Co<> {
      Stage().Require(0);
      started(7);
      co_await kSuspendForever;
    };
    OXCO_WITH(bg, const int v)
    {
      Stage().Require(1);
      EXPECT_EQ(v, 7);
      co_await el_->Sleep(1ms);
    };
    Stage().Require(2);
    EXPECT_EQ(el_->Now(), 1ms);
  });
}

//! An exception thrown by the first stage before `started()` propagates, and
//! the body never runs.
NOLINT_TEST_F(WithTest, FirstThrowsBeforeStarted)
{
  oxygen::co::Run(*el_, [this] -> Co<> {
    bool caught = false;
    try {
      co_await With(
        [this](TaskStarted<> /*started*/) -> Co<> {
          co_await el_->Sleep(1ms);
          throw std::runtime_error("first failed");
        },
        [] -> auto {
          ADD_FAILURE() << "should not be reached";
          return NoOp();
        });
    } catch (const std::runtime_error&) {
      caught = true;
    }
    EXPECT_TRUE(caught);
    EXPECT_EQ(el_->Now(), 1ms);
  });
}

//! An exception thrown by the first stage while the body runs cancels the
//! body, then propagates.
NOLINT_TEST_F(WithTest, FirstThrowsWhileBodyRuns)
{
  oxygen::co::Run(*el_, [this] -> Co<> {
    bool caught = false;
    bool body_cancelled = false;
    try {
      co_await With(
        [this](TaskStarted<> started) -> Co<> {
          started();
          co_await el_->Sleep(1ms);
          throw std::runtime_error("first failed");
        },
        [&body_cancelled] -> Co<> {
          co_await UntilCancelledAnd([&body_cancelled] -> Co<> {
            body_cancelled = true;
            co_return;
          });
        });
    } catch (const std::runtime_error&) {
      caught = true;
    }
    EXPECT_TRUE(caught);
    EXPECT_TRUE(body_cancelled);
    EXPECT_EQ(el_->Now(), 1ms);
  });
}

//! An exception thrown by the body propagates after the first stage has been
//! cancelled and has finished.
NOLINT_TEST_F(WithTest, BodyThrows)
{
  oxygen::co::Run(*el_, [this] -> Co<> {
    bool caught = false;
    bool first_finished = false;
    try {
      co_await With(
        [this, &first_finished](TaskStarted<> started) -> Co<> {
          started();
          co_await UntilCancelledAnd([this, &first_finished] -> Co<> {
            co_await el_->Sleep(1ms);
            first_finished = true;
          });
        },
        [this] -> Co<> {
          co_await el_->Sleep(1ms);
          throw std::runtime_error("body failed");
        });
    } catch (const std::runtime_error&) {
      caught = true;
      EXPECT_TRUE(first_finished);
    }
    EXPECT_TRUE(caught);
    EXPECT_EQ(el_->Now(), 2ms);
  });
}

} // namespace
