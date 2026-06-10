OPHANIM
=======

The OPHANIM node drives an Ophanim panoramic / sphere projection display over gRPC.
It is a consumer that forwards display commands to the projection service so visual
stimuli can be presented in sync with the rest of the pipeline.

Properties
----------

* **Address**: The gRPC address of the Ophanim display service.
* **Running**: Connect to the display service and begin driving it.
