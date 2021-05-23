Guitar Thing Without Any Name
-----------------------------

![screenshot](/screenshot.png)

jack is a pain to get to work while letting pulseaudio work, and jack +
guitarix has horrible latency for some reason.

So this is a very simple guitar effect thing using pulseaudio, which has less
than 1ms latency on my machine.


Warning
-------
Don't use it with Pipewire, Pipewire kind of dies.

Use Guitarix instead if you have Pipewire,


Usage
-----

Run `guitar-effects --help` to see what effects are available and how to
specify which ou want.

Then use something like
[pavucontrol](https://github.com/sandsmark/pavucontrol-qt) to select the
correct input and output for it.


Dependencies
------------

 - Pulseaudio
 - CMake
 - Qt (though not used yet)


TODO
----

 - Create GUI
  - Allow changing parameters
  - Allow changing pulseaudio connection parameters (latency vs. buffer underruns).
 - Switch from simple pulseaudio API to the threaded API.
    - Better control of latency.
    - Allow selecting input/output directly from the application instead of needing to use pavucontrol.
 - Switch to using a ringbuffer to make it simple to ensure always available buffer data to avoid underruns.

