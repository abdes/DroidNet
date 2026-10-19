// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Managed.Assets.Authoring.Textures;

/// <summary>
/// The codes of the problems a <c>*.otex.json</c> file can have, reported by <see cref="TextureSourceReader"/> and
/// <see cref="TextureSourceWriter"/>.
/// </summary>
public static class TextureSourceCodes
{
    /// <summary>The file is not JSON. The problem has the line and column of the error.</summary>
    public const string NotJson = DiagnosticCodes.TexturePrefix + "Source.NotJson";

    /// <summary>The JSON does not match the engine's texture descriptor schema.</summary>
    public const string SchemaMismatch = DiagnosticCodes.TexturePrefix + "Source.SchemaMismatch";

    /// <summary>The texture's <c>virtual_path</c> is not a canonical absolute virtual path below a mount point.</summary>
    public const string VirtualPathInvalid = DiagnosticCodes.TexturePrefix + "Source.VirtualPathInvalid";
}
