// Distributed under the MIT License. See accompanying file LICENSE or copy
// at https://opensource.org/licenses/MIT.
// SPDX-License-Identifier: MIT

using CommunityToolkit.Mvvm.Messaging;
using DroidNet.Controls;
using DroidNet.Documents;
using DroidNet.Routing;
using Microsoft.Extensions.Logging;
using Moq;
using Oxygen.Editor.Projects;
using Oxygen.Editor.World;
using Oxygen.Editor.World.Serialization;
using Oxygen.Editor.WorldEditor.SceneExplorer.Operations;
using Oxygen.Editor.WorldEditor.Services;

namespace Oxygen.Editor.WorldEditor.SceneExplorer.Tests.Infrastructure;

internal static class SceneExplorerViewModelTestFixture
{
    public static (TestSceneExplorerViewModel vm, Scene scene, Mock<ISceneMutator> mutator, Mock<ISceneOrganizer> organizer, IMessenger messenger, Mock<ISceneEngineSync> engineSync) CreateViewModel()
    {
        var loggerFactory = LoggerFactory.Create(builder =>
        {
            builder.SetMinimumLevel(LogLevel.Debug);
            builder.AddDebug();
            builder.AddConsole();
        });

        var messenger = new WeakReferenceMessenger();
        var project = new Mock<IProject>();
        var scene = new Scene(project.Object) { Name = "Scene" };
        project.Setup(p => p.Scenes).Returns(new List<Scene> { scene });
        project.SetupProperty(p => p.ActiveScene, scene);

        var projectManager = new Mock<IProjectManagerService>();
        projectManager.Setup(pm => pm.CurrentProject).Returns(project.Object);
        projectManager.Setup(pm => pm.LoadSceneAsync(It.IsAny<Scene>())).ReturnsAsync(true);

        var documentService = new Mock<IDocumentService>();
        var router = new Mock<IRouter>();
        var engineSync = new Mock<ISceneEngineSync>();
        engineSync.Setup(es => es.SyncSceneAsync(It.IsAny<Scene>())).Returns(Task.CompletedTask);
        engineSync.Setup(es => es.CreateNodeAsync(It.IsAny<SceneNode>(), It.IsAny<Guid?>())).Returns(Task.CompletedTask);
        engineSync.Setup(es => es.ReparentNodeAsync(It.IsAny<Guid>(), It.IsAny<Guid?>())).Returns(Task.CompletedTask);
        engineSync.Setup(es => es.RemoveNodeAsync(It.IsAny<Guid>())).Returns(Task.CompletedTask);
        var mutator = new Mock<ISceneMutator>();
        mutator.Setup(m => m.CreateNodeAtRoot(It.IsAny<SceneNode>(), It.IsAny<Scene>()))
            .Returns((SceneNode n, Scene s) =>
            {
                if (!s.RootNodes.Contains(n))
                {
                    s.RootNodes.Add(n);
                }

                return new SceneNodeChangeRecord("CreateNodeAtRoot", n, null, null, true, true, false);
            });
        mutator.Setup(m => m.CreateNodeUnderParent(It.IsAny<SceneNode>(), It.IsAny<SceneNode>(), It.IsAny<Scene>()))
            .Returns((SceneNode n, SceneNode p, Scene s) =>
            {
                if (!p.Children.Contains(n))
                {
                    p.Children.Add(n);
                }

                return new SceneNodeChangeRecord("CreateNodeUnderParent", n, null, p.Id, true, false, false);
            });
        mutator.Setup(m => m.RemoveNode(It.IsAny<Guid>(), It.IsAny<Scene>()))
            .Returns((Guid id, Scene s) =>
            {
                var node = s.AllNodes.FirstOrDefault(n => n.Id == id);
                if (node?.Parent is null)
                {
                    _ = s.RootNodes.Remove(node!);
                }
                else
                {
                    _ = node.Parent.Children.Remove(node);
                }

                return new SceneNodeChangeRecord("RemoveNode", node ?? new SceneNode(s) { Name = "Temp" }, id, null, true, false, true);
            });
        mutator.Setup(m => m.ReparentNode(It.IsAny<Guid>(), It.IsAny<Guid?>(), It.IsAny<Guid?>(), It.IsAny<Scene>()))
            .Returns((Guid nodeId, Guid? oldParent, Guid? newParent, Scene s) =>
            {
                var node = s.AllNodes.FirstOrDefault(n => n.Id == nodeId) ?? new SceneNode(s) { Name = "Temp" };

                if (node.Parent is not null)
                {
                    _ = node.Parent.Children.Remove(node);
                }
                else
                {
                    _ = s.RootNodes.Remove(node);
                }

                if (newParent is null)
                {
                    s.RootNodes.Add(node);
                }
                else
                {
                    var parentNode = s.AllNodes.FirstOrDefault(p => p.Id == newParent.Value) ?? new SceneNode(s) { Name = "TempParent" };
                    if (!s.RootNodes.Contains(parentNode) && parentNode.Parent is null)
                    {
                        s.RootNodes.Add(parentNode);
                    }
                    parentNode.Children.Add(node);
                }

                return new SceneNodeChangeRecord("ReparentNode", node, oldParent, newParent, true, false, false);
            });

        var organizer = new Mock<ISceneOrganizer>();
        organizer.Setup(o => o.MoveNodeToFolder(It.IsAny<Guid>(), It.IsAny<Guid>(), It.IsAny<Scene>()))
            .Returns((Guid n, Guid f, Scene s) => new LayoutChangeRecord("MoveNodeToFolder", [], [], null, null));

        // Setup for FilterTopLevelSelectedNodeIds
        organizer.Setup(o => o.FilterTopLevelSelectedNodeIds(It.IsAny<HashSet<Guid>>(), It.IsAny<Scene>()))
            .Returns((HashSet<Guid> ids, Scene s) => ids); // Simple pass-through for flat tests

        // Setup for CloneLayout
        organizer.Setup(o => o.CloneLayout(It.IsAny<IList<ExplorerEntryData>?>()))
            .Returns((IList<ExplorerEntryData>? layout) =>
            {
                if (layout == null) return null;
                // Shallow copy for tests is usually enough if we don't mutate deep structure
                // But let's do a slightly better job
                return layout.Select(e => new ExplorerEntryData
                {
                    Type = e.Type,
                    NodeId = e.NodeId,
                    FolderId = e.FolderId,
                    Name = e.Name,
                    Children = e.Children != null ? new List<ExplorerEntryData>(e.Children) : null,
                    IsExpanded = e.IsExpanded
                }).ToList();
            });

        // Setup for GetExpandedFolderIds
        organizer.Setup(o => o.GetExpandedFolderIds(It.IsAny<IList<ExplorerEntryData>?>()))
            .Returns((IList<ExplorerEntryData>? layout) =>
            {
                var ids = new HashSet<Guid>();
                if (layout == null) return ids;

                void Visit(IEnumerable<ExplorerEntryData> entries)
                {
                    foreach (var e in entries)
                    {
                        if (string.Equals(e.Type, "Folder", StringComparison.OrdinalIgnoreCase) && e.IsExpanded == true && e.FolderId.HasValue)
                        {
                            ids.Add(e.FolderId.Value);
                        }
                        if (e.Children != null)
                        {
                            Visit(e.Children);
                        }
                    }
                }
                Visit(layout);
                return ids;
            });

        // Setup for EnsureLayoutContainsNodes
        organizer.Setup(o => o.EnsureLayoutContainsNodes(It.IsAny<Scene>(), It.IsAny<IEnumerable<Guid>>()))
            .Callback((Scene s, IEnumerable<Guid> ids) =>
            {
                s.ExplorerLayout ??= s.RootNodes.Select(n => new ExplorerEntryData { Type = "Node", NodeId = n.Id }).ToList();
            });

        // Setup for BuildFolderOnlyLayout
        organizer.Setup(o => o.BuildFolderOnlyLayout(It.IsAny<LayoutChangeRecord>()))
            .Returns((LayoutChangeRecord r) =>
            {
                var list = r.PreviousLayout != null ? new List<ExplorerEntryData>(r.PreviousLayout) : new List<ExplorerEntryData>();
                if (r.NewFolder != null)
                {
                    list.Insert(0, new ExplorerEntryData
                    {
                        Type = "Folder",
                        FolderId = r.NewFolder.FolderId,
                        Name = r.NewFolder.Name,
                        Children = new List<ExplorerEntryData>(),
                        IsExpanded = r.NewFolder.IsExpanded
                    });
                }
                return list;
            });

        organizer.Setup(o => o.CreateFolderFromSelection(It.IsAny<HashSet<Guid>>(), It.IsAny<Scene>(), It.IsAny<SceneAdapter>()))
            .Returns((HashSet<Guid> ids, Scene s, SceneAdapter _) =>
            {
                var previousLayout = s.ExplorerLayout?.ToList()
                                     ?? s.RootNodes.Select(n => new ExplorerEntryData { Type = "Node", NodeId = n.Id, IsExpanded = n.IsExpanded }).ToList();

                var folderId = Guid.NewGuid();
                var folderEntry = new ExplorerEntryData
                {
                    Type = "Folder",
                    FolderId = folderId,
                    Name = "New Folder",
                    Children = ids.Select(id => new ExplorerEntryData { Type = "Node", NodeId = id }).ToList(),
                    IsExpanded = true
                };

                // Create new layout: keep items NOT in selection, add new folder
                var newLayout = new List<ExplorerEntryData>();

                // Add existing items that are not being moved
                foreach (var entry in previousLayout)
                {
                    if (entry.NodeId.HasValue && ids.Contains(entry.NodeId.Value))
                    {
                        continue;
                    }
                    // Note: This simple mock doesn't handle nested folders/nodes recursively for removal,
                    // but sufficient for flat list tests.
                    newLayout.Add(entry);
                }

                newLayout.Insert(0, folderEntry); // Insert at top for simplicity
                s.ExplorerLayout = newLayout;

                return new LayoutChangeRecord(
                    OperationName: "CreateFolderFromSelection",
                    PreviousLayout: previousLayout,
                    NewLayout: newLayout,
                    NewFolder: folderEntry,
                    ModifiedFolders: new List<ExplorerEntryData> { folderEntry },
                    ParentLists: new List<IList<ExplorerEntryData>> { newLayout });
            });

        var vm = new TestSceneExplorerViewModel(
            projectManager.Object,
            messenger,
            router.Object,
            documentService.Object,
            engineSync.Object,
            mutator.Object,
            organizer.Object,
            loggerFactory);

        return (vm, scene, mutator, organizer, messenger, engineSync);
    }

