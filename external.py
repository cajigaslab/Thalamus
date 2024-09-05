"""
Implementation of the simple task
"""
from thalamus import task_controller_pb2
from thalamus import task_controller_pb2_grpc

import grpc
import json
import queue
import pprint
from psychopy import visual, core

channel = grpc.insecure_channel('localhost:50051') # Connect to the task_controller server
stub = task_controller_pb2_grpc.TaskControllerStub(channel)

response_queue = queue.Queue()

# Create a window
# win = visual.Window(size=(800, 600), units='pix', fullscr=False)  
# win = visual.Window(size=(1024, 976), fullscr=True, screen=1)
win = visual.Window(size=(800, 600))

for message in stub.execution(iter(response_queue.get, None)):
   config = json.loads(message.body) # reads the message body as JSON and converts it into config
   pprint.pprint(config) # output for debugging

   width, height = config['width'], config['height']
   center_x, center_y = config['center_x'], config['center_y']
   target_color_rgb = config['target_color']
   print("target_color_rgb = ", target_color_rgb)  # Debugging statement
   
   ''
   # Create a Gaussian blurred circle stimulus
   gaussian_circle = visual.GratingStim(
      win=win,
      size=(width, height),  # Size of the circle
      pos=(center_x, center_y),
      sf=0,  # Spatial frequency of the grating (0 means no grating)
      mask='gauss',  # Gaussian mask
      color=target_color_rgb,  # Color of the circle
      colorSpace='rgb255'
   )
  
   # Draw the Gaussian circle
   gaussian_circle.draw()

   win.flip()
   core.wait(5)
   ''

   response_queue.put(task_controller_pb2.TaskResult(success=True))

# Close the window after the loop
win.close()
print("Window closed.")  # Debugging statement