// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

namespace Oxygen.Managed.Core.Compatibility;

/// <summary>Stable diagnostics for the native artifact compatibility boundary.</summary>
public static class NativeCompatibilityDiagnosticCodes
{
    /// <summary>The build receipt is missing, inaccessible or malformed.</summary>
    public const string ReceiptInvalid = "OXE.RUNTIME.COMPATIBILITY.ReceiptInvalid";

    /// <summary>The accepted configuration differs from the running build.</summary>
    public const string ConfigurationMismatch = "OXE.RUNTIME.COMPATIBILITY.ConfigurationMismatch";

    /// <summary>The required artifact inventory differs from the accepted set.</summary>
    public const string ArtifactSetMismatch = "OXE.RUNTIME.COMPATIBILITY.ArtifactSetMismatch";

    /// <summary>A required artifact is inaccessible or differs from its accepted bytes or schema.</summary>
    public const string ArtifactMismatch = "OXE.RUNTIME.COMPATIBILITY.ArtifactMismatch";
}
