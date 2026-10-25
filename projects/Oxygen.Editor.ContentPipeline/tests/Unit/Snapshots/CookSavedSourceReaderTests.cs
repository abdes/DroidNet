// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Collections.Immutable;
using System.Runtime.ExceptionServices;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Oxygen.Editor.ContentPipeline.Import;
using Oxygen.Editor.ContentPipeline.Snapshots;
using Oxygen.Managed.Core;
using Oxygen.Managed.Core.Diagnostics;

namespace Oxygen.Editor.ContentPipeline.Unit.Tests.Snapshots;

[TestClass]
[System.Diagnostics.CodeAnalysis.SuppressMessage("Maintainability", "CA1515:Consider making public types internal", Justification = "MSTest discovers public test classes with the repository discovery configuration.")]
public sealed class CookSavedSourceReaderTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>A missing import record or optional input is ordinary absence, without a thrown exception.</summary>
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

    /// <summary>
    /// A missing file outside the inputs' folder is omitted, an existing one is reported outside, and
    /// a missing file inside stays an absence input.
    /// </summary>
    /// <returns>The asynchronous classification check.</returns>
    [TestMethod]
    public async Task NativeInputsAreClassifiedAgainstTheirRoot()
    {
        const string root = @"H:\Project\Content\Model";
        const string absentInside = @"H:\Project\Content\Model\roof.png";
        const string absentOutside = @"C:\Users\artist\AppData\Local\Temp\blender\sat.tif";
        const string existingOutside = @"H:\Shared\tile.png";
        var facts = Facts(
            Observation(absentInside, exists: false),
            Observation(absentOutside, exists: false),
            Observation(existingOutside, exists: true));

        var inputs = await CookSavedSourceReader.ReadNativeInputsAsync(facts, FileSystemPath.From(root), ImmutableHashSet<FileSystemPath>.Empty, this.TestContext.CancellationToken).ConfigureAwait(false);

        _ = inputs.Files.Should().ContainSingle(file => file.SourcePath == FileSystemPath.From(absentInside) && file.Kind == CookSnapshotInputKind.Absent);
        _ = inputs.Omitted.Should().Equal(FileSystemPath.From(absentOutside));
        _ = inputs.Outside.Should().Equal(FileSystemPath.From(existingOutside));
        _ = inputs.OmittedDiagnostics(Guid.NewGuid(), FailureDomain.AssetImport, virtualPath: null).Should()
            .ContainSingle(issue => issue.Code == AssetImportDiagnosticCodes.MissingDependencyOmitted && issue.Severity == DiagnosticSeverity.Warning);
        _ = inputs.OutsideDiagnostics(Guid.NewGuid(), FailureDomain.AssetImport, virtualPath: null).Should()
            .ContainSingle(issue => issue.Code == AssetImportDiagnosticCodes.DependencyOutsideSource && issue.Severity == DiagnosticSeverity.Error);
    }

    /// <summary>
    /// The inputs' folder widens to hold existing parent-relative files on the same drive, never for a
    /// missing file or one on another drive.
    /// </summary>
    [TestMethod]
    public void NativeInputsRootHoldsOnlyExistingSameDriveFiles()
    {
        var facts = Facts(
            Observation(@"H:\Project\Content\Textures\wall.png", exists: true),
            Observation(@"H:\Temp\missing.png", exists: false),
            Observation(@"C:\Other\present.png", exists: true));

        var root = CookSavedSourceReader.FindNativeInputsRoot(facts, FileSystemPath.From(@"H:\Project\Content\Model"));

        _ = root.Should().Be(FileSystemPath.From(@"H:\Project\Content"));
    }

    private static NativeSourceObservation Observation(string path, bool exists)
        => new(path, exists, Metadata: null, Reads: []);

    private static NativeSourceAnalysisJob Facts(params NativeSourceObservation[] observations)
        => new("model", "fbx", @"H:\Project\Content\Model\model.fbx", Complete: true, Outputs: [], References: [], Files: [], Observations: [.. observations], Diagnostics: []);
}
