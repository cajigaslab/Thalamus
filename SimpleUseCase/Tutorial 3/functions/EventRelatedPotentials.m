function [segmentedData,segTime,timeAxis,samplingRate] = EventRelatedPotentials(time, data, eventTimes, interval,samplingRate)
% Author: Qasim Qureshi May 2024

    % segmentDataAroundEvents - Segments data around each event time within a specified interval.
    %
    % Syntax: segmentedData = segmentDataAroundEvents(time, data, eventTimes, interval)
    %
    % Inputs:
    %   time       - A vector of time points corresponding to the samples in data
    %   data       - A matrix of size (channels x samples) containing the data
    %   eventTimes - A vector of event times around which data is to be segmented
    %   interval   - A 2-element vector [start, stop] specifying the interval around each event time
    %
    % Outputs:
    %   segmentedData - A 3D matrix of size (number of events x channels x samples) containing the segmented data

    % Validate inputs
    if length(interval) ~= 2
        error('Interval must be a 2-element vector [start, stop].');
    end
    
    startOffset = interval(1);
    stopOffset = interval(2);
    
    % Initialize the output
    numEvents = length(eventTimes);
    numChannels = size(data, 1);
    
if nargin < 5
    totalSamples = length(time);
    totalDuration = time(end) - time(1);
    samplingRate = totalSamples / totalDuration;
    disp(['Sampling rate: ' num2str(samplingRate)]);
end

    % Determine the number of samples in the interval
    sampleInterval = (stopOffset - startOffset) * samplingRate;
    sampleInterval = round(sampleInterval) + 1;
    
    % Initialize the output matrix
    segmentedData = zeros(numEvents, numChannels, sampleInterval);
    
    % Loop through each event time and segment the data
    for i = 1:numEvents
        %disp(i)
        eventTime = eventTimes(i);
        startTime = eventTime + startOffset;
        stopTime = eventTime + stopOffset;
        
        % Find the indices corresponding to the start and stop times
        [~, startIdx] = min(abs(time - startTime));
        [~, stopIdx] = min(abs(time - stopTime));
        
       % Ensure the indices are within bounds and have consistent length
        if stopIdx - startIdx + 1 ~= sampleInterval
            if stopIdx - startIdx + 1 > sampleInterval
                stopIdx = startIdx + sampleInterval - 1;
            else
                startIdx = stopIdx - sampleInterval + 1;
            end
        end
        % Extract the segment for each channel and store in the output matrix
        segmentedData(i, :, :) = data(:, startIdx:stopIdx);
        segTime(i,:) = time(startIdx:stopIdx);
    end
    TimeCentered = time(startIdx:stopIdx);
    timeAxis = linspace(interval(1), interval(2), length(TimeCentered));
end
