# ===-----------------------------------------------------------------------===#
# Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
# copy at https://opensource.org/licenses/BSD-3-Clause.
# SPDX-License-Identifier: BSD-3-Clause
# ===-----------------------------------------------------------------------===#

# ==============================================================================
# Build instructions
# ==============================================================================

# -- GTest Test Programs -------------------------------------------------------

include(GTestHelpers)

m_gtest_program(Concepts SOURCES Concepts_test.cpp)
m_gtest_program(Executor SOURCES Executor_test.cpp)
m_gtest_program(Frames SOURCES Frames_test.cpp)
m_gtest_program(IntrusiveList SOURCES IntrusiveList_test.cpp)
m_gtest_program(IntrusivePtr SOURCES IntrusivePtr_test.cpp)
m_gtest_program(Optional SOURCES Optional_test.cpp)
m_gtest_program(ParkingLot SOURCES ParkingLot_test.cpp)
m_gtest_program(PointerBits SOURCES PointerBits_test.cpp)
m_gtest_program(Queue SOURCES Queue_test.cpp)
m_gtest_program(Result SOURCES Result_test.cpp)
m_gtest_program(ScopeGuard SOURCES ScopeGuard_test.cpp)
m_gtest_program(
  Mux
  SOURCES
    MuxHelper_test.cpp
    MuxBase_test.cpp
    AnyOf_test.cpp
    MostOf_test.cpp
    AllOf_test.cpp
    MuxRange_test.cpp
)
m_gtest_program(ChannelTest SOURCES Channel_test.cpp)
m_gtest_program(AsioTest SOURCES Asio_test.cpp)
m_gtest_program(SharedTest SOURCES Shared_test.cpp)

# -- Catch2 Test Programs ------------------------------------------------------

include(Catch2Helpers)

m_catch2_program(Sequence SOURCES Sequence_test.cpp)
m_catch2_program(Semaphore SOURCES Semaphore_test.cpp)
m_catch2_program(Value SOURCES Value_test.cpp)
m_catch2_program(Nursery SOURCES Nursery_test.cpp)

# -- Additional Functional Test ------------------------------------------------

m_gtest_program(
  Functional
  SOURCES
    Basic_test.cpp
    Cancellation_test.cpp
    ThrowingAwaitable_test.cpp
)
set(func_tests_target "${META_MODULE_NAME}.Functional.Tests")
target_compile_definitions(${func_tests_target} PRIVATE OXCO_AWAITABLE_STATE_DEBUG)

if(OXYGEN_WITH_COVERAGE)
  target_compile_options(${func_tests_target} PRIVATE "--coverage")
  target_link_options(${func_tests_target} PRIVATE "--coverage")
endif()

# -- Benchmarks ----------------------------------------------------------------

m_catch2_program(SynchronizedEventSource SOURCES EventStream_bench.cpp)
