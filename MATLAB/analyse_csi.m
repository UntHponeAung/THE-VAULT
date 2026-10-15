function result = analyse_csi(filename, showPlot)
% Replay a capture using the independent MATLAB detector, with no ESP32 needed.
% Pass false as the second argument for analysis without opening a figure.

if nargin < 2
    showPlot = true;
end

fileId = fopen(filename, 'r');
if fileId < 0
    error('CSI:File', 'Cannot read %s', filename);
end
cleanup = onCleanup(@() fclose(fileId)); %#ok<NASGU>

rows = zeros(0, 31);
line = fgetl(fileId);
while ischar(line)
    row = parse_csi_line(line);
    if ~isempty(row)
        % Match live capture by ignoring duplicate or out-of-order samples.
        accept = true;
        if ~isempty(rows) && row(1) == rows(end, 1)
            advance = mod(row(2) - rows(end, 2), 2^32);
            accept = advance > 0 && advance < 2^31;
        end
        if accept
            rows(end + 1, :) = row; %#ok<AGROW>
        end
    end
    line = fgetl(fileId);
end

if isempty(rows)
    error('CSI:Empty', 'No valid CSI1 records in this capture.');
end

sampleCount = size(rows, 1);
timeSeconds = zeros(sampleCount, 1);
score = nan(sampleCount, 1);
threshold = nan(sampleCount, 1);
motion = nan(sampleCount, 1);
firmwareScore = rows(:, 5);
firmwareMotion = nan(sampleCount, 1);
ready = rows(:, 4) >= 3;
firmwareMotion(ready) = rows(ready, 4) == 4;
state = [];

for sampleIndex = 1:sampleCount
    gap = 0;
    if sampleIndex > 1
        gap = mod(rows(sampleIndex, 3) - rows(sampleIndex - 1, 3), 2^32);
        if rows(sampleIndex, 1) ~= rows(sampleIndex - 1, 1)
            gap = 350; % Separate boot timelines; elapsed outage is unknown.
        end
        timeSeconds(sampleIndex) = timeSeconds(sampleIndex - 1) + gap / 1000;
    end

    state = csi_motion_step(state, rows(sampleIndex, 3), rows(sampleIndex, 8:end), rows(sampleIndex, 1));
    score(sampleIndex) = state.score;
    threshold(sampleIndex) = state.threshold;
    if gap >= 350
        score(sampleIndex) = NaN;
        threshold(sampleIndex) = NaN;
        firmwareScore(sampleIndex) = NaN; % Break the plot at missing data.
    end
    if state.calibrated
        motion(sampleIndex) = state.motion;
    end
end

result = table(timeSeconds, score, threshold, motion, firmwareScore, firmwareMotion, ...
    'VariableNames', {'Seconds', 'MatlabScore', 'Threshold', 'MatlabMotion', ...
                      'FirmwareScore', 'FirmwareMotion'});
if ~showPlot
    return;
end

figure('Name', 'THE VAULT CSI replay');
subplot(2, 1, 1);
plot(timeSeconds, score, timeSeconds, threshold, '--', timeSeconds, firmwareScore, ':');
legend('MATLAB score', 'MATLAB threshold', 'Firmware score');
ylabel('Shape change (%)');
title('Independent MATLAB analysis of measured CSI amplitudes');

subplot(2, 1, 2);
stairs(timeSeconds, motion, 'LineWidth', 1.5);
ylim([-0.1, 1.1]);
yticks([0, 1]);
yticklabels({'No warning', 'Motion warning'});
xlabel('Device time (seconds; reboots separated by 0.35 s)');
title('Gaps and calibration are unknown; motion does not imply a known distance');
end
