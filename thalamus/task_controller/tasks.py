"""
Defines the mapping from task types to the task implementation
"""

from . import simple_task
from . import simple_touch_task
from . import simple_touch_and_look_task
from . import suppressed_reach_task
from . import delayed_reach_task
from . import delayed_saccade_task
from . import delayed_reach_and_saccade_task
from . import double_step_reach
from . import double_step_saccade_and_touch
from . import distractor_suppression_reach
from . import gaze_anchoring
from . import context_dependent_reach
from . import context_dependent_delay_reach
from . import null_task
from . import iphone
from . import stim_task
from . import ceci_stim_task
from . import delayed_reach_stim_task
from . import doublestep_saccade
from . import motion_capture_task
from . import luminance_reward_selection
from . import avatar_task
from . import imagined_task
from . import feedback_task
from . import delayed_saccade_gaussian_task
from . import center_fixation_task
from . import receptive_field_mapping_task
from . import psychopy_task
from .task_context import TaskDescription

DESCRIPTIONS = [
  TaskDescription('simple', 'Simple',
    simple_task.create_widget,
    simple_task.run),
  TaskDescription('simple_touch_task', 'Simple Touch', 
    simple_touch_task.create_widget, 
    simple_touch_task.run),
  TaskDescription('simple_touch_and_look_task', 'Simple Touch And Look', 
    simple_touch_and_look_task.create_widget, 
    simple_touch_and_look_task.run),
  TaskDescription('suppressed_reach', 'Suppressed Reach',
    suppressed_reach_task.create_widget, # type: ignore
    suppressed_reach_task.run), # type: ignore
  TaskDescription('delayed_reach', 'Delayed Reach',
    delayed_reach_task.create_widget, # type: ignore
    delayed_reach_task.run), # type: ignore
  TaskDescription('delayed_saccade', 'Delayed Saccade',
    delayed_saccade_task.create_widget, # type: ignore
    delayed_saccade_task.run), # type: ignore
  TaskDescription('delayed_reach_and_saccade', 'Delayed Reach and Saccade', 
    delayed_reach_and_saccade_task.create_widget, 
    delayed_reach_and_saccade_task.run),
  TaskDescription('distractor_suppression_reach', 'Distractor Suppression Reach',
    distractor_suppression_reach.create_widget, # type: ignore
    distractor_suppression_reach.run), # type: ignore    
  TaskDescription('gaze_anchoring', 'Gaze Anchoring', 
    gaze_anchoring.create_widget, # type: ignore
    gaze_anchoring.run), # type: ignore
  TaskDescription('double_step_saccade_and_touch', 'Double Step Saccade and Touch', 
    double_step_saccade_and_touch.create_widget, # type: ignore
    double_step_saccade_and_touch.run), # type: ignore  
  TaskDescription('context_dependent_reach', 'Context Dependent Reach',
    context_dependent_reach.create_widget, # type: ignore
    context_dependent_reach.run), # type: ignore 
  TaskDescription('context_dependent_delay_reach', 'Context Dependent Delay Reach',
    context_dependent_delay_reach.create_widget, # type: ignore
    context_dependent_delay_reach.run), # type: ignore          
  TaskDescription('double_step_reach', 'Double Step Reach',
    double_step_reach.create_widget,
    double_step_reach.run),
  TaskDescription('null', 'Null',
    null_task.create_widget,
    null_task.run),
  TaskDescription('iphone', 'iPhone',
    iphone.create_widget,
    iphone.run),
  TaskDescription('stim_task', 'Stim Task',
    stim_task.create_widget,
    stim_task.run),
  TaskDescription('ceci_stim_task', 'Ceci Stim Task',
    ceci_stim_task.create_widget,
    ceci_stim_task.run),
  TaskDescription('delayed_read_stim_task', 'Delayed Reach Stim Task',
    delayed_reach_stim_task.create_widget,
    delayed_reach_stim_task.run),
  TaskDescription('doublestep_saccade', 'Doublestep saccade',
    doublestep_saccade.create_widget,
    doublestep_saccade.run),
  TaskDescription('luminance_reward_selection', 'Luminance Reward Selection',
    luminance_reward_selection.create_widget,
    luminance_reward_selection.run),
  TaskDescription('motion_capture_task', 'Motion Capture',
    motion_capture_task.create_widget,
    motion_capture_task.run),
  TaskDescription('avatar_task', 'Avatar',
    avatar_task.create_widget,
    avatar_task.run),
  TaskDescription('imagined_task', 'Imagined',
    imagined_task.create_widget,
    imagined_task.run),
  TaskDescription('feedback_task', 'Feedback',
    feedback_task.create_widget,
    feedback_task.run),
  TaskDescription('delayed_saccade_gaussian_task', 'Dellayed Saccade - Gaussian',
    delayed_saccade_gaussian_task.create_widget,
    delayed_saccade_gaussian_task.run),
    #add gazeanchoring stim task
  TaskDescription('center_fixation_task', 'Center Fixation',
    center_fixation_task.create_widget,
    center_fixation_task.run),
  TaskDescription('receptive_field_mapping_task', 'Receptive Field Mapping',
    receptive_field_mapping_task.create_widget,
    receptive_field_mapping_task.run),
  TaskDescription('psychopy', 'Psychopy',
    psychopy_task.create_widget,
    psychopy_task.run)
]

DESCRIPTIONS_MAP = dict((description.code, description) for description in DESCRIPTIONS)
