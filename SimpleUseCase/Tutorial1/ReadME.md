# Overview
Follow this tutorial to run the synchronicity test use case. This can be used to measure the latency in motion capture.

# Setup
First open Thalamus using the following command in the command prompt: *python -m thalamus.task_controller --config tutorial1.json.* After Thalamus opens you should see the following window without the Camera section:

<img src="https://github.com/user-attachments/assets/6c460c7a-2a4c-47d4-909c-ab1f9c67800c" alt="Thalamus Window" width="400"/>


Next, connect the National Instruments DAQ (NIDAQ) (1000 Hz sample rate) attached to a push-button to the computer running Thalamus. Fit the motion capture glove (120 Hz sample rate) on the participant. Then, open the HandEngine software [https://stretchsense.com/] on the computer. To ensure Thalamus is able to connect to HandEngine you need to input the correct local host address specific to your HandEngine software. In our case, our local host number is 9000.

# Data Collection

To run, just check the "Running" box under the RUNNER node. Instruct the participant to press down on the push-button and quickly release, which will cause a voltage drop and subsequent rise that is acquired by the NIDAQ. Simultaneously, motion in the glove is captured by finger acceleration with the pose of the finger defined as the distance between the proximal phalanx and the distal phalanx. To stop running, uncheck the "Running" box under the RUNNER node. 

# Hydration
Once you have acquired the data, it will be saved as a capture file to the path in which Thalamus was opened. Before you can use the data, you need to hydrate it with the built in hydration tool which will convert it to an **HDF5 file**. To run the hydration tool, input the following into the command prompt: *python -m thalamus.hydrate filename.* 

Note: The hydrated file can be downloaded from this guide.

# Analysis
Latency, in this test, is defined as the time between the onset of voltage rise and the corresponding increase in finger acceleration detected by the glove. To visualize the latency period, we recommend graphing the data in Matlab as follows: time (s) on x-axis, finger acceleration (m/s^2) on left y-axis, finger position (cm) on right y-axis, and voltage (V) also on right y-axis. See sample illustration of this plot below:

<img src="https://github.com/user-attachments/assets/e2688eba-5491-4d13-86fe-972f04bf9736" alt="Synchronicity Test Sample Graph" width="400"/>


