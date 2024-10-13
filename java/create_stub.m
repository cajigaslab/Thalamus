function stub = create_stub(address)
  credentials = javaMethod("create", "io.grpc.InsecureChannelCredentials");
  channel_builder = javaMethod("newChannelBuilder", "io.grpc.Grpc", address, credentials);
  channel = channel_builder.build;
  stub = javaMethod("newBlockingStub", "org.pesaranlab.thalamus_grpc.ThalamusGrpc", channel);
end
