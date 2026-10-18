// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Managed.Assets.Authoring.Materials;

/// <summary>
/// The codes of the problems a <c>*.omat.json</c> file can have, reported by <see cref="MaterialSourceReader"/> and
/// <see cref="MaterialSourceWriter"/>.
/// </summary>
public static class MaterialSourceCodes
{
    /// <summary>The file is not JSON. The problem has the line and column of the error.</summary>
    public const string NotJson = DiagnosticCodes.MaterialPrefix + "Source.NotJson";

    /// <summary>The JSON does not match the engine's material descriptor schema.</summary>
    public const string SchemaMismatch = DiagnosticCodes.MaterialPrefix + "Source.SchemaMismatch";

    /// <summary>A number does not fit a finite 32-bit float, which the engine stores.</summary>
    public const string NumberOutOfRange = DiagnosticCodes.MaterialPrefix + "Source.NumberOutOfRange";

    /// <summary>A texture is not named by a canonical absolute virtual path below a mount point.</summary>
    public const string TexturePathInvalid = DiagnosticCodes.MaterialPrefix + "Source.TexturePathInvalid";
}
