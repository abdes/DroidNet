// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.Materials;

/// <summary>
/// The codes of material authoring: why an edit or a new material was refused, and what a restored history step
/// warns about. A problem of a material file itself has a <see cref="Oxygen.Managed.Assets.Authoring.Materials.MaterialSourceCodes"/> code.
/// </summary>
public static class MaterialDiagnosticCodes
{
    /// <summary>The name of a new material is not a valid file name.</summary>
    public const string NameInvalid = DiagnosticCodes.MaterialPrefix + "Name.Invalid";

    /// <summary>An edit names a property the material does not have.</summary>
    public const string PropertyUnknown = DiagnosticCodes.MaterialPrefix + "Property.Unknown";

    /// <summary>An edited numeric material property value is not finite.</summary>
    public const string PropertyValueNotFinite = DiagnosticCodes.MaterialPrefix + "Property.Value.NotFinite";

    /// <summary>A finite edited material property value is outside the range the material editor accepts.</summary>
    public const string PropertyValueOutOfRange = DiagnosticCodes.MaterialPrefix + "Property.Value.OutOfRange";

    /// <summary>An edited enumeration material property value is not one of its declared members.</summary>
    public const string PropertyValueUndefined = DiagnosticCodes.MaterialPrefix + "Property.Value.Undefined";

    /// <summary>The edited material does not match the engine schema or the editor's constraints on it.</summary>
    public const string EditRejected = DiagnosticCodes.MaterialPrefix + "Edit.Rejected";

    /// <summary>An edit names a texture channel the engine's schema does not have.</summary>
    public const string TextureChannelInvalid = DiagnosticCodes.MaterialPrefix + "Texture.ChannelInvalid";

    /// <summary>An undo or redo brought back a reference to a texture that was deleted since.</summary>
    public const string TextureRestoredDeleted = DiagnosticCodes.MaterialPrefix + "Texture.RestoredDeleted";
}
