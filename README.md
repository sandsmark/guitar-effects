Guitar Thing Without Any Name
-----------------------------

![screenshot](/screenshot.png)

jack is a pain to get to work while letting pulseaudio work, and jack +
guitarix has horrible latency for some reason.

So this is a very simple guitar effect thing using pulseaudio.

Run it with either just `guitar-effects distortion` or `guitar-effects fuzz` to
select one of two distortion effects. Then use something like
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
  - Expose the rest of the effects
  - Allow changing parameters
  - Allow changing pulseaudio connection parameters (latency vs. buffer underruns).
 - Switch from simple pulseaudio API to the threaded API.
    - Better control of latency.
    - Allow selecting input/output directly from the application instead of needing to use pavucontrol.
 - Switch to using a ringbuffer to make it simple to ensure always available buffer data to avoid underruns.

