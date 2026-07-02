TASK_CONTROLLER
===============

The TASK_CONTROLLER node connects Thalamus to an external behavioral
task-controller service over gRPC.  When the node's Running state changes it tells
the task controller to start or stop, so a behavioral paradigm can be driven in
lockstep with data acquisition (for example via a RUNNER2 node).

Properties
----------

* **Address**: The gRPC address of the task-controller service to connect to.
* **Running**: Start or stop the connected task controller.
