// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using Oxygen.Managed.Core.Compatibility;

namespace Oxygen.Editor.ContentPipeline.Integration.Tests.Import;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class NativeCompatibilityTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>The deployed editor schema names and installed cooker pass the real preflight.</summary>
    /// <returns>The asynchronous installed-input check.</returns>
    [TestMethod]
    public async Task InstalledCookerInputsMatchEditorSchemas()
    {
        var compatibility = EditorNativeCompatibilityService.ForCooking();
        var result = await compatibility.VerifyAsync(Guid.NewGuid(), this.TestContext.CancellationToken).ConfigureAwait(false);
        _ = result.Succeeded.Should().BeTrue(string.Join("; ", result.Diagnostics.Select(static value => value.TechnicalMessage ?? value.Message)));
        await result.Artifacts!.DisposeAsync().ConfigureAwait(false);
    }
}
