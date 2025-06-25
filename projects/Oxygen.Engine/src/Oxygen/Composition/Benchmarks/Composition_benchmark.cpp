//===----------------------------------------------------------------------===//
// Benchmark for oxygen::Composition: Local vs Pooled Components
//===----------------------------------------------------------------------===//

#include <algorithm>
#include <numeric>
#include <random>
#include <string>
#include <vector>

#include <benchmark/benchmark.h>

#include <Oxygen/Composition/Component.h>
#include <Oxygen/Composition/ComponentPool.h>
#include <Oxygen/Composition/ComponentPoolRegistry.h>
#include <Oxygen/Composition/Composition.h>

using namespace oxygen;

// --- Component Definitions --- //

// Define a ResourceTypeList for pooled components
class PooledComponentA;
class PooledComponentB;
class PooledComponentC;
class PooledComponentD;
using BenchmarkResourceTypeList = oxygen::TypeList<PooledComponentA,
  PooledComponentB, PooledComponentC, PooledComponentD>;

// Local components
class LocalComponentA : public oxygen::Component {
  OXYGEN_COMPONENT(LocalComponentA)
public:
  int value;
  std::string name;
  std::vector<double> data;
  LocalComponentA(int v, std::string n)
    : value(v)
    , name(std::move(n))
    , data(100, v * 0.1)
  {
  }
};

class LocalComponentB : public oxygen::Component {
  OXYGEN_COMPONENT(LocalComponentB)
public:
  double x, y, z;
  std::string desc;
  std::vector<int> numbers;
  LocalComponentB(double a, double b, double c, std::string d)
    : x(a)
    , y(b)
    , z(c)
    , desc(std::move(d))
    , numbers(100, static_cast<int>(a))
  {
  }
};

class LocalComponentC : public oxygen::Component {
  OXYGEN_COMPONENT(LocalComponentC)
public:
  std::string text;
  std::vector<float> floats;
  LocalComponentC(std::string t)
    : text(std::move(t))
    , floats(100, 3.14f)
  {
  }
};

class LocalComponentD : public oxygen::Component {
  OXYGEN_COMPONENT(LocalComponentD)
public:
  int id;
  std::string label;
  std::vector<char> buffer;
  LocalComponentD(int i, std::string l)
    : id(i)
    , label(std::move(l))
    , buffer(100, 'x')
  {
  }
};

// Pooled components
class PooledComponentA : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(PooledComponentA, BenchmarkResourceTypeList)
public:
  static constexpr size_t kExpectedPoolSize = 2048;

  int value;
  std::string name;
  std::vector<double> data;
  PooledComponentA(int v, std::string n)
    : value(v)
    , name(std::move(n))
    , data(100, v * 0.2)
  {
  }
};

class PooledComponentB : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(PooledComponentB, BenchmarkResourceTypeList)
public:
  static constexpr size_t kExpectedPoolSize = 2048;

  double x, y, z;
  std::string desc;
  std::vector<int> numbers;
  PooledComponentB(double a, double b, double c, std::string d)
    : x(a)
    , y(b)
    , z(c)
    , desc(std::move(d))
    , numbers(100, static_cast<int>(a))
  {
  }
};

class PooledComponentC : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(PooledComponentC, BenchmarkResourceTypeList)
public:
  std::string text;
  std::vector<float> floats;
  PooledComponentC(std::string t)
    : text(std::move(t))
    , floats(100, 2.71f)
  {
  }
};

class PooledComponentD : public oxygen::Component {
  OXYGEN_POOLED_COMPONENT(PooledComponentD, BenchmarkResourceTypeList)
public:
  int id;
  std::string label;
  std::vector<char> buffer;
  PooledComponentD(int i, std::string l)
    : id(i)
    , label(std::move(l))
    , buffer(100, 'y')
  {
  }
};

// --- Benchmark Fixture --- //

// Define a custom composition type for the benchmark
class BenchmarkComposition : public oxygen::Composition {
public:
  using oxygen::Composition::AddComponent;
  using oxygen::Composition::GetComponent;
  using oxygen::Composition::HasComponent;
  using oxygen::Composition::RemoveComponent;
};