    public static (TestSceneExplorerViewModel vm, Scene scene, Mock<ISceneMutator> mutator, ISceneOrganizer organizer, IMessenger messenger, Mock<ISceneEngineSync> engineSync) CreateIntegrationViewModel()
    {
        var loggerFactory = LoggerFactory.Create(builder =>
        {
            builder.SetMinimumLevel(LogLevel.Debug);
            builder.AddDebug();
            builder.AddConsole();
        });

        var messenger = new WeakReferenceMessenger();
        var project = new Mock<IProject>();
        var scene = new Scene(project.Object) { Name = "Scene" };
        project.Setup(p => p.Scenes).Returns(new List<Scene> { scene });
        project.SetupProperty(p => p.ActiveScene, scene);

        var projectManager = new Mock<IProjectManagerService>();
        projectManager.Setup(pm => pm.CurrentProject).Returns(project.Object);
        projectManager.Setup(pm => pm.LoadSceneAsync(It.IsAny<Scene>())).ReturnsAsync(true);

        var documentService = new Mock<IDocumentService>();
        var router = new Mock<IRouter>();
        var engineSync = new Mock<ISceneEngineSync>();
        engineSync.Setup(es => es.SyncSceneAsync(It.IsAny<Scene>())).Returns(Task.CompletedTask);
        engineSync.Setup(es => es.CreateNodeAsync(It.IsAny<SceneNode>(), It.IsAny<Guid?>())).Returns(Task.CompletedTask);
        engineSync.Setup(es => es.ReparentNodeAsync(It.IsAny<Guid>(), It.IsAny<Guid?>())).Returns(Task.CompletedTask);
        engineSync.Setup(es => es.RemoveNodeAsync(It.IsAny<Guid>())).Returns(Task.CompletedTask);

        var mutator = new Mock<ISceneMutator>();

        mutator.Setup(m => m.CreateNodeAtRoot(It.IsAny<SceneNode>(), It.IsAny<Scene>()))
            .Returns((SceneNode n, Scene s) =>
            {
                if (!s.RootNodes.Contains(n)) s.RootNodes.Add(n);
                return new SceneNodeChangeRecord("CreateNodeAtRoot", n, null, null, true, true, false);
            });
        mutator.Setup(m => m.CreateNodeUnderParent(It.IsAny<SceneNode>(), It.IsAny<SceneNode>(), It.IsAny<Scene>()))
            .Returns((SceneNode n, SceneNode p, Scene s) =>
            {
                if (!p.Children.Contains(n)) p.Children.Add(n);
                return new SceneNodeChangeRecord("CreateNodeUnderParent", n, null, p.Id, true, false, false);
            });
        mutator.Setup(m => m.RemoveNode(It.IsAny<Guid>(), It.IsAny<Scene>()))
            .Returns((Guid id, Scene s) =>
            {
                var node = s.AllNodes.FirstOrDefault(n => n.Id == id);
                if (node != null)
                {
                    if (node.Parent is null) s.RootNodes.Remove(node);
                    else node.Parent.Children.Remove(node);
                }
                return new SceneNodeChangeRecord("RemoveNode", node ?? new SceneNode(s) { Name = "Temp" }, id, null, true, false, true);
            });
        mutator.Setup(m => m.ReparentNode(It.IsAny<Guid>(), It.IsAny<Guid?>(), It.IsAny<Guid?>(), It.IsAny<Scene>()))
            .Returns((Guid nodeId, Guid? oldParent, Guid? newParent, Scene s) =>
            {
                var node = s.AllNodes.FirstOrDefault(n => n.Id == nodeId);
                if (node == null) return new SceneNodeChangeRecord("ReparentNode", new SceneNode(s) { Name = "Temp" }, oldParent, newParent, false, false, false);

                if (node.Parent != null) node.Parent.Children.Remove(node);
                else s.RootNodes.Remove(node);

                if (newParent == null)
                {
                    s.RootNodes.Add(node);
                }
                else
                {
                    var parentNode = s.AllNodes.FirstOrDefault(p => p.Id == newParent.Value);
                    if (parentNode != null) parentNode.Children.Add(node);
                }
                return new SceneNodeChangeRecord("ReparentNode", node, oldParent, newParent, true, false, false);
            });

        var organizer = new SceneOrganizer(loggerFactory.CreateLogger<SceneOrganizer>());

        var vm = new TestSceneExplorerViewModel(
            projectManager.Object,
            messenger,
            router.Object,
            documentService.Object,
            engineSync.Object,
            mutator.Object,
            organizer,
            loggerFactory);

        return (vm, scene, mutator, organizer, messenger, engineSync);
    }

