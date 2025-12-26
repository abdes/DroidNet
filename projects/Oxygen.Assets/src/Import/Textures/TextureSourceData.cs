// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Assets.Import.Textures;

public sealed record TextureSourceData(
    string Schema,
    string? Name,
    string SourceImage,
    string ColorSpace,
    string TextureType,
    TextureMipPolicyData MipPolicy,
    TextureRuntimeFormatData RuntimeFormat,
    TextureImportedData? Imported);

public sealed record TextureMipPolicyData(
    string Mode);

public sealed record TextureRuntimeFormatData(
    string Format,
    string Compression);

public sealed record TextureImportedData(
    int Width,
    int Height);
