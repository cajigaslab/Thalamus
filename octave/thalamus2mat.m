function thalamus2mat(path)
  fid = fopen(path);

  try
    i = 1
    data = {};
    while true
      record = thalamus_readrecord(fid);
      if record.eof
        break
      end
      if isfield(record, 'image')
        'image';
      elseif isfield(record, 'motion')
        'motion'
        record.motion.segments(1).x
      elseif isfield(record, 'analog')
        % 'analog'
        if strcmp(record.analog.spans(1).name, 'Framerate')
          % record.analog.spans(1).name
          data{size(data,2)+1} = record.analog.data;
        end
      elseif isfield(record, 'motion')
        'motion'
        record.motion
      end
      if mod(++i, 2048) == 0
        ftell(fid)
        size(data)
      end
    end
    'done'
    data = cat(1, data{:})
  catch ME
    rethrow(ME)
  end

  fclose(fid)
end
