
# region -- Gaussian visualizer and manipulator
''
import tkinter as tk
from tkinter import ttk
from psychopy import visual, core, event, monitors

# Function to update the Gaussian circle
def update_gaussian():
    global gaussian_circle
    width = float(width_var.get())
    height = float(height_var.get())
    sf = float(sf_var.get())
    color = [int(c) for c in color_var.get().split(',')]
    ori = float(ori_var.get())
    contrast = float(contrast_var.get())
    opacity = float(opacity_var.get())
    
    gaussian_circle.size = (width, height)
    gaussian_circle.sf = sf
    gaussian_circle.color = color
    gaussian_circle.ori = ori
    gaussian_circle.contrast = contrast
    gaussian_circle.opacity = opacity
    
    # Draw the updated Gaussian circle
    gaussian_circle.draw()
    win.flip()

# Create a PsychoPy window
my_monitor = monitors.Monitor('DellLaptopMonitor') # Create a Monitor object
my_monitor.setSizePix((3840, 2160))  # # Set the screen resolution
my_monitor.setWidth(38.189)  # # Set the screen width in centimeters
my_monitor.setDistance(57)  # Set the distance from the user to the screen in centimeters
win = visual.Window( # Create a window using the Monitor object
   size=(1024, 768),
   monitor=my_monitor,
   units='deg',  # Use degrees of visual angle
   color='black'
)#, units='pix', fullscr=False, screen=2) 

# Initial parameters for the Gaussian circle
initial_width = 10
initial_height = 10
initial_sf = 0.05
initial_color = [255, 255, 255]
initial_ori = 0
initial_contrast = 1.0
initial_opacity = 1.0

# Create the Gaussian circle stimulus
gaussian_circle = visual.GratingStim(
    win=win,
    tex='sin',
    mask='gauss',
    size=(initial_width, initial_height),
    sf=initial_sf,
    color=initial_color,
    colorSpace='rgb255',
    ori=initial_ori,
    contrast=initial_contrast,
    opacity=initial_opacity
)

# Draw the initial Gaussian circle
gaussian_circle.draw()
win.flip()

# Create the GUI window
root = tk.Tk()
root.title("Gaussian Circle Parameters")

# Width parameter
tk.Label(root, text="Width:").grid(row=0, column=0)
width_var = tk.StringVar(value=str(initial_width))
tk.Entry(root, textvariable=width_var).grid(row=0, column=1)

# Height parameter
tk.Label(root, text="Height:").grid(row=1, column=0)
height_var = tk.StringVar(value=str(initial_height))
tk.Entry(root, textvariable=height_var).grid(row=1, column=1)

# Spatial frequency parameter
tk.Label(root, text="Spatial Frequency:").grid(row=2, column=0)
sf_var = tk.StringVar(value=str(initial_sf))
tk.Entry(root, textvariable=sf_var).grid(row=2, column=1)

# Color parameter
tk.Label(root, text="Color (R,G,B):").grid(row=3, column=0)
color_var = tk.StringVar(value=','.join(map(str, initial_color)))
tk.Entry(root, textvariable=color_var).grid(row=3, column=1)

# Orientation parameter
tk.Label(root, text="Orientation:").grid(row=4, column=0)
ori_var = tk.StringVar(value=str(initial_ori))
tk.Entry(root, textvariable=ori_var).grid(row=4, column=1)

# Contrast parameter
tk.Label(root, text="Contrast:").grid(row=5, column=0)
contrast_var = tk.StringVar(value=str(initial_contrast))
tk.Entry(root, textvariable=contrast_var).grid(row=5, column=1)

# Opacity parameter
tk.Label(root, text="Opacity:").grid(row=6, column=0)
opacity_var = tk.StringVar(value=str(initial_opacity))
tk.Entry(root, textvariable=opacity_var).grid(row=6, column=1)

# Update button
tk.Button(root, text="Update", command=update_gaussian).grid(row=7, columnspan=2)

# Run the GUI loop
root.mainloop()

# Close the PsychoPy window
win.close()
''
# endregion

