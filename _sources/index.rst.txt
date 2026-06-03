.. Thalamus documentation master file.

Thalamus
========

.. raw:: html

   <div class="tha-hero">
     <div class="tha-eyebrow">Real-time · Closed-loop · Multimodal</div>
     <p><strong>Thalamus</strong> is an open-source platform for real-time, synchronized,
     closed-loop multimodal data capture &mdash; built for the demands of the operating
     room and the research lab.</p>
     <div class="tha-cta">
       <a class="tha-btn tha-btn--primary" href="quickstart.html">Quick Start &rarr;</a>
       <a class="tha-btn" href="concepts.html">How it works</a>
       <a class="tha-btn" href="nodes/index.html">Node reference</a>
       <a class="tha-btn" href="examples/index.html">Examples</a>
     </div>
   </div>

Thalamus assembles experiments from a **pipeline of nodes** — small, configurable
units that acquire, transform, record, or control data streams.  Recordings are
written to a compact ``.tha`` capture file and converted to analysis-ready formats
(HDF5, CSV, Parquet, ...) with the bundled tooling.

If you use Thalamus in your work, please cite our paper:
`Thalamus: a real-time, closed-loop platform for synchronized multimodal data
acquisition <https://www.nature.com/articles/s44172-026-00646-z>`_
(Communications Engineering, Nature).

.. toctree::
   :maxdepth: 2
   :caption: Contents:

   quickstart
   concepts
   examples/index
   nodes/index

Indices and tables
==================

* :ref:`genindex`
* :ref:`modindex`
* :ref:`search`
