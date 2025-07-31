function [PSD, F, timeAxis] = ComputePSD(data, time, fs, timeResolution, freqResolution)
    % Author: Qasim Qureshi May 2024
    % computePSD - Computes the power spectral density using the PMTM (Multitaper) method.
    %
    % Syntax: [PSD, F, timeAxis] = computePSD(data, time, fs, timeResolution, freqResolution)
    %
    % Inputs:
    %   data - A 2D matrix of size (Channels x Samples)
    %   time - A 1D array representing the time axis corresponding to the samples
    %   fs - Sampling rate in Hz
    %   timeResolution - Time resolution in seconds (e.g., 0.2 for 200 ms)
    %   freqResolution - Frequency resolution in Hz (e.g., 5 Hz)
    %
    % Outputs:
    %   PSD - A 3D matrix of size (Channels x Frequency x Time)
    %   F - Frequency axis values for the PSD
    %   timeAxis - Downsampled time axis values for the PSD
    
    % Calculate the window length (N) and bandwidth (W)
    N = round(timeResolution*fs);  % Number of samples in the window
    W = freqResolution/(2*fs);              % Bandwidth of frequency smoothing in Hz

    % Calculate the time-bandwidth product
    NW = N * W;  % Time-bandwidth product

    % Ensure NW meets the minimum requirement
    if NW < 1.5
        original_N = N;
        original_W = W;
        NW = 1.5;  % Minimum NW value
        W = NW / N;  % Adjust W accordingly
        warning('Time-bandwidth product NW adjusted to minimum value of 1.5. Original N = %d, W = %.2f. Adjusted W = %.2f.', original_N, original_W, W);
    end

    % Define the number of tapers
    K = round(2 * NW - 1);  % Number of tapers (typically 2NW - 1)

    % Preallocate PSD array
    [numChannels, numSamples] = size(data);
    numWindows = floor(numSamples / N);  % Number of windows for non-overlapping
    PSD = [];
    F = [];

    % Downsample the provided time axis to match the number of windows
    timeAxis = linspace(time(1), time(end), numWindows);

    % Loop through each channel
    for ch = 1:numChannels
        % Get the data for the current channel
        channelData = data(ch, :);

        % Loop through each window to compute PSD
        for win = 1:numWindows
            % Define the window range
            startIdx = (win - 1) * N + 1;
            endIdx = startIdx + N - 1;

            % Extract the windowed data segment
            segment = channelData(startIdx:endIdx);

            % Compute the PSD using pmtm
            [pxx, f] = pmtm(segment, NW, [], fs);

            % Dynamically adjust the size of PSD matrix after the first call to pmtm
            if isempty(PSD)
                numFreqBins = length(f);
                PSD = zeros(numChannels, numFreqBins, numWindows);  % PSD matrix (Channels x Freq x Time)
                F = f;
            end

            % Store the PSD in the matrix
            PSD(ch, :, win) = pxx;
        end
    end
end
