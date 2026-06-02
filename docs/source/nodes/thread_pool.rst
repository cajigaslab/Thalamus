THREAD_POOL
===========

The THREAD_POOL node exposes Thalamus's shared worker thread pool as a node.  It is
a system/diagnostic node that lets you observe the pool that background work is
dispatched to.

Properties
----------

* **Idle Threads**: The number of currently idle worker threads, sampled and
  reported continuously as a diagnostic.
