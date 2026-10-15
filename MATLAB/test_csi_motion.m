function test_csi_motion()
% Synthetic checks only. Run this in MATLAB before collecting real measurements.

base = 40 * ones(1, 24);
state = csi_motion_step([], 0, base, 1);
minimumThreshold = state.threshold;
for timeMs = 70:70:10080
    state = csi_motion_step(state, timeMs, base, 1);
end

% Constant input has zero measured noise, so calibration must keep the floor.
assert(state.calibrated && ~state.motion && state.threshold == minimumThreshold);

timeMs = 10080;
for sampleIndex = 1:10
    timeMs = timeMs + 70;
    state = csi_motion_step(state, timeMs, base * (1 + mod(sampleIndex, 2)), 1);
    assert(~state.motion && state.score < 1e-9);
end

shape = [15 * ones(1, 12), -15 * ones(1, 12)];
timeMs = timeMs + 70;
state = csi_motion_step(state, timeMs, base + shape, 1);
for sampleIndex = 1:10
    timeMs = timeMs + 70;
    state = csi_motion_step(state, timeMs, base, 1);
    assert(~state.motion);
end

for sampleIndex = 1:8
    timeMs = timeMs + 70;
    state = csi_motion_step(state, timeMs, base + (-1)^sampleIndex * shape, 1);
end
assert(state.motion);

state = csi_motion_step(state, timeMs + 350, [], 1);
assert(~state.hasPrevious && ~state.motion && ~state.calibrated);
state = csi_motion_step(state, 10, base, 2);
assert(state.boot == 2 && ~state.calibrated);
state = csi_motion_step([], 2^32 - 70, base, 3);
state = csi_motion_step(state, 0, base, 3);
assert(state.hasPrevious && state.samples == 1);

assert(isempty(parse_csi_line('ordinary serial diagnostic')));
line = ['CSI1,1,1,100,2,0,2.5,0', repmat(',40', 1, 24)];
assert(numel(parse_csi_line(line)) == 31);
assert(isempty(parse_csi_line(strrep(line, 'CSI1', 'CSI0'))));
assert(isempty(parse_csi_line(strrep(line, 'CSI1,', 'CSI1,,'))));
badDropped = ['CSI1,1,1,100,2,0,2.5,4294967296', repmat(',40', 1, 24)];
assert(isempty(parse_csi_line(badDropped)));

testReplayOrdering();
disp('PASS synthetic MATLAB detector, parser and replay ordering checks');
end

function testReplayOrdering()
% Replay must ignore duplicate/reordered sequences, accept wrap and a new boot.
filename = [tempname, '.csv'];
cleanup = onCleanup(@() delete(filename)); %#ok<NASGU>
fileId = fopen(filename, 'w');
assert(fileId >= 0, 'Cannot create temporary replay input.');

records = [1, 2^32 - 1, 0; ...
           1, 2^32 - 1, 0; ... % Duplicate.
           1, 0, 70; ...       % Valid sequence wrap.
           1, 2^32 - 1, 0; ... % Older sequence after the wrap.
           1, 1, 140; ...
           2, 1, 10];          % New boot has a separate timeline.
for sampleIndex = 1:size(records, 1)
    fprintf(fileId, 'CSI1,%.0f,%.0f,%.0f,2,0,10,0', records(sampleIndex, :));
    fprintf(fileId, '%s', repmat(',40', 1, 24));
    fprintf(fileId, '\n');
end
fclose(fileId);

result = analyse_csi(filename, false);
assert(height(result) == 4);
assert(all(abs(result.Seconds - [0; 0.07; 0.14; 0.49]) < 1e-9));
end
