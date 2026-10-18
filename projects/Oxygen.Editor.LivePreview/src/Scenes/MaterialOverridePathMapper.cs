// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets;
using Oxygen.Managed.Assets.Catalog;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.LivePreview.Scenes;

internal static class MaterialOverridePathMapper
{
    public static string? ToEnginePath(AssetPath? material) => material?.VirtualPath;
}
