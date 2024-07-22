function [node, timestamp, recordtype, record] = thalamus_readrecord(fid)
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

  function result = read_varint()
    collecting = true;
    multiplier = uint64(1);
    result = uint64(0);
    while collecting
      digit = uint64(storage_record(i++));
      result += bitand(digit, 127)*multiplier;
      multiplier *= uint64(128);
      collecting = bitand(digit, 128);
    end
    varint = result;
  end

  function result = read_bytes()
    length = read_varint();
    result = storage_record(i:i+length-1);
    i += length;
  end

  function result = read_string()
    data = read_bytes();
    result = native2unicode(transpose(data), 'UTF-8');
  end

  function [field_number, wire_type] = read_tag()
    tag = storage_record(i++);
    field_number = bitshift(tag, -3);
    wire_type = bitand(tag, 7);
  end

  function result = read_image()
    length = read_varint();
    image_end = i+length-1;
    data = {};
    width = 0;
    height = 0;
    format = 0;
    frame_interval = 0;
    last = 0;
    while i <= image_end
      [field_number, wire_type] = read_tag()
      if field_number == Image_data
        data{size(data,2)+1} = read_bytes();
      elseif field_number == Image_width
        width = read_varint();
      elseif field_number == Image_height
        height = read_varint();
      elseif field_number == Image_format
        format = read_varint();
      elseif field_number == Image_frame_interval
        frame_interval = read_varint();
      elseif field_number == Image_last
        last = read_varint();
      end
    end

    width
    height
    format

    if format == Gray
        result = transpose(reshape(data{1}, [width height]));
    elseif format == RGB
        r = transpose(reshape(data{1}, [width height]));
        g = transpose(reshape(data{2}, [width height]));
        b = transpose(reshape(data{3}, [width height]));
        result = stack(3, r, g, b);
    elseif format == YUYV422
        error('YUYV422 images unsupported');
    elseif format == YUV420P
        error('YUV420P images unsupported');
    elseif format == YUVJ420P
        error('YUVJ420P images unsupported');
    end
  end

  record = 0;
  size_bytes = fread(fid, 1, 'uint64', 'b');
  size_bytes
  storage_record = fread(fid, size_bytes, 'uint8');
  i = uint64(1);
  recordtype = '';
  node = '';
  timestamp = 0;
  while i <= size(storage_record, 1)
    [field_number, wire_type] = read_tag();

    if field_number == StorageRecord_Analog
      i += read_varint();
      recordtype = 'analog';
    elseif field_number == StorageRecord_Xsens
      i += read_varint();
      recordtype = 'motion';
    elseif field_number == StorageRecord_Event
      i += read_varint();
      recordtype = 'event';
    elseif field_number == StorageRecord_Image
      record = read_image();
      recordtype = 'image';
    elseif field_number == StorageRecord_Text
      i += read_varint();
      recordtype = 'text';
    elseif field_number == StorageRecord_Node
      node = read_string();
    elseif field_number == StorageRecord_Time
      timestamp = read_varint();
    else
      % unknown tag, skip the data
      if wire_type == VARINT
        read_varint();
      elseif wire_type == LEN
        i += read_varint();
      elseif wire_type == I64
        i += 8;
      elseif wire_type == I32
        i += 4;
      end
    end
  end
end