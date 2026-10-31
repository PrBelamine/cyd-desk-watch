# Runs before the build (see platformio.ini).
# ESP8266Audio 2.4.1 ships AudioOutputPDM.cpp, which does not compile with Arduino-ESP32 core 3.2 (ESP-IDF 5.4: no "data_fmt" field).
# The firmware never uses it (Radio.cpp has its own DAC output), so the file is removed from the downloaded library copy.
Import("env")
import os

libs = os.path.join(env.subst("$PROJECT_LIBDEPS_DIR"), env.subst("$PIOENV"))
for root in (libs, os.path.join(env.subst("$PROJECT_DIR"), "lib")):
    f = os.path.join(root, "ESP8266Audio", "src", "AudioOutputPDM.cpp")
    if os.path.isfile(f):
        os.remove(f)
        print("patch_audio.py: removed " + f)
