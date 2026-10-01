// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.CodeAnalysis;
using Microsoft.CodeAnalysis.CSharp;
using Windows.Foundation;

namespace DroidNet.Coordinates.Tests;

[TestClass]
[ExcludeFromCodeCoverage]
[TestCategory("SpatialPointTests")]
public class SpatialPointTests
{
    [TestMethod]
    public void SpatialPoint_Construct_With_Point()
    {
        // Arrange
        var raw = new Point(42, 99);

        // Act
        var spatial = new SpatialPoint<ElementSpace>(raw);

        // Assert
        _ = spatial.Point.Should().Be(raw);
        _ = spatial.ToString().Should().Contain(nameof(ElementSpace));
    }

    [TestMethod]
    public void SpatialPoint_ToString_Includes_Space()
    {
        // Arrange
        var spatial = new SpatialPoint<ElementSpace>(new Point(1, 2));

        // Act
        var result = spatial.ToString();

        // Assert
        _ = result.Should().Contain("ElementSpace");
    }

    [TestMethod]
    public void SpatialPoint_ToPoint_Returns_RawPoint()
    {
        // Arrange
        var raw = new Point(1, 2);
        var spatial = new SpatialPoint<ElementSpace>(raw);

        // Act
        var result = spatial.Point;

        // Assert
        _ = result.Should().Be(raw);
    }

    [TestMethod]
    public void SpatialPoint_Add_SameSpace_Returns_Sum()
    {
        // Arrange
        var a = new SpatialPoint<ElementSpace>(new Point(1, 2));
        var b = new SpatialPoint<ElementSpace>(new Point(3, 4));

        // Act
        var sum = a + b;

        // Assert
        _ = sum.Point.Should().Be(new Point(4, 6));
    }

    [TestMethod]
    public void SpatialPoint_Subtract_SameSpace_Returns_Difference()
    {
        // Arrange
        var a = new SpatialPoint<ElementSpace>(new Point(5, 6));
        var b = new SpatialPoint<ElementSpace>(new Point(3, 4));

        // Act
        var diff = a - b;

        // Assert
        _ = diff.Point.Should().Be(new Point(2, 2));
    }

    [TestMethod]
    [DataRow("+")]
    [DataRow("-")]
    public void SpatialPoint_Arithmetic_RequiresMatchingSpaces(string operation)
    {
        var platformAssemblies = (string?)AppContext.GetData("TRUSTED_PLATFORM_ASSEMBLIES");
        _ = platformAssemblies.Should().NotBeNullOrEmpty();
        var references = platformAssemblies!.Split(Path.PathSeparator)
            .Append(typeof(SpatialPoint<>).Assembly.Location)
            .Distinct(StringComparer.OrdinalIgnoreCase)
            .Select(path => MetadataReference.CreateFromFile(path))
            .ToArray();

        _ = Compile(nameof(ElementSpace)).Should().BeEmpty("same-space arithmetic must compile");
        _ = Compile(nameof(WindowSpace)).Should().ContainSingle().Which.Id.Should().Be("CS0019");

        Diagnostic[] Compile(string rightSpace)
        {
            var source = $$"""
                using DroidNet.Coordinates;
                public static class ArithmeticProbe
                {
                    public static SpatialPoint<ElementSpace> Apply(
                        SpatialPoint<ElementSpace> left, SpatialPoint<{{rightSpace}}> right)
                        => left {{operation}} right;
                }
                """;
            var compilation = CSharpCompilation.Create(
                "SpatialArithmeticProbe",
                [CSharpSyntaxTree.ParseText(source)],
                references,
                new CSharpCompilationOptions(OutputKind.DynamicallyLinkedLibrary));
            return [.. compilation.GetDiagnostics().Where(d => d.Severity == DiagnosticSeverity.Error)];
        }
    }

    [TestMethod]
    public void SpatialPoint_Equality_SameSpace_SamePoint()
    {
        // Arrange
        var point = new Point(10, 20);
        var a = new SpatialPoint<ElementSpace>(point);
        var b = new SpatialPoint<ElementSpace>(point);

        // Act & Assert
        _ = a.Should().Be(b);
        _ = a.GetHashCode().Should().Be(b.GetHashCode());
    }

    [TestMethod]
    public void SpatialPoint_Equality_DifferentSpace_SamePoint_NotEqual()
    {
        // Arrange
        var point = new Point(10, 20);
        var a = new SpatialPoint<ElementSpace>(point);
        var b = new SpatialPoint<WindowSpace>(point);

        // Act & Assert
        _ = a.Should().NotBe(b);
    }

    [TestMethod]
    public void SpatialPoint_Construct_With_ZeroPoint()
    {
        // Arrange
        var raw = new Point(0, 0);

        // Act
        var spatial = new SpatialPoint<ElementSpace>(raw);

        // Assert
        _ = spatial.Point.Should().Be(raw);
    }

    [TestMethod]
    public void SpatialPoint_Construct_With_NegativePoint()
    {
        // Arrange
        var raw = new Point(-5, -10);

        // Act
        var spatial = new SpatialPoint<ElementSpace>(raw);

        // Assert
        _ = spatial.Point.Should().Be(raw);
    }

    [TestMethod]
    public void SpatialPoint_Add_With_Zero()
    {
        // Arrange
        var a = new SpatialPoint<ElementSpace>(new Point(5, 10));
        var zero = new SpatialPoint<ElementSpace>(new Point(0, 0));

        // Act
        var sum = a + zero;

        // Assert
        _ = sum.Point.Should().Be(new Point(5, 10));
    }

    [TestMethod]
    public void SpatialPoint_Subtract_With_Zero()
    {
        // Arrange
        var a = new SpatialPoint<ElementSpace>(new Point(5, 10));
        var zero = new SpatialPoint<ElementSpace>(new Point(0, 0));

        // Act
        var diff = a - zero;

        // Assert
        _ = diff.Point.Should().Be(new Point(5, 10));
    }

    [TestMethod]
    public void SpatialPoint_ToString_For_WindowSpace()
    {
        // Arrange
        var spatial = new SpatialPoint<WindowSpace>(new Point(1, 2));

        // Act
        var result = spatial.ToString();

        // Assert
        _ = result.Should().Contain("WindowSpace");
    }

    [TestMethod]
    public void SpatialPoint_ToString_For_ScreenSpace()
    {
        // Arrange
        var spatial = new SpatialPoint<ScreenSpace>(new Point(1, 2));

        // Act
        var result = spatial.ToString();

        // Assert
        _ = result.Should().Contain("ScreenSpace");
    }
}