    internal sealed class TestSceneExplorerViewModel : SceneExplorerViewModel
    {
        public TestSceneExplorerViewModel(
            IProjectManagerService projectManager,
            IMessenger messenger,
            IRouter router,
            IDocumentService documentService,
            ISceneEngineSync sceneEngineSync,
            ISceneMutator sceneMutator,
            ISceneOrganizer sceneOrganizer,
            ILoggerFactory? loggerFactory = null)
            : base(projectManager, messenger, router, documentService, sceneEngineSync, sceneMutator, sceneOrganizer, loggerFactory)
        {
        }

        public void InvokeHandleItemBeingAdded(Scene scene, LayoutNodeAdapter adapter, ITreeItem parent, TreeItemBeingAddedEventArgs args)
            => this.HandleItemBeingAdded(scene, adapter, parent, args);

        public void InvokeHandleItemBeingRemoved(Scene scene, LayoutNodeAdapter adapter, TreeItemBeingRemovedEventArgs args)
            => this.HandleItemBeingRemoved(scene, adapter, args);

        public void InvokeOnItemBeingRemoved(TreeItemBeingRemovedEventArgs args)
            => this.OnItemBeingRemoved(this, args);

        public Task InvokeHandleItemAddedAsync(TreeItemAddedEventArgs args)
            => this.HandleItemAddedAsync(args);

