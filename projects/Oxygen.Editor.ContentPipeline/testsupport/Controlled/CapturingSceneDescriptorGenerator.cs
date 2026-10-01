// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Diagnostics.CodeAnalysis;
using AwesomeAssertions;
using Microsoft.Extensions.Logging.Abstractions;
using Oxygen.Editor.ContentPipeline.Publication;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.World;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.TestSupport;

internal sealed class CapturingSceneDescriptorGenerator(IReadOnlyList<DiagnosticRecord> diagnostics) : ISceneDescriptorGenerator
{
    public ContentCookScope? Scope { get; private set; }

    public Scene? Scene { get; private set; }

    public Func<CancellationToken, Task>? BeforeGenerate { get; init; }

    public async Task<SceneDescriptorGenerationResult> GenerateAsync(
        Scene scene,
        ContentCookScope scope,
        CancellationToken cancellationToken)
    {
        this.Scope = scope;
        this.Scene = scene;
        if (this.BeforeGenerate is { } beforeGenerate)
        {
            await beforeGenerate(cancellationToken).ConfigureAwait(false);
        }

        var result = await new SceneDescriptorGenerator(new ProceduralGeometryDescriptorService(new BuiltinCatalogFixture()))
            .GenerateAsync(scene, scope, cancellationToken).ConfigureAwait(false);
        return result with { Diagnostics = [.. result.Diagnostics, .. diagnostics] };
    }
}
