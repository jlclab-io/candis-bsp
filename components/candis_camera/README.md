# Candis camera

Optional sensor support for the Candis validation fixture's OV5640 DVP camera.
The component uses Espressif's ESP Video driver and the Candis fixture power
sequence. Start the camera, open the V4L2 device path, and stop the stream and
close the device before releasing the camera. Example configuration and a
capture check are available in the repository's examples/camera project.
