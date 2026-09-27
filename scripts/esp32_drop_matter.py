"""Leaves the Matter stack out of the ESP32 link.

The pioarduino framework links esp_matter ahead of libstdc++, and two of its objects carry their own
instantiations of std::string members. The TLS client's std::string references are met there first,
and the Matter stack those objects depend on follows them into an image that has no use for Matter.
Dropping the archive from the list lets libstdc++ answer those references from its usual place; the
order of everything else is the framework's, which the IDF's own C++ runtime overrides rely on.
"""

Import("env")

MATTER = "-lespressif__esp_matter"  # The framework names its archives as -l flags in LIBS.

env.Replace(LIBS=[lib for lib in env["LIBS"] if lib != MATTER])
