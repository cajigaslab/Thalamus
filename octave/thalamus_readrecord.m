function [record] = thalamus_readrecord(fid)
  StorageRecord_Analog = 1;
  StorageRecord_Xsens = 2;
  StorageRecord_Event = 3;
  StorageRecord_Image = 6;
  StorageRecord_Text = 7;
  StorageRecord_Time = 4;
  StorageRecord_Node = 5;
  VARINT = 0;
  I64 = 1;
  LEN = 2;
  I32 = 3;
  Image_data = 1;
  Image_width = 2;
  Image_height = 3;
  Image_format = 4;
  Image_frame_interval = 5;
  Image_last = 6;
  Gray = 0;
  RGB = 1;
  YUYV422 = 2;
  YUV420P = 3;
  YUVJ420P = 4;
  Span_begin = 1;
  Span_end = 2;
  Span_name = 3;

  XsensSegment_id = 1;
  XsensSegment_x = 2;
  XsensSegment_y = 3;
  XsensSegment_z = 4;
  XsensSegment_q0 = 5;
  XsensSegment_q1 = 6;
  XsensSegment_q2 = 7;
  XsensSegment_q3 = 8;
  XsensSegment_frame = 9;
  XsensSegment_time = 10;

  XsensResponse_segments = 1;
  XsensResponse_pose_name = 2;

  AnalogResponse_data = 1;
  AnalogResponse_spans = 2;
  AnalogResponse_sample_intervals = 3;
  AnalogResponse_channels_changed = 4;

  function result = read_double()
    result = typecast(uint8(storage_record(position:position+7)), 'double');
    position = position + 8;
  end

  function result = read_float()
    result = typecast(uint8(storage_record(position:position+3)), 'single');
    position = position + 4;
  end

  function result = read_varint()
    collecting = true;
    multiplier = uint64(1);
    result = uint64(0);
    while collecting
      digit = uint64(storage_record(position++));
      result += bitand(digit, 127)*multiplier;
      multiplier *= uint64(128);
      collecting = bitand(digit, 128);
    end
    varint = result;
  end

  function result = read_doubles(wire_type)
    if wire_type == LEN
      length = read_varint()/8;
      result = zeros(1, length);
      for i = 1:length
        result(i) = read_double();
      end
    else
      result = [read_double()];
    end
  end

  function result = read_floats(wire_type)
    if wire_type == LEN
      length = read_varint()/4;
      result = zeros(1, length);
      for i = 1:length
        result(i) = read_float();
      end
    else
      result = [read_float()];
    end
  end

  function result = read_varints(wire_type)
    if wire_type == LEN
      length = read_varint();
      data_end = position+length-1;
      result = {};
      while position <= data_end
        result{size(result, 2)+1} = read_varint();
      end
      result = cat(1, result{:});
    else
      result = uint64([read_varint()]);
    end
  end

  function result = read_bytes()
    length = read_varint();
    result = storage_record(position:position+length-1);
    position += length;
  end

  function result = read_string()
    data = read_bytes();
    result = native2unicode(transpose(data), 'UTF-8');
  end

  function [field_number, wire_type] = read_tag()
    tag = storage_record(position++);
    field_number = bitshift(tag, -3);
    wire_type = bitand(tag, 7);
  end

  function result = read_image()
    length = read_varint();
    image_end = position+length-1;
    result.data = {};
    result.width = 0;
    result.height = 0;
    result.format = 0;
    result.frame_interval = 0;
    result.last = 0;
    while position <= image_end
      [field_number, wire_type] = read_tag();
      if field_number == Image_data
        result.data{size(result.data,2)+1} = read_bytes();
      elseif field_number == Image_width
        result.width = read_varint();
      elseif field_number == Image_height
        result.height = read_varint();
      elseif field_number == Image_format
        result.format = read_varint();
      elseif field_number == Image_frame_interval
        result.frame_interval = read_varint();
      elseif field_number == Image_last
        result.last = read_varint();
      end
    end
  end

  function result = read_span()
    length = read_varint();
    image_end = position+length-1;
    result.begin = 0;
    result.end = 0;
    result.name = '';
    while position <= image_end
      [field_number, wire_type] = read_tag();
      if field_number == Span_begin
        result.begin = read_varint();
      elseif field_number == Span_end
        result.end = read_varint();
      elseif field_number == Span_name
        result.name = read_string();
      end
    end
  end

  function result = read_analog()
    length = read_varint();
    image_end = position+length-1;
    result.data = {};
    result.spans = {};
    result.sample_intervals = {};
    result.channels_changed = 0;

    while position <= image_end
      [field_number, wire_type] = read_tag();
      if field_number == AnalogResponse_data
        result.data{size(result.data,2)+1} = read_doubles(wire_type);
      elseif field_number == AnalogResponse_spans
        result.spans{size(result.spans,2)+1} = read_span();
      elseif field_number == AnalogResponse_sample_intervals
        result.sample_intervals{size(result.sample_intervals,2)+1} = read_varints(wire_type);
      elseif field_number == AnalogResponse_channels_changed
        result.channels_changed = read_varint();
      end
    end

    result.data = cat(1, result.data{:});
    result.spans = cat(1, result.spans{:});
    result.sample_intervals = cat(1, result.sample_intervals{:});
  end

  function result = read_segment()
    length = read_varint();
    image_end = position+length-1;
    result.id = 0;
    result.x = 0;
    result.y = 0;
    result.z = 0;
    result.q0 = 0;
    result.q1 = 0;
    result.q2 = 0;
    result.q3 = 0;
    result.frame = 0;
    result.time = 0;
    while position <= image_end
      [field_number, wire_type] = read_tag();
      if field_number == XsensSegment_id
        result.id = read_varint();
      elseif field_number == XsensSegment_x 
        result.x = read_float();
      elseif field_number == XsensSegment_y 
        result.y = read_float();
      elseif field_number == XsensSegment_z 
        result.z = read_float();
      elseif field_number == XsensSegment_q0 
        result.q0 = read_float();
      elseif field_number == XsensSegment_q1 
        result.q1 = read_float();
      elseif field_number == XsensSegment_q2 
        result.q2 = read_float();
      elseif field_number == XsensSegment_q3 
        result.q3 = read_float();
      elseif field_number == XsensSegment_frame 
        result.frame = read_varint();
      elseif field_number == XsensSegment_time 
        result.time = read_varint();
      end
    end
  end

  function result = read_motion()
    length = read_varint();
    image_end = position+length-1;
    result.segments = {};
    result.pose_name = '';

    while position <= image_end
      [field_number, wire_type] = read_tag();
      if field_number == XsensResponse_segments
        result.segments{size(result.segments,2)+1} = read_segment();
      elseif field_number == XsensResponse_pose_name
        result.pose_name = read_string();
      end
    end

    result.segments = cat(1, result.segments{:});
  end

  size_bytes = fread(fid, 1, 'uint64', 'b');
  if ~length(size_bytes)
    record.eof = 1;
    return;
  end
  storage_record = fread(fid, size_bytes, 'uint8');
  if length(storage_record) < size_bytes
    record.eof = 1;
    return;
  end

  record.eof = 0;
  position = uint64(1);
  record.type = '';
  record.node = '';
  record.timestamp = 0;
  while position <= size(storage_record, 1)
    [field_number, wire_type] = read_tag();

    if field_number == StorageRecord_Analog
      record.analog = read_analog();
    elseif field_number == StorageRecord_Xsens
      record.motion = read_motion();
    elseif field_number == StorageRecord_Event
      position += read_varint();
    elseif field_number == StorageRecord_Image
      record.image = read_image();
    elseif field_number == StorageRecord_Text
      position += read_varint();
    elseif field_number == StorageRecord_Node
      record.node = read_string();
    elseif field_number == StorageRecord_Time
      record.timestamp = read_varint();
    else
      % unknown tag, skip the data
      if wire_type == VARINT
        read_varint();
      elseif wire_type == LEN
        position += read_varint();
      elseif wire_type == I64
        position += 8;
      elseif wire_type == I32
        position += 4;
      end
    end
  end
end
