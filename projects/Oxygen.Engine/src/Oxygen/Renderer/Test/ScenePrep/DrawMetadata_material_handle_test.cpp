//===----------------------------------------------------------------------===//
// Verifies DrawMetadata material_handle field presence & basic semantics.
//===----------------------------------------------------------------------===//

#include <gtest/gtest.h>

#include <Oxygen/Renderer/Types/DrawMetadata.h>

using oxygen::engine::DrawMetadata;

TEST(DrawMetadataMaterialHandleTest, FieldDefaultIsZeroSentinel)
{
  DrawMetadata dm {};
  EXPECT_EQ(dm.material_handle, 0u);
}

TEST(DrawMetadataMaterialHandleTest, SizeInvariant52Bytes)
{
  // Guard against accidental padding/packing drift after rename.
  EXPECT_EQ(sizeof(DrawMetadata), 52u);
}