        public Task InvokeHandleItemRemovedAsync(TreeItemRemovedEventArgs args)
            => this.HandleItemRemovedAsync(args);

        public Task InvokeExpandAndSelectFolderAsync(FolderAdapter folderAdapter, ExplorerEntryData folderEntry)
            => this.ExpandAndSelectFolderAsync(folderAdapter, folderEntry);

        public void InvokeRegisterCreateFolderUndo(SceneAdapter sceneAdapter, Scene scene, LayoutChangeRecord layoutChange, string folderName)
            => this.RegisterCreateFolderUndo(sceneAdapter, scene, layoutChange, folderName);

        public Task AddFolderToSceneAdapter(SceneAdapter sceneAdapter, FolderAdapter folderAdapter)
            => this.InsertItemAsync(0, sceneAdapter, folderAdapter);

        public Task InvokeInsertItemAsync(int index, ITreeItem parent, ITreeItem item)
            => this.InsertItemAsync(index, parent, item);

        public Task InitializeSceneAsync(SceneAdapter sceneAdapter)
            => this.InitializeRootAsync(sceneAdapter, skipRoot: false);

        public Task HandleDocumentOpenedForTestAsync(Scene scene)
            => this.HandleDocumentOpenedAsync(scene);

        public Task ExpandItemForTestAsync(ITreeItem item)
            => this.ExpandItemAsync(item);

        public Task RemoveSelectedItemsForTestAsync()
            => this.RemoveSelectedItems();
    }
}
