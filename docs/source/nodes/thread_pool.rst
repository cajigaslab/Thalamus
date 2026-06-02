THREAD_POOL
===========

The THREAD_POOL node exposes Thalamus's shared worker thread pool as a node.  It is
a system/diagnostic node: it lets you observe and control the pool that background
work is dispatched to.

Properties
----------

* **Running**: Whether the pool is active.
* **Idle Threads**: The number of currently idle worker threads (reported as a
  diagnostic).
