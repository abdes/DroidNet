// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Persistence.Pak;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline;

/// <summary>
/// The engine content pak: the always-present lowest cooked library that holds the <c>/Engine</c> assets. Cooks
/// reference its assets without copying them, and every runtime mounts it beneath the project's content.
/// </summary>
public interface IEngineContentPak
{
    /// <summary>Gets the pak file.</summary>
    public FileSystemPath Path { get; }

    /// <summary>Reads the pak's browsable metadata.</summary>
    /// <param name="cancellationToken">Cancels the read.</param>
    /// <returns>The assets the pak lists by virtual path, with its identity and catalog digest.</returns>
    /// <exception cref="IOException">The pak is missing or unreadable.</exception>
    /// <exception cref="InvalidDataException">The pak is malformed or uses another format version.</exception>
    public Task<PakDocument> ReadAsync(CancellationToken cancellationToken);
}
