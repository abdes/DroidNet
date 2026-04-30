// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Diagnostics;

/// <summary>
/// Stable diagnostic codes for scalar material authoring and assignment.
/// </summary>
public static class MaterialDiagnosticCodes
{
    /// <summary>Material field edit was rejected.</summary>
    public const string FieldRejected = DiagnosticCodes.MaterialPrefix + "Field.Rejected";

    /// <summary>Material name is invalid.</summary>
    public const string NameInvalid = DiagnosticCodes.MaterialPrefix + "Name.Invalid";

    /// <summary>Material document save failed.</summary>
    public const string DocumentSaveFailed = DiagnosticCodes.DocumentPrefix + "MATERIAL.SaveFailed";

    /// <summary>Material descriptor is dirty and must be saved before cooking.</summary>
    public const string DescriptorDirty = DiagnosticCodes.ContentPipelinePrefix + "MATERIAL.DescriptorDirty";

    /// <summary>Material source file is missing.</summary>
    public const string SourceMissing = DiagnosticCodes.ContentPipelinePrefix + "MATERIAL.SourceMissing";

    /// <summary>Material descriptor is invalid.</summary>
    public const string InvalidDescriptor = DiagnosticCodes.ContentPipelinePrefix + "MATERIAL.InvalidDescriptor";

    /// <summary>Material cooking failed.</summary>
    public const string CookFailed = DiagnosticCodes.ContentPipelinePrefix + "MATERIAL.CookFailed";

    /// <summary>Material index update failed.</summary>
    public const string IndexFailed = DiagnosticCodes.ContentPipelinePrefix + "MATERIAL.IndexFailed";

    /// <summary>Material asset is missing.</summary>
    public const string Missing = DiagnosticCodes.AssetIdentityPrefix + "MATERIAL.Missing";

    /// <summary>Material asset is broken.</summary>
    public const string Broken = DiagnosticCodes.AssetIdentityPrefix + "MATERIAL.Broken";

    /// <summary>Material asset has not been cooked.</summary>
    public const string NotCooked = DiagnosticCodes.AssetIdentityPrefix + "MATERIAL.NotCooked";
}
