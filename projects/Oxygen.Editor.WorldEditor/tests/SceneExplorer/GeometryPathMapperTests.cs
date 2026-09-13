// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.World.Services;

namespace Oxygen.Editor.World.SceneExplorer.Tests;

/// <summary>Checks geometry source and runtime identity mapping without rewriting authoring.</summary>
[TestClass]
public sealed class GeometryPathMapperTests
{
    /// <summary>Source descriptors address cooked geometry while built-ins and existing runtime identities remain exact.</summary>
    /// <param name="source">The stored reference.</param>
    /// <param name="expected">The native reference.</param>
    [TestMethod]
    [DataRow("asset:///Content/Geometry/Example.ogeo.json", "asset:///Content/Geometry/Example.ogeo")]
    [DataRow("asset:///Content/Geometry/Example.ogeo", "asset:///Content/Geometry/Example.ogeo")]
    [DataRow("asset:///Engine/Generated/BasicShapes/Cube", "asset:///Engine/Generated/BasicShapes/Cube")]
    public void GeometryReferencesPreserveAuthoringAndUseRuntimePaths(string source, string expected)
        => GeometryPathMapper.ToEnginePath(new Uri(source)).Should().Be(expected);
}