## A simplest code to draw a basic white circle
'''
from psychopy import visual, core

# Create a window
win = visual.Window(size=(800, 600))

# Create a circle stimulus
circle = visual.Circle(
    win=win,
    radius=0.1,  # Radius of the circle
    fillColor='white',  # Fill color of the circle
    lineColor='white'  # Line color of the circle
)

# Draw the circle
circle.draw()

# Flip the window to update the display
win.flip()

# Wait for 2 seconds
core.wait(2)

# Close the window
win.close()
'''


'''
## Code for rendering a simple OpenGL triangle using PyQt and PyOpenGL
import pyglet
from pyglet.gl import *
import ctypes

# Create a window
window = pyglet.window.Window()

@window.event
def on_draw():
    print("on_draw called")
    glEnable(GL_BLEND)
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA)

    # Clear the window
    glClear(GL_COLOR_BUFFER_BIT)
    
    # Set the color to white
    glColor3f(1.0, 1.0, 1.0)
    
    # Draw a triangle
    glBegin(GL_TRIANGLES)
    glVertex2f(0.0, 0.5)
    glVertex2f(-0.5, -0.5)
    glVertex2f(0.5, -0.5)
    glEnd()
    
    # Check for OpenGL errors
    error = glGetError()
    if error != GL_NO_ERROR:
        print(f"OpenGL Error: {error}")

@window.event
def on_resize(width, height):
    print(f"on_resize called with width={width}, height={height}")
    # Set the viewport
    glViewport(0, 0, width, height)
    
    # Set the projection matrix
    glMatrixMode(GL_PROJECTION)
    glLoadIdentity()
    gluOrtho2D(-1, 1, -1, 1)
    
    # Set the modelview matrix
    glMatrixMode(GL_MODELVIEW)
    glLoadIdentity()

# Ensure the resize event is called to set up the projection matrix
window.dispatch_event('on_resize', window.width, window.height)

# Print the OpenGL version
version = glGetString(GL_VERSION)
if version:
    version_str = ctypes.cast(version, ctypes.c_char_p).value.decode('utf-8')
    print(f"OpenGL Version: {version_str}")
else:
    print("Failed to get OpenGL version")

# Run the pyglet application
pyglet.app.run()
'''



