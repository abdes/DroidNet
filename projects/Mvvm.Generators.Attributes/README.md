# View to ViewModel wiring generator Attributes

## Introduction

The `Mvvm.Generators.Attributes` project is a part of the DroidNet.Mvvm framework, designed to facilitate the wiring of Views to ViewModels in .NET applications using source generators. This project provides attributes that can be used to decorate View classes with metadata, enabling automatic generation of boilerplate code required for MVVM pattern implementation.

### Key Features

- **ViewModelAttribute**: The primary attribute provided by this project is the `ViewModelAttribute`. This attribute is used to annotate View classes, specifying the corresponding ViewModel class. The source generator then augments the View class to implement the `IViewFor<T>` interface and adds a dependency property for the `ViewModel` property.

- **Source Generation**: By leveraging C# source generators, this project reduces the need for manual boilerplate code, making the development process more efficient and less error-prone.

### Example Usage

To use the `ViewModelAttribute`, simply decorate your View class with the attribute, specifying the ViewModel type:

```csharp
using DroidNet.Mvvm.Generators;

[ViewModel(typeof(MyViewModel))]
public partial class MyView : UserControl {
    // The source generator will augment this class
}
```

This will automatically generate the necessary code to bind the `MyView` class to the `MyViewModel` class, implementing the `IViewFor<MyViewModel>` interface and adding a `ViewModel` dependency property.
