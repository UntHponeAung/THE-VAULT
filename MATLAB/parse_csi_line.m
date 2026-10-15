function row = parse_csi_line(line)
% Numeric result: boot, sequence, ms, state, firmware score, threshold,
% dropped USB records, then amplitudes at bins 4:2:26 and 38:2:60.
% Ordinary serial diagnostics, headers and damaged records return [].

row = [];

% Preserve empty fields so a damaged CSV record cannot silently shift columns.
parts = strsplit(strtrim(char(line)), ',', 'CollapseDelimiters', false);
if numel(parts) ~= 32 || ~strcmp(parts{1}, 'CSI1')
    return;
end

values = str2double(parts(2:end));
if any(~isfinite(values)) || any(values < 0)
    return;
end

integers = values([1:4, 7]);
if any(integers ~= floor(integers)) || any(values([1:3, 7]) > 2^32 - 1) ...
        || values(1) == 0 || values(4) < 2 || values(4) > 4 ...
        || any(values(8:end) > 182) || mean(values(8:end)) < 1
    return;
end

row = values;
end
