# Oxygen Clap Module

A modern, extensible, and type-safe C++20 command-line argument parsing library
for the Oxygen Engine. This guide covers both user and developer perspectives,
with a focus on architecture, usage, and extensibility.

---

## Table of Contents

- [Overview](#overview)
- [Quick Start](#quick-start)
- [Defining a CLI: The Fluent Builder
  API](#defining-a-cli-the-fluent-builder-api)
- [Parsing Logic and Architecture](#parsing-logic-and-architecture)
- [Advanced Features](#advanced-features)
- [Testing and Extending](#testing-and-extending)
- [Design Rationale](#design-rationale)
- [FAQ](#faq)
- [References](#references)

---

## Overview

Oxygen Clap provides a robust, modular, and highly testable way to define and
parse command-line interfaces (CLIs) in C++. It supports:

- Hierarchical commands and subcommands
- Short/long/positional options
- Type-safe value parsing
- Automatic help/version handling
- Extensible value semantics
- Modern C++20 idioms and best practices

---

## Quick Start

### 1. Add Clap to Your Project

Ensure your CMake project links to the Clap module. See the main project
`CMakeLists.txt` for details.

### 2. Define Your CLI

```cpp
#include <Oxygen/Clap/Fluent/DSL.h>
using namespace oxygen::clap;

int main(int argc, char** argv) {
  auto cli = CliBuilder()
    .ProgramName("myprog")
    .Version("1.0")
    .About("A sample CLI application.")
    .WithHelpCommand()
    .WithVersionCommand()
    .WithCommand(
      CommandBuilder("run")
        .About("Run the program")
        .WithOption(
          OptionBuilder("verbose")
            .Short("v").Long("verbose")
            .WithValue<bool>()
            .Build()
        )
        .Build()
    )
    .Build();

  auto context = cli->Parse(argc, argv);
  // Use context.active_command, context.ovm, etc.
}
```

---

## Defining a CLI: The Fluent Builder API

Clap uses a fluent builder pattern for all major components:

- **CliBuilder**: Configures the overall CLI (program name, version, about,
  commands, help/version commands, output width with auto detection).
- **CommandBuilder**: Defines commands and subcommands, their descriptions, and
  options.
- **OptionBuilder**: Defines options (short/long/positional), their value types,
  and semantics.
- **OptionValueBuilder**: Specifies value type, default/implicit values,
  repeatability, and storage.
- **PositionalOptionBuilder**: Specializes for positional arguments.

### Example: Adding Options and Positionals

```cpp
auto run_cmd = CommandBuilder("run")
  .About("Run the program")
  .WithOption(
    OptionBuilder("input")
      .Short("i").Long("input")
      .WithValue<std::string>()
      .Required()
      .Build()
  )
  .WithPositionalArguments(
    Option::Positional("file")
      .About("Input file")
      .WithValue<std::string>()
      .Build()
  )
  .Build();
```

---

## Parsing Logic and Architecture

### 1. Tokenization

- The `Tokenizer` class converts raw command-line arguments into a stream of
  typed tokens (short/long options, values, dashes, etc.).

### 2. State Machine Parser

- Parsing is performed by a state machine, with each state (e.g.,
  `InitialState`, `IdentifyCommandState`, `ParseOptionsState`) handling a
  specific phase.
- The parser context (`ParserContext`) holds all relevant state, including the
  list of commands, the active command/option, and collected positional tokens.
- The parser matches the deepest possible command path, then options and
  positionals.

#### State Machine Details

The Clap parser uses a modular state machine to process CLI arguments. Each
state is responsible for a specific phase of parsing, and transitions are
determined by the type of token encountered. The main states are:

- **InitialState**: Entry point. Determines if the first token matches a command
  or should be treated as an option/positional. If a default command exists, may
  transition directly to option parsing.
- **IdentifyCommandState**: Attempts to match the longest possible command path
  from the input tokens. Sets the active command if found, or errors if not.
  Once a command is identified, transitions to option parsing.
- **ParseOptionsState**: Handles parsing of options and positional arguments for
  the active command. Collects positional tokens and recognizes when to
  transition to option-specific states.
- **ParseShortOptionState / ParseLongOptionState**: Handle parsing of short
  (`-o`) and long (`--option`) options, respectively. Validate option existence,
  parse values if required, and update the context.
- **DashDashState**: Handles the special `--` token, which signals the end of
  options. All subsequent tokens are treated as positional arguments.
- **FinalState**: Indicates the end of input. Finalizes parsing and processes
  any remaining positional tokens.

**State Transitions:**

- Transitions are determined by the type of token (option, value, dash, end of
  input, etc.).
- Each state documents its expected context and the actions it performs.
- Errors (such as unrecognized commands or options) cause transitions to
  error/termination states, with diagnostic output.

**Why this design?**

- Each state is responsible for a specific parsing phase, making the logic easy
  to reason about and extend.
- New states or transitions can be added for advanced CLI features without
  disrupting existing logic.
- The modular state machine design is highly testable and robust.

**Summary Table:**

| State | Purpose | Typical Next State(s) |
| --- | --- | --- |
| InitialState | Entry, command/option/positional detection | IdentifyCommand, ParseOptions |
| IdentifyCommandState | Match command path | ParseOptions, Error, Final |
| ParseOptionsState | Parse options/positionals | ParseShort/LongOption, DashDash, Final |
| ParseShortOptionState | Handle short options | ParseOptions, Error |
| ParseLongOptionState | Handle long options | ParseOptions, Error |
| DashDashState | Handle `--` (end of options) | ParseOptions, Final |
| FinalState | End of input, finalize parsing | (none) |

### 3. Value Semantics

- Each option is associated with a `ValueSemantics` object, which defines how
  its value is parsed, validated, and stored (type, required, repeatable,
  default/implicit value, etc.).
- All parsed values are stored in an `OptionValuesMap` for type-safe retrieval.

### 4. Contextual Result

- The result of parsing is a `CommandLineContext` object, containing the active
  command, parsed option values, and I/O streams for further interaction.

---

## Advanced Features

- **Hierarchical Commands**: Support for subcommands and multi-level command
  paths.
- **Automatic Help/Version**: Built-in support for `--help`, `-h`, `help`,
  `--version`, `-v`, and `version` commands/options.
- **Global Options**: Declare options that apply to all commands using
  `WithGlobalOptions` or `WithGlobalOption`. Global options must appear before
  any command (default or named). If a global option name conflicts with a
  command option, the command option takes precedence. Help output lists global
  options in a separate section before command options.
- **Theme Selection**: Set a predictable output style via
  `CliBuilder::WithTheme(CliTheme::Plain())` (or `Dark()`/`Light()`) for help and
  usage formatting. To allow users to select a theme at runtime, enable the
  pre-configured global option with `CliBuilder::WithThemeSelectionOption()`.
- **Notifiers**: Register callbacks for option values using
  `CallOnEachValue` (per occurrence) and `CallOnFinalValue` (after parsing).
- **Usage Footer**: Add a help footer with `CliBuilder::Footer("...")`.
- **Custom Value Semantics**: Extend `ValueSemantics` to support custom
  parsing/validation logic.
- **Extensible Builders**: Builders are facets-compatible and can be extended
  for custom needs.
- **Strong Type Safety**: All option values are type-erased with `std::any` but
  accessed in a type-safe way.
- **No Global State**: All state is managed within context objects, making the
  library suitable for embedding and testing.

---

## Testing and Extending

- **Unit Tests**: The module includes comprehensive scenario-based tests for all
  parser states, API surfaces, and edge cases. See `Test/` for examples.
- **Adding New Features**: Extend the builder classes or parser states as
  needed. Follow the project’s C++20 and documentation conventions.
- **Custom Value Types**: Implement your own `ValueSemantics` if you need
  special parsing or validation.

---

## Design Rationale

- **State Machine Parsing**: Enables robust, maintainable, and extensible
  parsing logic, especially for complex CLI grammars.
- **Fluent Builder API**: Provides a declarative, readable, and type-safe way to
  define CLIs.
- **Separation of Concerns**: Tokenization, parsing, and value handling are
  cleanly separated.
- **Extensibility**: The architecture is designed for easy extension and
  customization.
- **Documentation and Testing**: Strict Doxygen and test rules ensure
  maintainability and reliability.

---

## FAQ

**Q: How do I add a new command or option?**
A: Use the builder API (`WithCommand`, `WithOption`, etc.) and chain methods to
configure your CLI.

**Q: How do I access parsed values?**
A: After parsing, use `context.ovm.ValuesOf("option_name")` and `GetAs<T>()` to
retrieve values.

**Q: How do I add custom validation?**
A: Implement a custom `ValueSemantics` class and attach it to your option via
the builder.

**Q: How do I test my CLI?**
A: Add tests in the `Test/` directory, following the scenario-based, AAA-pattern
required by the project.

---

## References

- [Oxygen Engine Main README](../../../../README.md)
- [Design Documents](../../../design/)
- [Unit Test
  Instructions](../../../../.github/instructions/unit_tests.instructions.md)
- [C++ Coding
  Style](../../../../.github/instructions/cpp_coding_style.instructions.md)
- [Doxygen Doc
  Comments](../../../../.github/instructions/doc_comments.instructions.md)

---

## TODOs and Open Work

| Area/Feature | Status | Description |
| --- | --- | --- |
| CLI output width config | ✅ | Auto by default via `WithAutoOutputWidth()`, with `OutputWidth()` taking precedence. |
| Global CLI options | ✅ | Configurable via `WithGlobalOptions`/`WithGlobalOption`, apply before any command (default or named), listed separately before command options; command options take precedence on conflicts. |
| Help command (built-in) | ✅ | Built-in `help` command plus `-h/--help` option routing to the active command. |
| Version command (built-in) | ✅ | Built-in `version` command plus `-v/--version` option on the default command. |
| StoreTo/Finalize integration tests | ⏳ | Add end-to-end tests that verify `StoreTo`/`CallOnFinalValue` behavior during parsing. |
| Value type parsers documentation | ✅ | Documented in [Detail/ParseValue.h](Detail/ParseValue.h). |
| Value type parsers coverage | ✅ | Core numeric, bool, char, string-like, enums, and chrono durations are supported. |
| Repeatable value parsing tests | ⏳ | Add tests for multiple occurrences and illegal repeats. |
| Required value parsing tests | ⏳ | Add tests for missing required options/positionals without defaults. |
| Callback interface refactor | ✅ | Per-value notifier (`CallOnEachValue`) and final notifier (`CallOnFinalValue`) are supported; per-value fires on each parsed occurrence, final fires after parsing/defaults. |
| Multi-token value support | ⏳ | Support values that span multiple tokens (e.g., `--opt a b`). |
| Notifiers/store-to finalization | ✅ | Final values are propagated after parsing via `Option::FinalizeValue`. |
| Error type name reporting | ✅ | Invalid value errors include the expected type name. |
| OptionValuesMap tests | ✅ | Comprehensive unit tests are present. |
| Usage footer support | ✅ | CLI usage footer output and tests are implemented. |
| Multiple command names | ⏳ | Add support and tests for commands with multiple names/aliases. |
| Consolidate styled wrapping | ✅ | Unified styled text wrapping across CLI, command, and option printers. |

---

## Future Enhancements

| Priority | Effort | Enhancement | Description |
| --- | --- | --- | --- |
| 1 | Medium | Shell completion generation | Generate shell completion scripts (bash, zsh, fish, PowerShell) from the CLI definition. |
| 2 | Low | Environment variable support | Enable options to be set via environment variables, with clear precedence rules. |
| 3 | Medium | Config file integration | Allow loading default option values from configuration files (YAML, JSON, INI), merging with CLI arguments. |
| 4 | High | Rich error reporting & suggestions | Implement typo correction and suggestions for mistyped commands/options. Provide contextual error messages with actionable hints. |
| 5 | Medium | Advanced validation and constraints | Allow declarative constraints (e.g., mutually exclusive options, required groups, value ranges) with clear error reporting. |
| 6 | Medium | Interactive mode | Optionally prompt for missing required arguments interactively if not provided on the command line. |
| 7 | Low | Command/option deprecation | Mark commands or options as deprecated, with warnings and migration hints. |
| 8 | High | Dynamic option/command registration | Support registering commands and options at runtime (e.g., for plugin systems or extensible tools). |

---

For further questions, consult the design documents or open an issue.
