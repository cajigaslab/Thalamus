# Overview
Follow this tutorial to run the **heart rate variability use case** to investigate how physical exercise, specifically hand movements, correlates with changes in heart rate.

Participants perform predefined hand movements while their heart rate was monitored. Synchronized time-series data of hand movements and heart rate was collected and stored. 

# Setup
First open Thalamus using the following command in the command prompt: *python -m thalamus.task_controller --config SimpleUseCase.json.* After Thalamus opens you should see the following window without the Camera section:

<img src="https://github.com/user-attachments/assets/6c460c7a-2a4c-47d4-909c-ab1f9c67800c" alt="Thalamus Window" width="400"/>


Next, connect the National Instruments DAQ (NIDAQ) (1000 Hz sample rate) attached to a heart rate monitor via an analog-in port to the computer running Thalamus. Fit the motion capture glove (120 Hz sample rate) on the participant. Then, open the HandEngine software [https://stretchsense.com/] on the computer. To ensure Thalamus is able to connect to HandEngine you need to input the correct local host address specific to your HandEngine software. In our case, our local host number is 9000.

# Data Collection

To run, just check the "Running" box under the RUNNER node. Instruct the participant to follow predefined hand movements, motion that is captured by gloves. Simultaneously their heart rate is monitored. To stop running, uncheck the "Running" box under the RUNNER node. 

# Hydration
Once you have acquired the data, it will be saved as a capture file to the path in which Thalamus was opened. Before you can use the data, you need to hydrate it with the built in hydration tool which will convert it to an **HDF5 file**. To run the hydration tool, input the following into the command prompt: *python -m thalamus.hydrate filename.* 

Note: The hydrated file can be downloaded from this guide.

# Analysis
XXX
