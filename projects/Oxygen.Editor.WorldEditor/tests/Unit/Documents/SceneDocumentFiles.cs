// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using System.Text.Json;
using AwesomeAssertions;
using DroidNet.Storage;
using DroidNet.Storage.Native;
using Moq;
using Oxygen.Editor.ContentPipeline.Authoring;
using Oxygen.Editor.Documents;
using Oxygen.Editor.MaterialEditor;
using Oxygen.Editor.Projects;
using Oxygen.Editor.Scenes.Authoring;
using Oxygen.Editor.Scenes.Documents;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Managed.Core;
using Testably.Abstractions;

namespace Oxygen.Editor.WorldEditor.Unit.Tests.Documents;

/// <summary>Scene and material files in the Content mount of a temporary project, opened as documents of their kinds.</summary>
internal sealed partial class SceneDocumentFiles : IDisposable
{
    private readonly string root = Path.Combine(Path.GetTempPath(), "OxygenSceneDocumentTests", Guid.NewGuid().ToString("N"));

    /// <summary>Initializes a new instance of the <see cref="SceneDocumentFiles"/> class.</summary>
    /// <param name="store">The file store documents read and write through, or the real one.</param>
    /// <param name="interaction">The lock and hide state the opened scenes' edits respect, if any.</param>
    public SceneDocumentFiles(IAtomicFileStore? store = null, ISceneNodeInteraction? interaction = null)
    {
        _ = Directory.CreateDirectory(Path.Combine(this.root, "Content"));
        var info = new ProjectInfo("Scene documents", Category.Games, this.root);
        this.Context = new ProjectContext
        {
            ProjectId = info.Id,
            Name = "Scene documents",
            Category = Category.Games,
            ProjectRoot = this.root,
            AuthoringMounts = [new ProjectMountPoint("Content", "Content")],
            LocalFolderMounts = [],
        };
        var project = new Mock<IProject>();
        _ = project.SetupGet(value => value.ProjectInfo).Returns(info);
        this.Project = project.Object;
        var projects = new Mock<IProjectManagerService>();
        _ = projects.SetupGet(value => value.CurrentProject).Returns(this.Project);
        this.ProjectManager = projects.Object;
        this.Documents = new OpenDocuments(
            [new SceneDocumentKind(this.Project, interaction), new MaterialDocumentKind(new MaterialValidation())],
            store ?? new NativeAtomicFileStore(new RealFileSystem()));
    }

    /// <summary>Gets the project manager, whose current project is <see cref="Project"/>.</summary>
    public IProjectManagerService ProjectManager { get; }

    /// <summary>Gets the project's content folders.</summary>
    public ProjectContext Context { get; }

    /// <summary>Gets the project the scenes belong to.</summary>
    public IProject Project { get; }

    /// <summary>Gets the open documents.</summary>
    public OpenDocuments Documents { get; }

    /// <summary>Describes a scene file, its nodes complete with their default components.</summary>
    /// <param name="id">The scene's identity.</param>
    /// <param name="name">The name the file records.</param>
    /// <param name="nodes">The names of the scene's root nodes.</param>
    /// <returns>What the file records.</returns>
    public static SceneData Data(SceneId id, string name, params string[] nodes)
    {
        var scene = new Scene(Mock.Of<IProject>()) { Name = name };
        foreach (var node in nodes)
        {
            scene.RootNodes.Add(new SceneNode(scene) { Name = node });
        }

        return scene.Dehydrate() with { Id = id };
    }

    /// <summary>Gets the file of a scene.</summary>
    /// <param name="name">The scene's name.</param>
    /// <returns>The scene's file.</returns>
    public DocumentFile FileOf(string name) => new(this.PathOf(name), new Uri($"asset:///Content/{name}.oscene.json"));

    /// <summary>Gets the path of a scene's file.</summary>
    /// <param name="name">The scene's name, after the folder it is in below the Content mount, if any.</param>
    /// <returns>The path.</returns>
    public string PathOf(string name) => Path.GetFullPath(Path.Combine(this.root, "Content", name + ".oscene.json"));

    /// <summary>Writes a scene file.</summary>
    /// <param name="name">The scene's name, after the folder it is in below the Content mount, if any.</param>
    /// <param name="data">What the file records.</param>
    public void Write(string name, SceneData data)
    {
        _ = Directory.CreateDirectory(Path.GetDirectoryName(this.PathOf(name))!);
        File.WriteAllBytes(this.PathOf(name), JsonSerializer.SerializeToUtf8Bytes(data, SceneJsonContext.Default.SceneData));
    }

    /// <summary>Writes a new material file below the Content mount.</summary>
    /// <param name="name">The material's name.</param>
    /// <returns>The material's file.</returns>
    public DocumentFile WriteMaterial(string name)
    {
        var path = Path.GetFullPath(Path.Combine(this.root, "Content", "Materials", name + ".omat.json"));
        _ = Directory.CreateDirectory(Path.GetDirectoryName(path)!);
        File.WriteAllText(path, AssetTemplates.Material(name));
        return new DocumentFile(path, new Uri($"asset:///Content/Materials/{name}.omat.json"));
    }

    /// <summary>Writes a file that may not be a scene.</summary>
    /// <param name="name">The scene's name.</param>
    /// <param name="text">The file's text.</param>
    public void WriteText(string name, string text) => File.WriteAllText(this.PathOf(name), text);

    /// <summary>Reads a scene file.</summary>
    /// <param name="name">The scene's name.</param>
    /// <returns>What the file records.</returns>
    public SceneData Read(string name)
        => JsonSerializer.Deserialize(File.ReadAllBytes(this.PathOf(name)), SceneJsonContext.Default.SceneData)!;

    /// <summary>Writes an empty scene file and opens it.</summary>
    /// <param name="name">The scene's name.</param>
    /// <param name="cancellationToken">Cancels opening the file.</param>
    /// <returns>The document and its scene content.</returns>
    public Task<(Document Document, SceneContent Content)> CreateAsync(string name, CancellationToken cancellationToken)
    {
        this.Write(name, Data(SceneId.New(), name));
        return this.OpenAsync(name, cancellationToken);
    }

    /// <summary>Opens a scene file, which must decode.</summary>
    /// <param name="name">The scene's name.</param>
    /// <param name="cancellationToken">Cancels opening the file.</param>
    /// <returns>The document and its scene content.</returns>
    public async Task<(Document Document, SceneContent Content)> OpenAsync(string name, CancellationToken cancellationToken)
    {
        var result = await this.Documents.OpenAsync(this.FileOf(name), cancellationToken).ConfigureAwait(false);
        _ = result.Document.Should().NotBeNull(result.Problem);
        return (result.Document!, (SceneContent)result.Document!.Content);
    }

    /// <inheritdoc/>
    public void Dispose()
    {
        this.Documents.Dispose();
        Directory.Delete(this.root, recursive: true);
    }
}