class CompositionBenchmark : public benchmark::Fixture {
public:
  void SetUp(const ::benchmark::State&) override
  {
    // Ensure pools for pooled components are created
    static bool pools_created = false;
    if (!pools_created) {
      (void)oxygen::ComponentPoolRegistry::GetComponentPool<PooledComponentA>();
      (void)oxygen::ComponentPoolRegistry::GetComponentPool<PooledComponentB>();
      (void)oxygen::ComponentPoolRegistry::GetComponentPool<PooledComponentC>();
      (void)oxygen::ComponentPoolRegistry::GetComponentPool<PooledComponentD>();
      pools_created = true;
    }
  }
  void TearDown(const ::benchmark::State&) override { }
};

// Helper for random access
namespace {
constexpr int kNumComps = 1024;
template <typename F> void random_access_loop(F&& func)
{
  std::vector<int> indices(kNumComps);
  std::iota(indices.begin(), indices.end(), 0);
  std::mt19937 rng(42);
  std::shuffle(indices.begin(), indices.end(), rng);
  for (int idx : indices)
    func(idx);
}
}

// Helper for value checking
inline void check_local_a(const LocalComponentA& a, int i)
{
  if (a.value != i)
    benchmark::DoNotOptimize(a.value);
}
inline void check_local_b(const LocalComponentB& b, int i)
{
  if (b.x != i * 0.5)
    benchmark::DoNotOptimize(b.x);
}
inline void check_pooled_a(const PooledComponentA& a, int i)
{
  if (a.value != i + 100)
    benchmark::DoNotOptimize(a.value);
}
inline void check_pooled_b(const PooledComponentB& b, int i)
{
  if (b.x != i * 2.0)
    benchmark::DoNotOptimize(b.x);
}

// --- Benchmarks --- //

BENCHMARK_F(CompositionBenchmark, RandomAccessLocalComponents)(
  benchmark::State& state)
{
  std::vector<std::unique_ptr<BenchmarkComposition>> comps;
  comps.reserve(kNumComps);
  for (size_t i = 0; i < kNumComps; ++i) {
    auto comp = std::make_unique<BenchmarkComposition>();
    comp->AddComponent<LocalComponentA>(static_cast<int>(i), "Alpha");
    comp->AddComponent<LocalComponentB>(i * 0.5, i * 0.25, i * 0.125, "Beta");
    comp->AddComponent<LocalComponentC>("Gamma");
    comp->AddComponent<LocalComponentD>(7, "Delta");
    comps.push_back(std::move(comp));
  }
  for (auto _ : state) {
    random_access_loop([&](size_t idx) {
      auto& comp = *comps[idx];
      auto& a = comp.GetComponent<LocalComponentA>();
      auto& b = comp.GetComponent<LocalComponentB>();
      auto& c = comp.GetComponent<LocalComponentC>();
      auto& d = comp.GetComponent<LocalComponentD>();
      // Read and check data
      benchmark::DoNotOptimize(a.value);
      benchmark::DoNotOptimize(a.name);
      benchmark::DoNotOptimize(a.data[0]);
      if (a.value != static_cast<int>(idx))
        benchmark::DoNotOptimize(false);
      if (a.name != "Alpha")
        benchmark::DoNotOptimize(false);
      if (a.data[0] != idx * 0.1)
        benchmark::DoNotOptimize(false);
      benchmark::DoNotOptimize(b.x);
      benchmark::DoNotOptimize(b.desc);
      benchmark::DoNotOptimize(b.numbers[0]);
      if (b.x != idx * 0.5)
        benchmark::DoNotOptimize(false);
      if (b.y != idx * 0.25)
        benchmark::DoNotOptimize(false);
      if (b.z != idx * 0.125)
        benchmark::DoNotOptimize(false);
      if (b.desc != "Beta")
        benchmark::DoNotOptimize(false);
      if (b.numbers[0] != static_cast<int>(idx * 0.5))
        benchmark::DoNotOptimize(false);
      benchmark::DoNotOptimize(c.text);
      benchmark::DoNotOptimize(c.floats[0]);
      if (c.text != "Gamma")
        benchmark::DoNotOptimize(false);
      if (c.floats[0] != 3.14f)
        benchmark::DoNotOptimize(false);
      benchmark::DoNotOptimize(d.id);
      benchmark::DoNotOptimize(d.label);
      benchmark::DoNotOptimize(d.buffer[0]);
      if (d.id != 7)
        benchmark::DoNotOptimize(false);
      if (d.label != "Delta")
        benchmark::DoNotOptimize(false);
      if (d.buffer[0] != 'x')
        benchmark::DoNotOptimize(false);
    });
  }
}

