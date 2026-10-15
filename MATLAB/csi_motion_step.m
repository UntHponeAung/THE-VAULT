function state = csi_motion_step(state, timeMs, amplitude, boot)
% Estimate motion from changes in 24 normalized CSI amplitudes.
% Call with amplitude=[] after a data gap to mark the detector unavailable.
% This function never sends alarm or clearance commands to either ESP32.

minimumThreshold = 10.0;
noiseMultiplier = 10.0;
if isempty(state) || state.boot ~= boot
    state = newState(boot, minimumThreshold);
end

% Device timestamps wrap after 2^32 ms. A gap requires a new calibration.
if state.hasPrevious && elapsed(timeMs, state.lastMs) >= 350
    state = newState(boot, minimumThreshold);
end
if state.motion && elapsed(timeMs, state.lastMotionMs) >= 2000
    state.motion = false;
end
if isempty(amplitude)
    return;
end

amplitude = double(amplitude(:).');
if numel(amplitude) ~= 24 || any(~isfinite(amplitude)) || any(amplitude < 0)
    error('CSI:Amplitude', 'Expected 24 finite, nonnegative CSI amplitudes.');
end
if mean(amplitude) < 1 || (state.hasPrevious && elapsed(timeMs, state.lastMs) < 40)
    return;
end

% Normalization reduces uniform receiver gain changes. It does not measure range.
shape = amplitude / mean(amplitude);
change = mean(abs(shape - state.previous)) * 100;
state.previous = shape;
state.lastMs = timeMs;
if ~state.hasPrevious
    state.hasPrevious = true;
    state.startedMs = timeMs;
    return;
end

state.score = state.score + 0.25 * (change - state.score);

% Calibrate for at least ten seconds and 80 comparisons.
if ~state.calibrated
    state.samples = state.samples + 1;
    state.noiseSum = state.noiseSum + change;
    state.noiseSquaredSum = state.noiseSquaredSum + change * change;

    if elapsed(timeMs, state.startedMs) >= 10000 && state.samples >= 80
        noiseMean = state.noiseSum / state.samples;
        variance = max(0, state.noiseSquaredSum / state.samples - noiseMean^2);
        state.threshold = max(minimumThreshold, noiseMean + noiseMultiplier * sqrt(variance));
        state.calibrated = true;
    end
    return;
end

% Three consecutive changes reject isolated spikes. Hold warnings for two seconds.
if change > state.threshold && state.score > state.threshold
    state.highSamples = min(3, state.highSamples + 1);
    if state.highSamples >= 3
        state.motion = true;
        state.lastMotionMs = timeMs;
    end
else
    state.highSamples = 0;
end
end

function state = newState(boot, threshold)
state = struct('boot', boot, 'previous', zeros(1, 24), 'score', 0, ...
    'threshold', threshold, 'hasPrevious', false, 'calibrated', false, ...
    'motion', false, 'samples', 0, 'highSamples', 0, 'noiseSum', 0, ...
    'noiseSquaredSum', 0, 'startedMs', 0, 'lastMs', 0, 'lastMotionMs', 0);
end

function delta = elapsed(now, before)
delta = mod(double(now) - double(before), 2^32);
end
