// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Oxygen.Managed.Assets.Persistence.Pak;
using Oxygen.Managed.Core;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

/// <summary>An engine content pak for tests that run no native tool: it declares its assets instead of reading a file.</summary>
/// <param name="assets">The assets the pak lists; none by default.</param>
internal sealed class EngineContentPakFixture(params PakAssetEntry[] assets) : IEngineContentPak
{
    /// <inheritdoc />
    public FileSystemPath Path { get; } = FileSystemPath.From(System.IO.Path.Combine(System.IO.Path.GetTempPath(), "oxygen-engine-content-fixture", "engine.pak"));

    /// <summary>Gets the metadata every read returns.</summary>
    public PakDocument Document { get; } = new(ContentVersion: 1, Guid.Parse("6f1c2a3e-0000-4000-8000-00000000e001"), CatalogDigest: new string('0', 64), assets);

    /// <inheritdoc />
    public Task<PakDocument> ReadAsync(CancellationToken cancellationToken)
    {
        cancellationToken.ThrowIfCancellationRequested();
        return Task.FromResult(this.Document);
    }
}
