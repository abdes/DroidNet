# DroidNet DynamicTree

The DynamicTree is a flexible and customizable tree view control for WinUI 3. It's designed to handle dynamic and lazy-loaded data structures efficiently and provide a rich set of features for displaying and interacting with hierarchical information.

## Key Features

- **Dynamic Data Binding**: Supports binding to dynamic data models, allowing for real-time updates and efficient rendering.
- **Customizable Appearance**: Offers extensive styling options through XAML templates and resource dictionaries.
- **Selection Modes**: Supports single selection, multiple selection, and no selection modes with corresponding event handlers.
- **Expand/Collapse Animation**: Provides smooth animations when expanding or collapsing tree nodes.
- **Item Drag & Drop**: TODO: Enables drag-and-drop functionality for moving items within the tree or between trees.
- **Custom Data Templates**: Allows for custom data templates to display item content in various formats.
- **Events & Commands**: Offers a rich set of events and commands for handling user interactions and managing the tree's state.

## Usage

1. First, install the `DroidNet.Controls` package via dotnet CLI, nuget, etc:
    ```
    dotnet add package DroidNet.Controls.DynamicTree
    ```

2. Import the required namespaces in your XAML file:
    ```xml
    <Page
        ...
        xmlns:dnc="using:DroidNet.Controls"
        ...>
    </Page>
    ```

3. Add a DynamicTree control to your XAML layout, and configure its behavior using properties such as `ItemsSource`, `ItemTemplate`, and various selection-related settings.
   ```xml
   <droidnet:DynamicTree x:Name="treeView"
                          ItemsSource="{Binding TreeData}"
                          ItemTemplate="{StaticResource MyItemTemplate}"
                          SelectionMode="Multiple">
       <!-- Configure additional properties here -->
   </droidnet:DynamicTree>
   ```

4. Create and bind your data model to the `ItemsSource` property of the DynamicTree control.

5. Optionally, style the DynamicTree using XAML templates or resource dictionaries to customize its appearance.

## Samples & Examples

Explore the [DroidNet Controls demo project](../DemoApp/README.md) for a working example demonstrating various features and use cases of the DynamicTree control.

*Happy coding! 🚀*
