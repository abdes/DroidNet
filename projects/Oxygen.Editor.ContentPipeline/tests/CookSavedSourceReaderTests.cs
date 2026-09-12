// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Runtime.ExceptionServices;
using AwesomeAssertions;
using DroidNet.Storage.Native;

namespace Oxygen.Editor.ContentPipeline.Tests;

/// <summary>Routine absence of optional files must not generate debugger exception traffic.</summary>
[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class CookSavedSourceReaderTests
{
    /// <summary>Gets or sets the running test context.</summary>
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Missing settings and a first cook's missing provenance are ordinary absence, without a thrown exception.</summary>
    /// <param name="missingParent">Whether the parent directory is also absent.</param>
    /// <returns>The asynchronous read regression.</returns>
    [TestMethod]
    [DataRow(false)]
    [DataRow(true)]
    public async Task MissingOptionalInputsDoNotThrowFirstChanceExceptions(bool missingParent)
    {
        var root = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"));
        var path = missingParent ? Path.Combine(root, "Scene.oscene.json.import.json") : root + ".import.json";
        var exceptions = new System.Collections.Concurrent.ConcurrentQueue<Exception>();
        void Observe(object? sender, FirstChanceExceptionEventArgs args)
        {
            if (args.Exception is IOException && args.Exception.Message.Contains(root, StringComparison.OrdinalIgnoreCase))
            {
                exceptions.Enqueue(args.Exception);
            }
        }

        AppDomain.CurrentDomain.FirstChanceException += Observe;
        try
        {
            _ = CookSavedSourceReader.Exists(path).Should().BeFalse();
            var files = new NativeAtomicFileStore(new Testably.Abstractions.RealFileSystem());
            _ = (await files.ReadAsync(path, this.TestContext.CancellationToken).ConfigureAwait(false)).Version.Exists.Should().BeFalse();
            _ = exceptions.Should().BeEmpty();
        }
        finally
        {
            AppDomain.CurrentDomain.FirstChanceException -= Observe;
        }
    }
}
