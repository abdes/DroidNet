// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Editor.LivePreview.Scenes;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Tests.Scenes;

/// <summary>Checks geometry source and runtime identity mapping without rewriting authoring.</summary>
[TestClass]
public sealed class GeometryPathMapperTests
{
    /// <summary>Geometry identities, including built-ins, address the engine by their virtual path.</summary>
    /// <param name="path">The geometry identity and its native reference.</param>
    [TestMethod]
    [DataRow("/Content/Geometry/Example.ogeo")]
    [DataRow("/Engine/Generated/BasicShapes/Cube")]
    public void GeometryReferencesPreserveAuthoringAndUseRuntimePaths(string path)
        => GeometryPathMapper.ToEnginePath(AssetPath.Parse(path)).Should().Be(path);
}
