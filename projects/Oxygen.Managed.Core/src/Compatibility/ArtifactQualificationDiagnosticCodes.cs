// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Stable diagnostics for the fixed native artifact qualification boundary.</summary>
public static class ArtifactQualificationDiagnosticCodes
{
    /// <summary>The fixed manifest is missing, inaccessible or malformed.</summary>
    public const string ManifestInvalid = "OXE.RUNTIME.QUALIFICATION.ManifestInvalid";

    /// <summary>The accepted configuration differs from the running build.</summary>
    public const string ConfigurationMismatch = "OXE.RUNTIME.QUALIFICATION.ConfigurationMismatch";

    /// <summary>The required artifact inventory differs from the accepted set.</summary>
    public const string ArtifactSetMismatch = "OXE.RUNTIME.QUALIFICATION.ArtifactSetMismatch";

    /// <summary>A required artifact is inaccessible or differs from its accepted bytes or schema.</summary>
    public const string ArtifactMismatch = "OXE.RUNTIME.QUALIFICATION.ArtifactMismatch";
}
