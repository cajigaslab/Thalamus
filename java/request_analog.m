function stream = request_analog(stub, node_name)
  request_builder = javaMethod("newBuilder", "org.pesaranlab.thalamus_grpc.ThalamusOuterClass$AnalogRequest");
  selector_builder = javaMethod("newBuilder", "org.pesaranlab.thalamus_grpc.ThalamusOuterClass$NodeSelector");
  request = request_builder.setNode(selector_builder.setName("Node 1").build()).build();
  stream = stub.analog(request);
end
