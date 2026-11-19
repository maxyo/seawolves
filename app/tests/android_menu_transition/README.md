Run `python3 tests/android_menu_transition/run.py` with a JDK.

The runner extracts the `MobileHud` screen-transition, rebuild, touch-dispatch and detach code and checks it on a
virtual Android scheduler. Rendering, Android touch delivery, JNI and native lifecycle are recording boundaries:
verify the visible result on a device (gameplay → pause → settings → pause → gameplay, rapid taps, Back during the
transition, background/resume).
