"""
Implementation of the simple task
"""
from thalamus import task_controller_pb2
from thalamus import task_controller_pb2_grpc

import datetime
import grpc
import json
import queue
import pprint
import typing
import random
from psychopy import visual, core, monitors


def get_value(config: dict, key: str, default: typing.Any = None) -> typing.Union[int, float, bool]:
   """
   Reads a number from the config for the parameters defindes as [min]...[max] and randomly choses 
   a single value from the defined interval
   """

   if default is None:
      default = {}
   value_config = config.get(key, default)

   # if key == 'shape':
   #    return value_config

   # if isinstance(value_config, (int, float, bool)):
   #    self.trial_summary_data.used_values[key] = value_config
   #    return value_config

   if ('min' in value_config or 'min' in default) and ('max' in value_config or 'max' in default):
      lower = value_config['min'] if 'min' in value_config else default['min']
      upper = value_config['max'] if 'max' in value_config else default['max']
      sampled_value = random.uniform(lower, upper)
      # trial_summary_data.used_values[key] = sampled_value
      return sampled_value

   raise RuntimeError(f'Expected number or object with min and max fields, got {key}={value_config}')

channel = grpc.insecure_channel('localhost:50051') # Connect to the task_controller server
stub = task_controller_pb2_grpc.TaskControllerStub(channel)

response_queue = queue.Queue()
clock = core.Clock() # Create a clock object; > precise than core.wait(2.3)

# Create a window
win = visual.Window(size=(1024, 768), color='black')#, units='pix', fullscr=False, screen=2)

for message in stub.execution(iter(response_queue.get, None)):
   config = json.loads(message.body) # reads the message body as JSON and converts it into config
   # pprint.pprint(config) # output for debugging

   width, height = config['width'], config['height']
   center_x, center_y = config['center_x']/100, config['center_y']/100 # /100 b/c visual.GratingStim uses a range of -1 to 1
   print("center_x = ", center_x)  # Debugging statement
   target_color_rgb = config['target_color']

   blink_timeout = get_value(config,'blink_timeout')
   intertrial_timeout = get_value(config,'intertrial_timeout')
   
   ''
   # Create a Gaussian blurred circle stimulus
   gaussian_circle = visual.GratingStim(
      win=win,
      size=(width, height),  # Size of the circle
      pos=(center_x, center_y), # If units for Visual.Window are not defined, pos is in fraction of the screen
      sf=0,  # Spatial frequency of the grating (0 means no grating)
      mask='gauss',  # Gaussian mask
      color=target_color_rgb,  # Color of the circle
      colorSpace='rgb255'
   )
  
   # Create a white rectangle
   rectangle = visual.Rect(
      win=win,
      width=0.45,
      height=0.45,
      fillColor='white',
      lineColor='white',
      pos=(-0.95, 0.95)  # Position as a fraction of the screen
   )

   # Draw
   gaussian_circle.draw() # the Gaussian circle
   rectangle.draw() # draw the rectangle

   win.flip() # Switch drawing buffer to the screen

   # Stimulus presentation time
   clock.reset()
   while clock.getTime() < blink_timeout:
      pass # Busy-wait (i.e. pauses the entire OS)
      # core.wait(0.01)  # Non-blocking wait
      if clock.getTime() >= blink_timeout + 1:  # Add a safety margin
         print("Warning: blink_timeout exceeded")
         break

   win.flip()

   # Intertrial interval (inter-stimulus wait time)
   clock.reset()
   while clock.getTime() < intertrial_timeout:
      pass # Busy-wait (i.e. pauses the entire OS)
      # core.wait(0.01)  # Non-blocking wait
      if clock.getTime() >= intertrial_timeout + 1:  # Add a safety margin
         print("Warning: intertrial_timeout exceeded")
         break

   response_queue.put(task_controller_pb2.TaskResult(success=True))

# Close the window after the loop
win.close()
print("Window closed.")  # Debugging statement