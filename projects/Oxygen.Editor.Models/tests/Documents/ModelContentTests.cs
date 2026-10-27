// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text;
using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Storage.Native;
using Oxygen.Editor.Documents;
using Oxygen.Editor.Models.Documents;
using Oxygen.Editor.Projects.Testing;
using Oxygen.Managed.Core;
using Testably.Abstractions.Testing;

namespace Oxygen.Editor.Models.Tests.Documents;

/// <summary>
/// A model document edits its model's import record (model editor ME-5, ME-6, M10): each change of its settings or node
/// continuity is one undo step, Save replaces the record through its writer and never writes the source, and what an
/// import rewrote is taken without losing unsaved edits.
/// </summary>
[TestClass]
public sealed class ModelContentTests : IDisposable
{
    private const string SourceBytes = "glTF";
    private readonly TestProject project = TestProject.Create(new MockFileSystem());
    private readonly TestSession session;
    private readonly RecordWriter writer;
    private readonly OpenDocuments documents;
    private readonly string recordPath;
    private readonly DocumentFile source;

    public ModelContentTests()
    {
        this.session = new TestSession(this.project.Context);
        var path = this.project.WriteFile("/Content/Vehicles/Car.glb", SourceBytes);
        this.source = new DocumentFile(path, AssetPath.Parse("/Content/Vehicles/Car.glb"));
        this.recordPath = ModelImportSettings.PathFor(this.project.Root.FullPath, Guid.CreateVersion7());
        this.writer = new RecordWriter(this);
        this.documents = new OpenDocuments([new ModelDocumentKind(this.session, this.writer)], new NativeAtomicFileStore(this.project.Context.FileSystem));
        using var settings = JsonDocument.Parse("""{"name":"Car","unit_policy":"normalize"}""");
        this.WriteRecord(ModelImportSettings.Create(this.recordPath, path.FullPath, "/Content/Vehicles/Car.glb", settings.RootElement));
    }

    public TestContext TestContext { get; set; }

    private CancellationToken Token => this.TestContext.CancellationToken;

    [TestMethod]
    public async Task OpeningAModelReadsItsImportRecord()
    {
        var (_, model) = await this.OpenAsync().ConfigureAwait(false);

        _ = ModelImportSettingCatalog.UnitPolicy.ReadFrom(model.Record!.Settings).Should().Be("normalize");
    }

    [TestMethod]
    public async Task ASettingsEditIsOneUndoStepThatDirtiesTheDocument()
    {
        var (document, model) = await this.OpenAsync().ConfigureAwait(false);
        var changed = 0;
        model.Changed += (_, _) => changed++;

        model.EditSettings(ModelImportSettingCatalog.UnitPolicy.WriteTo(model.Record!.Settings, "custom"), "Edit Units");

        _ = document.IsDirty.Should().BeTrue();
        _ = document.History.UndoStack.Should().ContainSingle();
        await document.History.UndoAsync(this.Token).ConfigureAwait(false);
        _ = ModelImportSettingCatalog.UnitPolicy.ReadFrom(model.Record!.Settings).Should().Be("normalize");
        _ = document.IsDirty.Should().BeFalse();
        _ = changed.Should().Be(2);
    }

    [TestMethod]
    public async Task SaveReplacesTheRecordAndNeverWritesTheSource()
    {
        var (document, model) = await this.OpenAsync().ConfigureAwait(false);
        model.EditSettings(ModelImportSettingCatalog.UnitPolicy.WriteTo(model.Record!.Settings, "custom"), "Edit Units");

        var result = await document.SaveAsync(this.Token).ConfigureAwait(false);

        _ = result.Outcome.Should().Be(DocumentSaveOutcome.Saved);
        _ = document.IsDirty.Should().BeFalse();
        _ = this.writer.Saves.Should().Be(1);
        _ = ModelImportSettingCatalog.UnitPolicy.ReadFrom(this.ReadRecord().Settings).Should().Be("custom");
        _ = this.project.Context.FileSystem.File.ReadAllText(this.source.Path.FullPath, Encoding.UTF8).Should().Be(SourceBytes);
    }

