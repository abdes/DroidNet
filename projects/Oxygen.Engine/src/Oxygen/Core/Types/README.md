# Core Types

These are types that are used across many layers of the Oxygen engine. They must
be lightweight, have no dependency on the graphics backend and defined in
self-contained way.

When applicable, a `to_string()` converter should be provided. If the type is
intended to be used in hashtables, a `std::hash()` compatible hasher must be
provided.
