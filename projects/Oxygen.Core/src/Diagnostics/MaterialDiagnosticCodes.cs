// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Core.Diagnostics;

/// <summary>
/// Stable diagnostic codes for scalar material authoring and assignment.
/// </summary>
public static class MaterialDiagnosticCodes
{
    public const string FieldRejected = DiagnosticCodes.MaterialPrefix + "Field.Rejected";

    public const string NameInvalid = DiagnosticCodes.MaterialPrefix + "Name.Invalid";

    public const string DocumentSaveFailed = DiagnosticCodes.DocumentPrefix + "MATERIAL.SaveFailed";

    public const string DescriptorDirty = DiagnosticCodes.ContentPipelinePrefix + "MATERIAL.DescriptorDirty";

    public const string SourceMissing = DiagnosticCodes.ContentPipelinePrefix + "MATERIAL.SourceMissing";

    public const string InvalidDescriptor = DiagnosticCodes.ContentPipelinePrefix + "MATERIAL.InvalidDescriptor";

    public const string CookFailed = DiagnosticCodes.ContentPipelinePrefix + "MATERIAL.CookFailed";

    public const string IndexFailed = DiagnosticCodes.ContentPipelinePrefix + "MATERIAL.IndexFailed";

    public const string Missing = DiagnosticCodes.AssetIdentityPrefix + "MATERIAL.Missing";

    public const string Broken = DiagnosticCodes.AssetIdentityPrefix + "MATERIAL.Broken";

    public const string NotCooked = DiagnosticCodes.AssetIdentityPrefix + "MATERIAL.NotCooked";

    public const string LiveSyncUnsupported = DiagnosticCodes.LiveSyncPrefix + "MATERIAL.Unsupported";
}