BENCHMARK_F(CompositionBenchmark, RandomAccessPooledComponents)(
  benchmark::State& state)
{
  std::vector<std::unique_ptr<BenchmarkComposition>> comps;
  comps.reserve(kNumComps);
  for (size_t i = 0; i < kNumComps; ++i) {
    auto comp = std::make_unique<BenchmarkComposition>();
    comp->AddComponent<PooledComponentA>(static_cast<int>(i) + 100, "Omega");
    comp->AddComponent<PooledComponentB>(i * 2.0, i * 3.0, i * 4.0, "Sigma");
    comp->AddComponent<PooledComponentC>("Theta");
    comp->AddComponent<PooledComponentD>(13, "Lambda");
    comps.push_back(std::move(comp));
  }
  for (auto _ : state) {
    random_access_loop([&](size_t idx) {
      auto& comp = *comps[idx];
      auto& a = comp.GetComponent<PooledComponentA>();
      auto& b = comp.GetComponent<PooledComponentB>();
      auto& c = comp.GetComponent<PooledComponentC>();
      auto& d = comp.GetComponent<PooledComponentD>();
      // Read and check data
      benchmark::DoNotOptimize(a.value);
      benchmark::DoNotOptimize(a.name);
      benchmark::DoNotOptimize(a.data[0]);
      if (a.value != static_cast<int>(idx) + 100)
        benchmark::DoNotOptimize(false);
      if (a.name != "Omega")
        benchmark::DoNotOptimize(false);
      if (a.data[0] != (idx + 100) * 0.2)
        benchmark::DoNotOptimize(false);
      benchmark::DoNotOptimize(b.x);
      benchmark::DoNotOptimize(b.desc);
      benchmark::DoNotOptimize(b.numbers[0]);
      if (b.x != idx * 2.0)
        benchmark::DoNotOptimize(false);
      if (b.y != idx * 3.0)
        benchmark::DoNotOptimize(false);
      if (b.z != idx * 4.0)
        benchmark::DoNotOptimize(false);
      if (b.desc != "Sigma")
        benchmark::DoNotOptimize(false);
      if (b.numbers[0] != static_cast<int>(idx * 2.0))
        benchmark::DoNotOptimize(false);
      benchmark::DoNotOptimize(c.text);
      benchmark::DoNotOptimize(c.floats[0]);
      if (c.text != "Theta")
        benchmark::DoNotOptimize(false);
      if (c.floats[0] != 2.71f)
        benchmark::DoNotOptimize(false);
      benchmark::DoNotOptimize(d.id);
      benchmark::DoNotOptimize(d.label);
      benchmark::DoNotOptimize(d.buffer[0]);
      if (d.id != 13)
        benchmark::DoNotOptimize(false);
      if (d.label != "Lambda")
        benchmark::DoNotOptimize(false);
      if (d.buffer[0] != 'y')
        benchmark::DoNotOptimize(false);
    });
  }
}

