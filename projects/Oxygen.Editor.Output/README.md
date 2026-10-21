# Oxygen.Editor.Output

The Output panel: a log view over the editor's output buffer, which the
application fills with the editor's log and its operation results.

`OutputModule` contributes the panel (`log`) to each workspace window and
registers its view model and view there. From the window's start it adds the
player's output (`IPlayerLog`, implemented by LivePreview's play session) to
the buffer, as entries of the `Player` source, whether or not the panel shows;
a new play session's start clears the buffer.

The panel presents the log only; producing it belongs to the editor's logging
and to LivePreview.
