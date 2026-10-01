// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using AwesomeAssertions;
using DroidNet.Controls;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.ContentPipeline;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.World;
using Testably.Abstractions;
using Windows.UI;

namespace Oxygen.Editor.MaterialEditor.Tests;

[TestClass]
public sealed partial class MaterialColorHistoryTests
{
    public TestContext TestContext { get; set; } = null!;

    /// <summary>Material color samples commit once or cancel completely without dirtying the document.</summary>
    /// <param name="samples">The number of preview samples.</param>
    /// <param name="cancel">Whether to cancel the edit session.</param>
    /// <returns>The test task.</returns>
    [TestMethod]
    [DataRow(1, false)]
    [DataRow(3, false)]
    [DataRow(3, true)]
    public async Task MaterialColorEditSessionPreservesHistory(int samples, bool cancel)
    {
        var directory = Directory.CreateTempSubdirectory("OxygenMaterialPicker-");
        try
        {
            var uri = new Uri("asset:///Content/Materials/Picker.omat.json");
            var resolver = new Mock<IMaterialSourcePathResolver>();
            _ = resolver.Setup(value => value.Resolve(uri)).Returns(new MaterialSourceLocation(uri, directory.FullName, "Content", Path.Combine(directory.FullName, "Picker.omat.json"), "Picker.omat.json"));
            var service = new MaterialDocumentService(Moq.Mock.Of<Oxygen.Editor.ContentPipeline.Cooking.IAutomaticCookService>(), resolver.Object, Mock.Of<IMaterialCookService>(), new Oxygen.Editor.ContentPipeline.Snapshots.CookDocumentRegistry(), new NativeAtomicFileStore(new RealFileSystem()));
            var document = await service.CreateAsync(uri, this.TestContext.CancellationToken).ConfigureAwait(true);
            await service.CloseAsync(document.DocumentId, discard: false, this.TestContext.CancellationToken).ConfigureAwait(true);
            using var model = new MaterialEditorViewModel(new MaterialDocumentMetadata(uri), service, Oxygen.Testing.AssetStatusFixture.EmptyProvider, System.Reactive.Concurrency.ImmediateScheduler.Instance);
            await model.PrepareForCloseAsync().ConfigureAwait(true);
            model.ResumeEditing();
            try
            {
                await CheckMaterialColorGestureAsync(model, samples, cancel).ConfigureAwait(true);
            }
            finally
            {
                await model.CloseAsync(discard: true).ConfigureAwait(true);
            }
        }
        finally
        {
            directory.Delete(recursive: true);
        }
    }

    private static async Task CheckMaterialColorGestureAsync(MaterialEditorViewModel model, int samples, bool cancel)
    {
        var before = model.BaseColorColor;
        model.BeginEditSession("Base color", NumberBoxEditInteractionKind.PointerDrag);
        var preview = default(Color);
        for (var sample = 1; sample <= samples; sample++)
        {
            preview = Color.FromArgb(255, (byte)(48 * sample), 80, 160);
            model.SetBaseColor(preview);
        }

        _ = model.BaseColorColor.Should().NotBe(before);
        _ = model.UndoCommand.CanExecute(parameter: null).Should().BeTrue("Undo can cancel the active material preview");
        model.EndEditSession(cancel ? NumberBoxEditCompletionKind.Cancel : NumberBoxEditCompletionKind.Commit);
        _ = model.BaseColorColor.Should().Be(cancel ? before : preview);
        _ = model.UndoCommand.CanExecute(parameter: null).Should().Be(!cancel);
        _ = model.IsDirty.Should().Be(!cancel);
        if (!cancel)
        {
            await model.UndoCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
            _ = model.BaseColorColor.Should().Be(before);
            _ = model.IsDirty.Should().BeFalse();
            _ = model.UndoCommand.CanExecute(parameter: null).Should().BeFalse("one edit session must create exactly one history entry");
            await model.RedoCommand.ExecuteAsync(parameter: null).ConfigureAwait(true);
            _ = model.BaseColorColor.Should().Be(preview);
            _ = model.IsDirty.Should().BeTrue();
        }
    }
}