BENCHMARK_F(CompositionBenchmark, RandomAccessHybridComponents)(
  benchmark::State& state)
{
  std::vector<std::unique_ptr<BenchmarkComposition>> comps;
  comps.reserve(kNumComps);
  for (size_t i = 0; i < kNumComps; ++i) {
    auto comp = std::make_unique<BenchmarkComposition>();
    comp->AddComponent<LocalComponentA>(static_cast<int>(i), "Alpha");
    comp->AddComponent<LocalComponentB>(i * 0.5, i * 0.25, i * 0.125, "Beta");
    comp->AddComponent<PooledComponentA>(static_cast<int>(i) + 100, "Omega");
    comp->AddComponent<PooledComponentB>(i * 2.0, i * 3.0, i * 4.0, "Sigma");
    comps.push_back(std::move(comp));
  }
  for (auto _ : state) {
    random_access_loop([&](size_t idx) {
      auto& comp = *comps[idx];
      auto& la = comp.GetComponent<LocalComponentA>();
      auto& lb = comp.GetComponent<LocalComponentB>();
      auto& pa = comp.GetComponent<PooledComponentA>();
      auto& pb = comp.GetComponent<PooledComponentB>();
      benchmark::DoNotOptimize(la.value);
      benchmark::DoNotOptimize(la.name);
      if (la.value != static_cast<int>(idx))
        benchmark::DoNotOptimize(false);
      if (la.name != "Alpha")
        benchmark::DoNotOptimize(false);
      benchmark::DoNotOptimize(lb.x);
      benchmark::DoNotOptimize(lb.desc);
      if (lb.x != idx * 0.5)
        benchmark::DoNotOptimize(false);
      if (lb.desc != "Beta")
        benchmark::DoNotOptimize(false);
      benchmark::DoNotOptimize(pa.value);
      benchmark::DoNotOptimize(pa.name);
      if (pa.value != static_cast<int>(idx) + 100)
        benchmark::DoNotOptimize(false);
      if (pa.name != "Omega")
        benchmark::DoNotOptimize(false);
      benchmark::DoNotOptimize(pb.x);
      benchmark::DoNotOptimize(pb.desc);
      if (pb.x != idx * 2.0)
        benchmark::DoNotOptimize(false);
      if (pb.desc != "Sigma")
        benchmark::DoNotOptimize(false);
    });
  }
}

BENCHMARK_F(CompositionBenchmark, SequentialAccessGetComponents)(
  benchmark::State& state)
{
  std::vector<std::unique_ptr<BenchmarkComposition>> comps;
  comps.reserve(kNumComps);
  for (size_t i = 0; i < kNumComps; ++i) {
    auto comp = std::make_unique<BenchmarkComposition>();
    comp->AddComponent<LocalComponentA>(static_cast<int>(i), "Alpha");
    comp->AddComponent<LocalComponentB>(i * 0.5, i * 0.25, i * 0.125, "Beta");
    comp->AddComponent<PooledComponentA>(static_cast<int>(i) + 100, "Omega");
    comp->AddComponent<PooledComponentB>(i * 2.0, i * 3.0, i * 4.0, "Sigma");
    comps.push_back(std::move(comp));
  }
  for (auto _ : state) {
    for (size_t idx = 0; idx < kNumComps; ++idx) {
      auto& comp = *comps[idx];
      auto& pa = comp.GetComponent<PooledComponentA>();
      auto& pb = comp.GetComponent<PooledComponentB>();
      benchmark::DoNotOptimize(pa.value);
      benchmark::DoNotOptimize(pa.name);
      if (pa.value != static_cast<int>(idx) + 100)
        benchmark::DoNotOptimize(false);
      if (pb.x != idx * 2.0)
        benchmark::DoNotOptimize(false);
      if (pb.desc != "Sigma")
        benchmark::DoNotOptimize(false);
    }
  }
}

