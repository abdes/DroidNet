//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Base/Finally.h>

using oxygen::Finally;

namespace {

void f(int& i) { i += 1; }
int j = 0;
void g() { j += 1; }

NOLINT_TEST(FinallyTest, Lambda)
{
  int i = 0;
  {
    auto f = oxygen::Finally([&]() { i = 42; });
    EXPECT_EQ(i, 0);
  }
  EXPECT_EQ(i, 42);
}

NOLINT_TEST(FinallyTest, LambdaMove)
{
  int i = 0;
  {
    auto _1 = Finally([&]() { f(i); });
    {
      auto _2 = std::move(_1);
      EXPECT_TRUE(i == 0);
    }
    EXPECT_TRUE(i == 1);
    {
      auto _2 = std::move(_1);
      EXPECT_TRUE(i == 1);
    }
    EXPECT_TRUE(i == 1);
  }
  EXPECT_TRUE(i == 1);
}

NOLINT_TEST(FinallyTest, ConstLvalueLambda)
{
  int i = 0;
  {
    const auto const_lvalue_lambda = [&]() { f(i); };
    auto _ = Finally(const_lvalue_lambda);
    EXPECT_TRUE(i == 0);
  }
  EXPECT_TRUE(i == 1);
}

NOLINT_TEST(FinallyTest, MutableLvalueLambda)
{
  int i = 0;
  {
    auto mutable_lvalue_lambda = [&]() { f(i); };
    auto _ = Finally(mutable_lvalue_lambda);
    EXPECT_TRUE(i == 0);
  }
  EXPECT_TRUE(i == 1);
}

NOLINT_TEST(FinallyTest, FunctionWithBind)
{
  int i = 0;
  {
    auto _ = Finally([&i] { return f(i); });
    EXPECT_TRUE(i == 0);
  }
  EXPECT_TRUE(i == 1);
}

NOLINT_TEST(FinallyTest, FunctionPointer)
{
  j = 0;
  {
    auto _ = Finally(&g);
    EXPECT_TRUE(j == 0);
  }
  EXPECT_TRUE(j == 1);
}

NOLINT_TEST(FinallyTest, Function)
{
  j = 0;
  {
    auto _ = Finally(g);
    EXPECT_TRUE(j == 0);
  }
  EXPECT_TRUE(j == 1);
}

} // namespace
