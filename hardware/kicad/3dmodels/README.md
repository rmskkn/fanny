# 3D models

STEP models used by `fan_controller.kicad_pcb`, copied from the KiCad 3D model
library, tag 8.0.9: https://gitlab.com/kicad/libraries/kicad-packages3D

They are licensed under CC-BY-SA 4.0 with the KiCad libraries exception:
https://www.kicad.org/libraries/license/

`Fuse.3dshapes/Fuse_1812_4532Metric.step` is a copy of
`Resistor_SMD.3dshapes/R_1812_4532Metric.step`, because the library has no
model for that fuse footprint.

`hardware/tools/place_pcb.py` downloads any model that is missing here.
