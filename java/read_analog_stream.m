function [data, spans, names] = read_analog_stream(stream)
  response = stream.next();
  stream_data = response.getDataList();
  length = stream_data.size();
  data = zeros(length, 1);
  for i = 1:length
    data(i) = stream_data.get(i-1);
  end

  stream_spans = response.getSpansList();
  length = stream_spans.size();
  spans = zeros(length, 2);
  names = cell(length, 1);
  for i = 1:length
    spans(i,1) = stream_spans.get(i-1).getBegin();
    spans(i,2) = stream_spans.get(i-1).getEnd();
    names(i) = stream_spans.get(i-1).getName();
  end
end
