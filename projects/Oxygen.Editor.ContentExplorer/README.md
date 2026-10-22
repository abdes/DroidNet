# Oxygen.Editor.ContentExplorer

The Content Explorer panel: the project's content folders and mounts in its
Content Folders pane, and the assets of the folders it shows in its assets pane,
with their search, filters, layouts, details and import dialogs.

The explorer shows what Assets' feed lists and the cook status the content
pipeline reports. It renames, moves, copies and deletes through ContentPipeline's
relocation, creates scripts, input files, scenes and materials through
ContentPipeline's asset authoring, and applies a confirmed change of the content
mounts through ContentPipeline's `IContentMountChanges`, which the project's
preview implements. It opens documents and shows panels through the Shell's
placement, and inspects cooked output through the Shell's `ICookedInspections`,
so it references none of the panels that do those.

For the window's other parts it implements the Shell's `IAssetReveal`, which
shows and selects assets, and Scripting's `IScriptCreation`, which creates a
script in the folder the user picks. Its panes call each other through
`IContentFoldersPane`, which the Content Folders pane implements: the assets
pane asks it to show a folder or to create one. The explorer sends and receives
no messages.

## Layout

| Folder                  | Holds                                                                                                      |
| ----------------------- | ---------------------------------------------------------------------------------------------------------- |
| `src/`                  | The module, the panel and its navigation, and the ports the explorer implements.                           |
| `Panes/ContentFolders/` | The Content Folders pane: the tree of content folders and mounts, and the mounts and priority dialogs.     |
| `Panes/Assets/`         | The assets pane: its rows, details, actions and layouts.                                                   |
| `State/`                | The explorer's search, filters, presentation and selection, shared by its panes.                           |
| `Importing/`            | The scene and texture import dialogs.                                                                      |
| `Relocation/`           | Rename, move, copy and delete as the user asks for them: names, confirmations and the outcome they see.    |
| `Authoring/`            | New Script, New Input File, New Scene and New Material: the name and folder dialog, then opening the file. |

The panel's view merges `Styles/GridSplitterStyles.xaml`; every other resource
it uses is WinUI's.
