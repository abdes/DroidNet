//===----------------------------------------------------------------------===//
// Distributed under the 3-Clause BSD License. See accompanying file LICENSE or
// copy at https://opensource.org/licenses/BSD-3-Clause.
// SPDX-License-Identifier: BSD-3-Clause
//===----------------------------------------------------------------------===//

#include <Oxygen/Testing/GTest.h>

#include <Oxygen/Serio/MemoryStream.h>
#include <Oxygen/Serio/Reader.h>
#include <Oxygen/Serio/Writer.h>

/*!
  Integration test: Full-cycle serialization and deserialization of a composite
  structured type using the serio library and MemoryStream.

  This test demonstrates:
  - Custom struct serialization/deserialization via Store/Load ADL
  - Nested containers (std::vector, std::string)
  - Alignment and endianness handling
  - Use of AnyWriter/AnyReader interfaces
  - Round-trip data integrity

  @see Store, Load, MemoryStream, AnyWriter, AnyReader
*/

namespace oxygen::serio::testing {

//! Composite struct for integration test.
struct Person {
  uint32_t id;
  std::string name;
  std::vector<float> scores;
  uint8_t is_active; // 0 = inactive, 1 = active (Do not use bool)

  bool operator==(const Person& other) const noexcept
  {
    return id == other.id && name == other.name && scores == other.scores
      && is_active == other.is_active;
  }
};

} // namespace oxygen::serio::testing

namespace oxygen::serio {

//! Store specialization for Person.
inline auto Store(oxygen::serio::AnyWriter& writer,
  const oxygen::serio::testing::Person& person) -> oxygen::Result<void>
{
  CHECK_RESULT(writer.Write(person.id));
  CHECK_RESULT(writer.Write(person.name));
  CHECK_RESULT(writer.Write(person.scores));
  CHECK_RESULT(writer.Write(person.is_active));
  return {};
}

//! Load specialization for Person.
inline auto Load(oxygen::serio::AnyReader& reader,
  oxygen::serio::testing::Person& person) -> oxygen::Result<void>
{
  CHECK_RESULT(reader.ReadInto(person.id));
  CHECK_RESULT(reader.ReadInto(person.name));
  CHECK_RESULT(reader.ReadInto(person.scores));
  CHECK_RESULT(reader.ReadInto(person.is_active));
  return {};
}

} // namespace oxygen::serio

namespace {

//! Verifies that the buffer matches the expected hex dump.
void VerifyBufferMatchesHex(
  std::span<const std::byte> buffer, const std::vector<uint8_t>& expected)
{
  ASSERT_EQ(buffer.size(), expected.size()) << "Buffer size mismatch";
  for (size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(static_cast<uint8_t>(buffer[i]), expected[i])
      << "Mismatch at byte " << i;
  }
}

//! Integration: Serializes and deserializes a vector of Person using
//! MemoryStream.
NOLINT_TEST(SerioFullCycle, SerializeDeserializeComposite)
{
  using oxygen::serio::MemoryStream;
  using oxygen::serio::Reader;
  using oxygen::serio::Writer;
  using oxygen::serio::testing::Person;

  // Arrange
  const std::vector<Person> people = {
    { 1, "Alice", { 95.5f, 88.0f }, true },
    { 2, "Bob", { 72.0f, 85.5f, 90.0f }, false },
    { 3, "Charlie", {}, true },
  };

  // Expected Hex Dump (as bytes)
  const std::vector<uint8_t> expected_hex = {
    // clang-format off
    0x03, 0x00, 0x00, 0x00, // vector size

    // Person 1
    0x01, 0x00, 0x00, 0x00, // id
    0x05, 0x00, 0x00, 0x00, // name length
    0x41, 0x6c, 0x69, 0x63, 0x65, // "Alice"
    0x02, 0x00, 0x00, 0x00, // scores size
    0x00, 0x00, 0xbf, 0x42, // 95.5f
    0x00, 0x00, 0xb0, 0x42, // 88.0f
    0x01,                   // is_active

    // Person 2
    0x02, 0x00, 0x00, 0x00, // id
    0x03, 0x00, 0x00, 0x00, // name length
    0x42, 0x6f, 0x62,       // "Bob"
    0x03, 0x00, 0x00, 0x00, // scores size
    0x00, 0x00, 0x90, 0x42, // 72.0f
    0x00, 0x00, 0xab, 0x42, // 85.5f
    0x00, 0x00, 0xb4, 0x42, // 90.0f
    0x00,                   // is_active

    // Person 3
    0x03, 0x00, 0x00, 0x00, // id
    0x07, 0x00, 0x00, 0x00, // name length
    0x43, 0x68, 0x61, 0x72, 0x6c, 0x69, 0x65, // "Charlie"
    0x00, 0x00, 0x00, 0x00, // scores size
    0x01,                   // is_active
    // clang-format on
  };

  MemoryStream mem_stream;
  Writer<MemoryStream> writer(mem_stream);

  // Act
  {
    auto pack = writer.ScopedAlignment(1); // Ensure no padding
    ASSERT_TRUE(writer.Write(people));
  }
  ASSERT_TRUE(writer.Flush());

  // Assert buffer matches expected hex
  VerifyBufferMatchesHex(mem_stream.Data(), expected_hex);

  // Prepare for reading
  EXPECT_TRUE(mem_stream.Seek(0));
  Reader<MemoryStream> reader(mem_stream);

  std::vector<Person> loaded_people;
  {
    auto pack = reader.ScopedAlignment(1); // Ensure no padding
    ASSERT_TRUE(reader.ReadInto(loaded_people));
  }

  // Assert
  ASSERT_EQ(people.size(), loaded_people.size());
  for (size_t i = 0; i < people.size(); ++i) {
    EXPECT_EQ(people[i], loaded_people[i]);
  }
}

} // namespace
