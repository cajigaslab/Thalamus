CHANNEL_PICKER
==============

The CHANNEL_PICKER node selects channels from one or more upstream nodes and
re-publishes them as a single, reordered output stream.  It is a transformer: use
it to pick out the channels you care about, combine channels from several sources,
and assign them stable output positions.

Usage
-----

Add the source nodes you want to pull from; the node tracks them under its
**Sources** configuration.  For each selected channel you assign an **Out Channel**
(its index/position in the output stream).  The node prevents two selected channels
from being mapped to the same output position, automatically resolving conflicts to
the next free index.

The result is a clean, consistently ordered stream that downstream nodes (such as
STORAGE2 or a data view) can subscribe to.
