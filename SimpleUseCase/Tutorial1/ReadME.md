# Overview
In this tutorial we will show you how to run the sychrony test use case.
# Setup
First open Thalamus using the following command in the command prompt: python -m thalamus.task_controller --config tutorial1.json. After Thalamus opens you should see this

![alt text](https://github.com/cajigaslab/Thalamus/blob/main/SimpleUseCase/Tutorial1/image1.png?raw=true)

Next we will plug in the National Instruments DAQ and a Genicam compatible camera to our computer hosting Thalamus. Then we will open the HandEngine software on our computer. To ensure Thalamus is able to connect to HandEngine we need to input the correct loclahost address specific to your HandEngine software. In our case our local host number is 9000.


To run we just check the runner node and to stop we uncheck it.

# Hydration
Once we have aquired the data it will be saved as a capture file to the path which Thalamus was opened in. Before we can use the data, we need to hydrate it with the built in hydration tool which will convert it to an HDF5 file. To run the hydration tool we can input the following into the command prompt: python -m thalamus.hydrate filename. 

Note: The hydrated file can be downloaded from this guide.
