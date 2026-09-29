# Dexed UI pieces

The look-and-feel (`DXLookNFeel`), the envelope, pitch-envelope and image
combo-box components (`DXComponents`), the algorithm diagram (`AlgoDisplay`)
and the images and font in `assets/` come from Dexed
(https://github.com/asb2m10/dexed), copyright 2013-2025 Pascal Gauthier and
contributors, GPL-3.0-or-later. The DX7 envelope tables in `DXComponents.cpp`
are from legasynth-0.4.1 / DX7 Patch Editor, copyright 2002 Juan Linietsky,
2006 Mark-André Hopf.

Changes: Dexed's theme-file loading, processor references, program selector,
LCD and accessibility shims were removed; `OperatorPanel` and `GlobalPanel`
are this project's own layouts using Dexed's coordinates, bound to this
plugin's parameters.
