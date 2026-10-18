// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core;

namespace Oxygen.Managed.Assets.Tests;

/// <summary>Asset URIs for generic generated-catalog and resolver tests.</summary>
internal static class GeneratedAssetFixtures
{
    public static IEnumerable<AssetPath> Create()
    {
        foreach (var name in new[] { "Cube", "Sphere", "Plane", "Cylinder" })
        {
            yield return AssetPath.Generated.Combine("BasicShapes/" + name);
        }

        yield return AssetPath.Generated.Combine("Materials/Default");
    }
}
