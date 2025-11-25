// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;

namespace Oxygen.Editor.Documents.Tests;

/// <summary>
/// Unit tests for <see cref="SceneViewLayout"/> enum.
/// </summary>
[TestClass]
[TestCategory("Unit")]
[ExcludeFromCodeCoverage]
public class SceneViewLayoutTests
{
    /// <summary>
    /// Test that all expected layout values exist.
    /// </summary>
    [TestMethod]
    public void SceneViewLayout_ContainsAllExpectedValues()
    {
        // Arrange
        var expectedValues = new[]
        {
            SceneViewLayout.OnePane,
            SceneViewLayout.TwoMainLeft,
            SceneViewLayout.TwoMainRight,
            SceneViewLayout.TwoMainTop,
            SceneViewLayout.TwoMainBottom,
            SceneViewLayout.ThreeMainLeft,
            SceneViewLayout.ThreeMainRight,
            SceneViewLayout.ThreeMainTop,
            SceneViewLayout.ThreeMainBottom,
            SceneViewLayout.FourMainLeft,
            SceneViewLayout.FourMainRight,
            SceneViewLayout.FourMainTop,
            SceneViewLayout.FourMainBottom,
            SceneViewLayout.FourQuad,
        };

        // Act
        var allValues = Enum.GetValues<SceneViewLayout>();

        // Assert
        _ = allValues.Should().HaveCount(expectedValues.Length);

        foreach (var expectedValue in expectedValues)
        {
            _ = allValues.Should().Contain(expectedValue);
        }
    }

    /// <summary>
    /// Test that enum values can be compared.
    /// </summary>
    [TestMethod]
    public void SceneViewLayout_ValuesCanBeCompared()
    {
        // Arrange
        const SceneViewLayout layout1 = SceneViewLayout.OnePane;
        const SceneViewLayout layout2 = SceneViewLayout.OnePane;
        const SceneViewLayout layout3 = SceneViewLayout.TwoMainLeft;

        // Assert
        _ = layout1.Should().Be(layout2);
        _ = layout1.Should().NotBe(layout3);
    }

    /// <summary>
    /// Test that enum can be converted to string.
    /// </summary>
    [TestMethod]
    public void SceneViewLayout_CanBeConvertedToString()
    {
        // Arrange
        const SceneViewLayout layout = SceneViewLayout.FourQuad;

        // Act
        var result = layout.ToString();

        // Assert
        _ = result.Should().Be("FourQuad");
    }
}
