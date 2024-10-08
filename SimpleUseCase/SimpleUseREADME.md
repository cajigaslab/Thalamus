# Overview
This project demonstrates the functionality of Thalamus by monitoring the relationship between hand movement and heart rate. The objective was to investigate how physical exercise, specifically hand movements, correlates with changes in heart rate. During the experiment, subjects performed predefined hand movements while their heart rate was monitored. Synchronized time-series data of hand movements and heart rate was collected and stored. Data visualization tools within Thalamus were used to analyze correlations between physical activity and heart rate changes. The experiment illustrated Thalamus's capability to integrate and synchronize multimodal data streams effectively. 

# Setup
Motion Capture Sensor: Connected using the HAND_ENGINE node.
Heart Rate Monitor: Connected using the NIDAQ node. The monitor was plugged into an analog-in port on the NIDAQ.
Thalamus was configured to integrate data from both sensors:

# Nodes
HAND_ENGINE: Captures data from the motion capture sensor.
NIDAQ: Receives data from the heart rate monitor via NIDAQ. 
STORAGE: Saves collected data to a designated file path.
RUNNER: Synchronizes all nodes to ensure simultaneous data collection.

# Execution
1. Configure Thalamus nodes (HAND_ENGINE, NIDAQ, STORAGE, RUNNER) as described.
2. Connect and ensure proper functioning of motion capture sensor and heart rate monitor.
3. Start Thalamus to initiate data collection and synchronization.
4. Perform specific hand movements while continuously monitoring heart rate.
5. Data Collection and Analysis