    [TestMethod]
    public async Task ARecordAnImportRewroteIsTakenWithTheUnsavedEditsKept()
    {
        var (document, model) = await this.OpenAsync().ConfigureAwait(false);
        model.EditSettings(ModelImportSettingCatalog.UnitPolicy.WriteTo(model.Record!.Settings, "custom"), "Edit Units");
        var node = new ModelPublishedNode("Body", Guid.NewGuid());
        this.WriteRecord(this.ReadRecord() with { PublishedNodes = [node] });

        var conflict = await document.SaveAsync(this.Token).ConfigureAwait(false);
        model.TakeRecord(this.ReadRecord(), keepEdits: true);
        var saved = await document.SaveAsync(this.Token).ConfigureAwait(false);

        _ = conflict.Outcome.Should().Be(DocumentSaveOutcome.Conflict);
        _ = saved.Outcome.Should().Be(DocumentSaveOutcome.Saved);
        var record = this.ReadRecord();
        _ = ModelImportSettingCatalog.UnitPolicy.ReadFrom(record.Settings).Should().Be("custom");
        _ = record.PublishedNodes.Should().Equal(node);
    }

    [TestMethod]
    public async Task UndoKeepsWhatAnImportRewrote()
    {
        var (document, model) = await this.OpenAsync().ConfigureAwait(false);
        model.AddContinuity("Body/Door_Front_L", "Body/Door_FL");
        var node = new ModelPublishedNode("Body/Door_Front_L", Guid.NewGuid());
        model.TakeRecord(this.ReadRecord() with { PublishedNodes = [node] }, keepEdits: true);

        await document.History.UndoAsync(this.Token).ConfigureAwait(false);

        _ = model.Record!.NodeContinuity.Should().BeEmpty();
        _ = model.Record.PublishedNodes.Should().Equal(node);
    }

    [TestMethod]
    public async Task ContinuityEntriesAreAddedAndRemovedAsUndoSteps()
    {
        var (document, model) = await this.OpenAsync().ConfigureAwait(false);

        model.AddContinuity("Body/Door_Front_L", "Body/Door_FL");
        model.RemoveContinuity("Body/Door_Front_L");

        _ = model.Record!.NodeContinuity.Should().BeEmpty();
        _ = document.History.UndoStack.Should().HaveCount(2);
        await document.History.UndoAsync(this.Token).ConfigureAwait(false);
        _ = model.Record!.NodeContinuity.Should().Equal(new ModelNodeContinuity("Body/Door_Front_L", "Body/Door_FL"));
    }

    [TestMethod]
    public async Task AModelWithoutARecordOpensAndRefusesToSave()
    {
        this.project.Context.FileSystem.File.Delete(this.recordPath);

        var (document, model) = await this.OpenAsync().ConfigureAwait(false);

        _ = model.Record.Should().BeNull();
        _ = model.Capture().Refusal.Should().Contain("Car.glb");
        _ = document.IsDirty.Should().BeFalse();
    }

    public void Dispose()
    {
        this.documents.Dispose();
        this.session.Dispose();
    }

    private async Task<(Document Document, ModelContent Model)> OpenAsync()
    {
        var opened = await this.documents.OpenAsync(this.source, this.Token).ConfigureAwait(false);
        var document = opened.Document ?? throw new AssertFailedException(opened.Problem ?? "The model did not open.");
        return (document, (ModelContent)document.Content);
    }

    private void WriteRecord(ModelImportSettings record)
    {
        _ = this.project.Context.FileSystem.Directory.CreateDirectory(Path.GetDirectoryName(this.recordPath)!);
        this.project.Context.FileSystem.File.WriteAllBytes(this.recordPath, record.ToBytes());
    }

    private ModelImportSettings ReadRecord()
        => ModelImportSettings.Parse(this.recordPath, this.project.Context.FileSystem.File.ReadAllBytes(this.recordPath));

    // The content pipeline's rule: the record is replaced only while it holds what was read.
    private sealed class RecordWriter(ModelContentTests test) : IModelImportRecordWriter
    {
        public int Saves { get; private set; }

        public Task<DocumentSaveResult> ReplaceAsync(AssetPath model, ModelImportSettings expected, ModelImportSettings replacement, CancellationToken cancellationToken)
        {
            if (!test.ReadRecord().ToBytes().AsSpan().SequenceEqual(expected.ToBytes()))
            {
                return Task.FromResult(new DocumentSaveResult(DocumentSaveOutcome.Conflict, "The record changed."));
            }

            test.WriteRecord(replacement);
            this.Saves++;
            return Task.FromResult(new DocumentSaveResult(DocumentSaveOutcome.Saved));
        }
    }
}