## Code for generating the same visual stimulus upon button click both in PyQt and PsychoPy
'''
import sys
import multiprocessing
from PyQt5.QtWidgets import QApplication, QWidget, QPushButton, QVBoxLayout, QSlider, QLabel, QHBoxLayout
from PyQt5.QtCore import Qt
from PyQt5.QtGui import QColor, QPainter, QBrush, QPen
from PyQt5.QtOpenGL import QGLWidget
from psychopy import visual, core


def show_psychopy_stimulus(radius, red, green, blue):
    # PsychoPy code to show a visual stimulus with given parameters
    win = visual.Window(size=(800, 800), fullscr=False, screen=1)
    stimulus = visual.Circle(
        win,
        radius=radius,
        fillColor=[red / 255, green / 255, blue / 255],
        lineColor=[red / 255, green / 255, blue / 255]
    )
    stimulus.draw()
    win.flip()
    core.wait(2)  # Display the stimulus for 2 seconds
    win.close()


class PyQtStimulusWidget(QWidget):
    def __init__(self, radius, red, green, blue, parent=None):
        super().__init__(parent)
        self.radius = radius
        self.color = QColor(red, green, blue)
        self.setMinimumSize(800, 800)

    def paintEvent(self, event):
        painter = QPainter(self)
        painter.setRenderHint(QPainter.Antialiasing)
        painter.setBrush(QBrush(self.color, Qt.SolidPattern))
        painter.setPen(QPen(self.color, 1, Qt.SolidLine))
        # Draw the circle
        painter.drawEllipse(self.width() // 2 - self.radius, self.height() // 2 - self.radius,
                            self.radius * 2, self.radius * 2)


class MainWindow(QWidget):
    def __init__(self):
        super().__init__()
        self.initUI()

    def initUI(self):
        self.setWindowTitle('PyQt and PsychoPy Example')
        self.setGeometry(100, 100, 300, 400)

        layout = QVBoxLayout()
        self.setLayout(layout)

        # Radius Slider
        self.radius_slider = QSlider(Qt.Horizontal)
        self.radius_slider.setMinimum(1)
        self.radius_slider.setMaximum(200)
        self.radius_slider.setValue(50)
        self.radius_slider.setTickInterval(10)
        self.radius_slider.setTickPosition(QSlider.TicksBelow)
        layout.addWidget(QLabel('Radius'))
        layout.addWidget(self.radius_slider)

        # Red Slider
        self.red_slider = QSlider(Qt.Horizontal)
        self.red_slider.setMinimum(0)
        self.red_slider.setMaximum(255)
        self.red_slider.setValue(255)
        self.red_slider.setTickInterval(10)
        self.red_slider.setTickPosition(QSlider.TicksBelow)
        layout.addWidget(QLabel('Red'))
        layout.addWidget(self.red_slider)

        # Green Slider
        self.green_slider = QSlider(Qt.Horizontal)
        self.green_slider.setMinimum(0)
        self.green_slider.setMaximum(255)
        self.green_slider.setValue(255)
        self.green_slider.setTickInterval(10)
        self.green_slider.setTickPosition(QSlider.TicksBelow)
        layout.addWidget(QLabel('Green'))
        layout.addWidget(self.green_slider)

        # Blue Slider
        self.blue_slider = QSlider(Qt.Horizontal)
        self.blue_slider.setMinimum(0)
        self.blue_slider.setMaximum(255)
        self.blue_slider.setValue(255)
        self.blue_slider.setTickInterval(10)
        self.blue_slider.setTickPosition(QSlider.TicksBelow)
        layout.addWidget(QLabel('Blue'))
        layout.addWidget(self.blue_slider)

        # Start Button
        btn = QPushButton('Start PsychoPy and PyQt Stimuli', self)
        btn.clicked.connect(self.run_stimuli)
        layout.addWidget(btn)

    def run_stimuli(self):
        # Get the current values from the sliders
        radius = self.radius_slider.value()
        red = self.red_slider.value()
        green = self.green_slider.value()
        blue = self.blue_slider.value()

        # Run PsychoPy stimulus in a separate process
        psychopy_process = multiprocessing.Process(
            target=show_psychopy_stimulus,
            args=(radius, red, green, blue)
        )
        psychopy_process.start()

        # Run PyQt stimulus in a new window
        self.pyqt_window = QWidget()
        self.pyqt_window.setWindowTitle('PyQt Stimulus')
        pyqt_layout = QVBoxLayout()
        self.pyqt_window.setLayout(pyqt_layout)

        self.stimulus_widget = PyQtStimulusWidget(radius, red, green, blue, self.pyqt_window)
        pyqt_layout.addWidget(self.stimulus_widget)
        self.pyqt_window.show()

        psychopy_process.join()  # Wait for the PsychoPy process to complete


if __name__ == '__main__':
    multiprocessing.set_start_method('spawn')  # Required for compatibility on Windows
    app = QApplication(sys.argv)
    mainWin = MainWindow()
    mainWin.show()
    sys.exit(app.exec_())

'''