BENCHMARK_F(CompositionBenchmark, PoolDirectIteration)(benchmark::State& state)
{
  // Create 1024 compositions, each with 2 pooled components of each type
  std::vector<std::unique_ptr<BenchmarkComposition>> comps;
  comps.reserve(kNumComps);
  for (int i = 0; i < kNumComps; ++i) {
    auto comp = std::make_unique<BenchmarkComposition>();
    comp->AddComponent<PooledComponentA>(i + 100, "Omega");
    comp->AddComponent<PooledComponentB>(i * 2.0, i * 3.0, i * 4.0, "Sigma");
    comps.push_back(std::move(comp));
  }
  // Access the pools directly
  auto& poolA
    = oxygen::ComponentPoolRegistry::GetComponentPool<PooledComponentA>();
  auto& poolB
    = oxygen::ComponentPoolRegistry::GetComponentPool<PooledComponentB>();
  for (auto _ : state) {
    // Iterate over all pooled components of type A
    size_t idxA = 0;
    poolA.ForEach([&](const PooledComponentA& a) {
      benchmark::DoNotOptimize(a.value);
      benchmark::DoNotOptimize(a.name);
      if (a.value != static_cast<int>(idxA) + 100)
        benchmark::DoNotOptimize(false);
      ++idxA;
    });
    size_t idxB = 0;
    poolB.ForEach([&](const PooledComponentB& b) {
      benchmark::DoNotOptimize(b.x);
      benchmark::DoNotOptimize(b.desc);
      if (b.x != idxB * 2.0)
        benchmark::DoNotOptimize(false);
      ++idxB;
    });
  }
}

BENCHMARK_F(CompositionBenchmark, FragmentedPoolDirectIteration)(
  benchmark::State& state)
{
  constexpr size_t initial_count = kNumComps + kNumComps / 2; // 1536
  constexpr size_t target_after_delete = 800;
  std::vector<std::unique_ptr<BenchmarkComposition>> comps;
  comps.reserve(initial_count);
  // Step 1: Create 50% more instances
  for (size_t i = 0; i < initial_count; ++i) {
    auto comp = std::make_unique<BenchmarkComposition>();
    comp->AddComponent<PooledComponentA>(static_cast<int>(i) + 100, "Omega");
    comp->AddComponent<PooledComponentB>(i * 2.0, i * 3.0, i * 4.0, "Sigma");
    comps.push_back(std::move(comp));
  }
  // Step 2: Randomly delete until 800 remain
  std::vector<size_t> indices(comps.size());
  std::iota(indices.begin(), indices.end(), 0);
  std::mt19937 rng(42);
  std::shuffle(indices.begin(), indices.end(), rng);
  size_t to_delete = comps.size() - target_after_delete;
  for (size_t i = 0; i < to_delete; ++i) {
    comps[indices[i]].reset();
  }
  // Remove nullptrs
  comps.erase(std::remove(comps.begin(), comps.end(), nullptr), comps.end());
  // Step 3: Fill up to 1024
  size_t fill_start = initial_count;
  for (size_t i = comps.size(); i < kNumComps; ++i) {
    auto comp = std::make_unique<BenchmarkComposition>();
    comp->AddComponent<PooledComponentA>(
      static_cast<int>(fill_start + i) + 100, "Omega");
    comp->AddComponent<PooledComponentB>((fill_start + i) * 2.0,
      (fill_start + i) * 3.0, (fill_start + i) * 4.0, "Sigma");
    comps.push_back(std::move(comp));
  }
  // Step 4: Only measure the access loop
  auto& poolA
    = oxygen::ComponentPoolRegistry::GetComponentPool<PooledComponentA>();
  auto& poolB
    = oxygen::ComponentPoolRegistry::GetComponentPool<PooledComponentB>();
  for (auto _ : state) {
    size_t idxA = 0;
    poolA.ForEach([&](const PooledComponentA& a) {
      benchmark::DoNotOptimize(a.value);
      benchmark::DoNotOptimize(a.name);
      if (a.value != static_cast<int>(idxA) + 100)
        benchmark::DoNotOptimize(false);
      ++idxA;
    });
    size_t idxB = 0;
    poolB.ForEach([&](const PooledComponentB& b) {
      benchmark::DoNotOptimize(b.x);
      benchmark::DoNotOptimize(b.desc);
      if (b.x != idxB * 2.0)
        benchmark::DoNotOptimize(false);
      ++idxB;
    });
  }
}

BENCHMARK_MAIN();
