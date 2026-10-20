// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Textures;

/// <summary>
/// The codes of texture import: why an image was not imported. A problem of a texture descriptor itself has a
/// <see cref="Oxygen.Managed.Assets.Authoring.Textures.TextureSourceCodes"/> code.
/// </summary>
public static class TextureImportCodes
{
    /// <summary>The image is not of a type the import supports.</summary>
    public const string ImageUnsupported = DiagnosticCodes.TexturePrefix + "Import.ImageUnsupported";

    /// <summary>The image does not exist.</summary>
    public const string ImageMissing = DiagnosticCodes.TexturePrefix + "Import.ImageMissing";

    /// <summary>The texture's name is not a valid asset name.</summary>
    public const string NameInvalid = DiagnosticCodes.TexturePrefix + "Import.NameInvalid";

    /// <summary>A texture of that name, or one of its images, already exists in the destination.</summary>
    public const string NameTaken = DiagnosticCodes.TexturePrefix + "Import.NameTaken";

    /// <summary>The intent, color space or format is not one the import offers.</summary>
    public const string SettingsUnsupported = DiagnosticCodes.TexturePrefix + "Import.SettingsUnsupported";

    /// <summary>A panorama's face size is not a positive multiple of 256.</summary>
    public const string FaceSizeInvalid = DiagnosticCodes.TexturePrefix + "Import.FaceSizeInvalid";

    /// <summary>The destination is not inside one of the project's authoring mounts.</summary>
    public const string DestinationNotAuthoring = DiagnosticCodes.TexturePrefix + "Import.DestinationNotAuthoring";

    /// <summary>The destination is a source-media or generated folder.</summary>
    public const string DestinationDerived = DiagnosticCodes.TexturePrefix + "Import.DestinationDerived";

    /// <summary>A strip or cross image is not exactly its grid of square faces.</summary>
    public const string LayoutMismatch = DiagnosticCodes.TexturePrefix + "Import.LayoutMismatch";

    /// <summary>The six face files of a cube are not all next to the selected one.</summary>
    public const string CubeFacesIncomplete = DiagnosticCodes.TexturePrefix + "Import.CubeFacesIncomplete";

    /// <summary>A cube face image could not be read.</summary>
    public const string CubeFaceUnreadable = DiagnosticCodes.TexturePrefix + "Import.CubeFaceUnreadable";

    /// <summary>The cube faces are not square images of one size.</summary>
    public const string CubeFacesMismatched = DiagnosticCodes.TexturePrefix + "Import.CubeFacesMismatched";
}
