# PyMOL 3.1.0 PML parsing fixtures

`pymol310_pml_parsing.json` and `pymol310_pml_modes.json` were produced by running
Homebrew Open-Source PyMOL 3.1.0 with probe commands registered through
`cmd.extend`. They record the positional and keyword arguments PyMOL's parser handed
to each probe for 102 inputs (single lines through `cmd.do`, script files through
`cmd.load` and `@`) and for parser modes 11, 12, 13, 21, 22 and 1.

They contain only the probes' received arguments, no PyMOL code. The capture scripts
are kept with the private research record.
