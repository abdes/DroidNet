// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Editor.MaterialEditor;

/// <summary>
/// Stable scalar material field keys supported by the ED-M05 material editor.
/// </summary>
public static class MaterialFieldKeys
{
    public const string Name = "Name";

    public const string BaseColorR = "PbrMetallicRoughness.BaseColorR";

    public const string BaseColorG = "PbrMetallicRoughness.BaseColorG";

    public const string BaseColorB = "PbrMetallicRoughness.BaseColorB";

    public const string BaseColorA = "PbrMetallicRoughness.BaseColorA";

    public const string MetallicFactor = "PbrMetallicRoughness.MetallicFactor";

    public const string RoughnessFactor = "PbrMetallicRoughness.RoughnessFactor";

    public const string NormalTextureScale = "NormalTexture.Scale";

    public const string OcclusionTextureStrength = "OcclusionTexture.Strength";

    public const string AlphaMode = "AlphaMode";

    public const string AlphaCutoff = "AlphaCutoff";

    public const string DoubleSided = "DoubleSided";
}
