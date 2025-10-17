// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using System.Text.Json;
using FluentAssertions;

namespace DroidNet.Aura.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
public class WindowCategoryTests
{
    [TestMethod]
    public void Constructor_Null_UsesSystemCategory()
    {
        // Act
        var cat = new WindowCategory(value: null);

        // Assert
        _ = cat.Value.Should().Be(WindowCategory.System.Value);
    }

    [TestMethod]
    public void Constructor_Empty_ThrowsValidationException()
    {
        // Act
        Action act = () => _ = new WindowCategory("   ");

        // Assert
        _ = act.Should().Throw<ValidationException>();
    }

    [TestMethod]
    public void Equals_IsCaseInsensitive()
    {
        var a = new WindowCategory("Main");
        var b = new WindowCategory("main");

        _ = a.Equals(b).Should().BeTrue();
        _ = a.GetHashCode().Should().Be(b.GetHashCode());
    }

    [TestMethod]
    public void Json_SerializeDeserialize_AsString()
    {
        var cat = WindowCategory.Tool;

        var options = new JsonSerializerOptions
        {
            PropertyNameCaseInsensitive = true,
        };

        var json = JsonSerializer.Serialize(cat, options);

        // Should produce a JSON string like "tool"
        _ = json.Should().Be('"' + cat.Value + '"');

        var deserialized = JsonSerializer.Deserialize<WindowCategory>(json, options);
        _ = deserialized.Equals(cat).Should().BeTrue();
    }

    [TestMethod]
    public void Json_CanUseAsDictionaryKey()
    {
        var dict = new Dictionary<WindowCategory, string>
        {
            [WindowCategory.Main] = "main window",
            [WindowCategory.Tool] = "tool window",
        };

        var options = new JsonSerializerOptions
        {
            Converters = { new WindowCategoryJsonConverter() },
        };

        var json = JsonSerializer.Serialize(dict, options);

        // Ensure property names (keys) are the category values
        _ = json.Should().Contain("\"" + WindowCategory.Main.Value + "\"");
        _ = json.Should().Contain("\"" + WindowCategory.Tool.Value + "\"");

        // Deserialize back
        var deserialized = JsonSerializer.Deserialize<Dictionary<WindowCategory, string>>(json, options);
        _ = deserialized.Should().NotBeNull();
        _ = deserialized!.Count.Should().Be(2);
        _ = deserialized[WindowCategory.Main].Should().Be("main window");
        _ = deserialized[WindowCategory.Tool].Should().Be("tool window");
    }
}
