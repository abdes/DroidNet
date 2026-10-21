// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;

namespace Oxygen.Editor.SceneInspector.Unit.Tests;

[TestClass]
public sealed class CompactNumberDisplayConverterTests
{
    /// <summary>Compact display preserves units and nonnumeric placeholders.</summary>
    /// <param name="text">The existing mask-formatted text.</param>
    /// <param name="expected">The compact text.</param>
    [TestMethod]
    [DataRow("-.-", "-.-")]
    [DataRow("12.300 m", "12.3 m")]
    [DataRow(".536", "0.536")]
    public void ConvertPreservesUnitsAndPlaceholders(string text, string expected)
        => _ = new CompactNumberDisplayConverter().Convert(text, typeof(string), null!, "en-US").Should().Be(expected);
}