## Code for running PsychoPy stimulus in a separate process while having control via PyQt sliders
'''
import sys
import multiprocessing
from PyQt5.QtWidgets import QApplication, QWidget, QPushButton, QVBoxLayout, QSlider, QLabel, QHBoxLayout
from PyQt5.QtCore import Qt
from psychopy import visual, core


def show_psychopy_stimulus(radius, red, green, blue):
    # PsychoPy code to show a visual stimulus with given parameters
    win = visual.Window(size=(800, 800), fullscr=False, screen=1)
    stimulus = visual.Circle(
        win,
        radius=radius,
        fillColor=[red / 255, green / 255, blue / 255],
        lineColor=[red / 255, green / 255, blue / 255]
    )
    stimulus.draw()
    win.flip()
    core.wait(2)  # Display the stimulus for 2 seconds
    win.close()


class MainWindow(QWidget):
    def __init__(self):
        super().__init__()
        self.initUI()

    def initUI(self):
        self.setWindowTitle('PyQt and PsychoPy Example')
        self.setGeometry(100, 100, 300, 400)

        layout = QVBoxLayout()
        self.setLayout(layout)

        # Radius Slider
        self.radius_slider = QSlider(Qt.Horizontal)
        self.radius_slider.setMinimum(1)
        self.radius_slider.setMaximum(200)
        self.radius_slider.setValue(50)
        self.radius_slider.setTickInterval(10)
        self.radius_slider.setTickPosition(QSlider.TicksBelow)
        layout.addWidget(QLabel('Radius'))
        layout.addWidget(self.radius_slider)

        # Red Slider
        self.red_slider = QSlider(Qt.Horizontal)
        self.red_slider.setMinimum(0)
        self.red_slider.setMaximum(255)
        self.red_slider.setValue(255)
        self.red_slider.setTickInterval(10)
        self.red_slider.setTickPosition(QSlider.TicksBelow)
        layout.addWidget(QLabel('Red'))
        layout.addWidget(self.red_slider)

        # Green Slider
        self.green_slider = QSlider(Qt.Horizontal)
        self.green_slider.setMinimum(0)
        self.green_slider.setMaximum(255)
        self.green_slider.setValue(255)
        self.green_slider.setTickInterval(10)
        self.green_slider.setTickPosition(QSlider.TicksBelow)
        layout.addWidget(QLabel('Green'))
        layout.addWidget(self.green_slider)

        # Blue Slider
        self.blue_slider = QSlider(Qt.Horizontal)
        self.blue_slider.setMinimum(0)
        self.blue_slider.setMaximum(255)
        self.blue_slider.setValue(255)
        self.blue_slider.setTickInterval(10)
        self.blue_slider.setTickPosition(QSlider.TicksBelow)
        layout.addWidget(QLabel('Blue'))
        layout.addWidget(self.blue_slider)

        # Start Button
        btn = QPushButton('Start PsychoPy Stimulus', self)
        btn.clicked.connect(self.run_psychopy_stimulus)
        layout.addWidget(btn)

    def run_psychopy_stimulus(self):
        # Get the current values from the sliders
        radius = self.radius_slider.value()
        red = self.red_slider.value()
        green = self.green_slider.value()
        blue = self.blue_slider.value()

        # Run PsychoPy in a separate process with the current parameters
        psychopy_process = multiprocessing.Process(
            target=show_psychopy_stimulus,
            args=(radius, red, green, blue)
        )
        psychopy_process.start()
        psychopy_process.join()  # Wait for the PsychoPy process to complete


if __name__ == '__main__':
    multiprocessing.set_start_method('spawn')  # Required for compatibility on Windows
    app = QApplication(sys.argv)
    mainWin = MainWindow()
    mainWin.show()
    sys.exit(app.exec_())
'''


## Code for rendering PsychoPy inside PyQt window
'''
import sys
import numpy as np
from PyQt5.QtWidgets import QApplication, QLabel, QVBoxLayout, QWidget
from PyQt5.QtGui import QImage, QPixmap
from psychopy import visual, core
from PIL import ImageQt

class PsychoPyRenderer:
    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.win = visual.Window(size=(width, height), units='pix', fullscr=False, allowGUI=False)

    def render_gaussian_circle(self, width, height, center_x, center_y, color):
        gaussian_circle = visual.GratingStim(
            win=self.win,
            size=(width, height),
            pos=(center_x, center_y),
            sf=0,
            mask='gauss',
            color=color,
            colorSpace='rgb'
        )
        gaussian_circle.draw()
        self.win.flip()
        return self._capture_frame()

    def _capture_frame(self):
        self.win.getMovieFrame(buffer='back')
        frame = self.win.movieFrames[-1]
        return frame

class PsychoPyWidget(QWidget):
    def __init__(self, renderer):
        super().__init__()
        self.renderer = renderer
        self.initUI()

    def initUI(self):
        layout = QVBoxLayout()
        self.label = QLabel(self)
        layout.addWidget(self.label)
        self.setLayout(layout)
        self.update_image()

    def update_image(self):
        frame = self.renderer.render_gaussian_circle(200, 200, 0, 0, [1, 0, 0])
        qimage = ImageQt.ImageQt(frame)
        image = QImage(qimage)
        pixmap = QPixmap.fromImage(image)
        self.label.setPixmap(pixmap)

if __name__ == '__main__':
    app = QApplication(sys.argv)
    renderer = PsychoPyRenderer(800, 600)
    ex = PsychoPyWidget(renderer)
    ex.show()
    sys.exit(app.exec_())
'''