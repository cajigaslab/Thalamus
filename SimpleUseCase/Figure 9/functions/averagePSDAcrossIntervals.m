function averagedPSD = averagePSDAcrossIntervals(PSD, timeIntervals, timeArray)
    % Author: Qasim Qureshi May 2024
    % PSD: A 3D array (channels by freq by samples)
    % timeIntervals: A matrix or cell array containing the start and end times
    % timeArray: A 1D array of time points corresponding to the third dimension of PSD

    % Initialize an empty list to hold the indices
    indices = [];

    % Loop through each interval and collect indices
    for i = 1:size(timeIntervals, 1)
        startTime = timeIntervals(i, 1);
        endTime = timeIntervals(i, 2);
        intervalIndices = find(timeArray >= startTime & timeArray <= endTime);
        indices = [indices, intervalIndices];
    end

    % Remove duplicate indices (if any)
    indices = unique(indices);

    % Average the PSD across the specified indices
    averagedPSD = mean(PSD(:, :, indices), 3);
end