function files = capture_csi(ports, durationSeconds, prefix)
% Capture and plot CSI from one or two ESP32 USB ports.
% Example: capture_csi(["/dev/cu.usbserial-0001","/dev/cu.usbserial-0002"],120)
% Close PlatformIO Serial Monitor first. Boards still communicate over ESP-NOW.

if nargin < 2
    durationSeconds = 60;
end
if nargin < 3
    prefix = "csi_" + string(datestr(now, 'yyyymmdd_HHMMSS'));
end

ports = string(ports);
if isempty(ports) || numel(ports) > 2 || numel(unique(ports)) ~= numel(ports)
    error('CSI:Ports', 'Supply one or two different USB serial ports.');
end
validateattributes(durationSeconds, {'numeric'}, {'scalar', 'positive', 'finite'});

nodeCount = numel(ports);
devices = cell(1, nodeCount);
buffers = repmat({''}, 1, nodeCount);
states = cell(1, nodeCount);
lastRows = cell(1, nodeCount);
fileIds = -ones(1, nodeCount);
files = strings(1, nodeCount);
lastArrival = nan(1, nodeCount);
lastRequest = -inf(1, nodeCount);
cleanup = onCleanup(@closeConnections); %#ok<NASGU>

fig = figure('Name', 'THE VAULT - experimental CSI motion');
layout = tiledlayout(fig, nodeCount, 1);
axesList = gobjects(1, nodeCount);
scoreLines = gobjects(1, nodeCount);
thresholdLines = gobjects(1, nodeCount);
firmwareLines = gobjects(1, nodeCount);
bins = [4:2:26, 38:2:60];

for nodeIndex = 1:nodeCount
    files(nodeIndex) = string(prefix) + "_node" + nodeIndex + ".csv";
    if isfile(files(nodeIndex))
        error('CSI:Exists', 'Output already exists: %s', files(nodeIndex));
    end
    fileIds(nodeIndex) = fopen(files(nodeIndex), 'w');
    if fileIds(nodeIndex) < 0
        error('CSI:File', 'Cannot write %s', files(nodeIndex));
    end
    fprintf(fileIds(nodeIndex), 'record,boot,sequence,time_ms,state,firmware_score,firmware_threshold,dropped');
    fprintf(fileIds(nodeIndex), ',amp_%02d', bins);
    fprintf(fileIds(nodeIndex), '\n');

    devices{nodeIndex} = serialport(ports(nodeIndex), 115200, 'Timeout', 1);
    configureTerminator(devices{nodeIndex}, 'LF');
    flush(devices{nodeIndex});

    axesList(nodeIndex) = nexttile(layout);
    scoreLines(nodeIndex) = animatedline(axesList(nodeIndex), 'Color', [0 0.45 0.75], 'MaximumNumPoints', 900);
    thresholdLines(nodeIndex) = animatedline(axesList(nodeIndex), 'Color', [0.85 0.3 0.1], 'LineStyle', '--', 'MaximumNumPoints', 900);
    firmwareLines(nodeIndex) = animatedline(axesList(nodeIndex), 'Color', [0.4 0.4 0.4], 'LineStyle', ':', 'MaximumNumPoints', 900);
    xlabel(axesList(nodeIndex), 'Capture time (seconds)');
    ylabel(axesList(nodeIndex), 'CSI shape change (%)');
    legend(axesList(nodeIndex), {'MATLAB score', 'MATLAB threshold', 'Firmware score'});
    title(axesList(nodeIndex), "Node " + nodeIndex + " - waiting for CSI");
end

captureTimer = tic;
while toc(captureTimer) < durationSeconds && isgraphics(fig)
    captureTime = toc(captureTimer);
    for nodeIndex = 1:nodeCount
        % Opening USB can reset the board. Retry the stream command after startup.
        if captureTime - lastRequest(nodeIndex) >= 2
            writeline(devices{nodeIndex}, 'm');
            lastRequest(nodeIndex) = captureTime;
        end

        count = min(devices{nodeIndex}.NumBytesAvailable, 4096);
        if count > 0
            bytes = read(devices{nodeIndex}, count, 'uint8');
            buffers{nodeIndex} = [buffers{nodeIndex}, char(bytes(:).')]; %#ok<AGROW>
        end

        while true
            newlineAt = find(buffers{nodeIndex} == char(10), 1);
            if isempty(newlineAt)
                break;
            end
            line = buffers{nodeIndex}(1:newlineAt - 1);
            buffers{nodeIndex}(1:newlineAt) = [];
            row = parse_csi_line(line);
            if isempty(row)
                continue;
            end

            % Ignore duplicate or out-of-order samples from the same boot.
            if ~isempty(lastRows{nodeIndex}) && row(1) == lastRows{nodeIndex}(1)
                advance = mod(row(2) - lastRows{nodeIndex}(2), 2^32);
                if advance == 0 || advance >= 2^31
                    continue;
                end
            end

            fprintf(fileIds(nodeIndex), '%s\n', strtrim(line));
            states{nodeIndex} = csi_motion_step(states{nodeIndex}, row(3), row(8:end), row(1));
            lastRows{nodeIndex} = row;
            lastArrival(nodeIndex) = captureTime;

            addpoints(scoreLines(nodeIndex), captureTime, states{nodeIndex}.score);
            addpoints(thresholdLines(nodeIndex), captureTime, states{nodeIndex}.threshold);
            addpoints(firmwareLines(nodeIndex), captureTime, row(5));
            label = 'CALIBRATING - keep room still';
            if states{nodeIndex}.calibrated
                label = 'READY - no motion warning';
            end
            if states{nodeIndex}.motion
                label = 'MOTION WARNING - range unknown';
            end
            title(axesList(nodeIndex), sprintf('Node %d: %s | USB skipped: %.0f', nodeIndex, label, row(7)));
        end

        if numel(buffers{nodeIndex}) > 8192
            buffers{nodeIndex} = ''; % Cap an incomplete serial line.
        end
        if ~isnan(lastArrival(nodeIndex)) && captureTime - lastArrival(nodeIndex) >= 0.35
            states{nodeIndex} = []; % Require calibration after a data gap.
            addpoints(scoreLines(nodeIndex), captureTime, NaN);
            addpoints(thresholdLines(nodeIndex), captureTime, NaN);
            addpoints(firmwareLines(nodeIndex), captureTime, NaN);
            title(axesList(nodeIndex), sprintf('Node %d: NO CSI DATA - waiting to recalibrate', nodeIndex));
        end
        xlim(axesList(nodeIndex), [max(0, captureTime - 60), max(60, captureTime)]);
    end

    drawnow limitrate;
    pause(0.01);
end
disp(files);

    function closeConnections()
        for index = 1:nodeCount
            if ~isempty(devices{index})
                try
                    writeline(devices{index}, 'n');
                    delete(devices{index});
                catch
                    % Close output files even if the serial port has disconnected.
                end
            end

            if fileIds(index) >= 0
                fclose(fileIds(index));
            end
        end
    end
end
