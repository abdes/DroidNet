// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Testing;

internal static partial class NativeInventoryFixture
{
    public static IEngineContentPipelineApi CreateApi()
    {
        var api = new Mock<IEngineContentPipelineApi>(MockBehavior.Strict);
        _ = api.Setup(value => value.ReadInventoryAsync(It.IsAny<string>(), It.IsAny<NativeArtifactLease?>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync((string root, NativeArtifactLease? _, CancellationToken _) => Read(root));
        _ = api.Setup(value => value.ReadImporterRevisionsAsync(It.IsAny<NativeArtifactLease?>(), It.IsAny<CancellationToken>()))
            .ReturnsAsync(Oxygen.Editor.ContentPipeline.Testing.ImporterRevisionsFixture.Current);
        return api.Object;
    }
}
