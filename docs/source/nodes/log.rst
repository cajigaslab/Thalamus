LOG
===

The LOG node receives text log messages and displays them in its widget.  It is a
consumer that gives an experiment a place to collect human-readable notes and
diagnostic messages on the same timeline as the data; when recorded, those messages
are stored as ``text`` records.

There are no configuration fields to set -- log text arrives from other nodes and
clients through the logging API, and from the node's own widget.
