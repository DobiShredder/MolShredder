# Contact analysis

MolShredder provides contact analysis through the same registry used by the
native CLI, GUI actions, the interactive console, and Python:

```text
analyze contacts --first "chain A" --second "chain B" --cutoff 4.0
contacts --first all --cutoff 3.5 --pbc minimum-image --unit angstrom
```

When `--second` is omitted, each unordered pair from `--first` is emitted at
most once. With two selections, the columns preserve first-to-second direction.
Pairs joined by a topology bond are excluded by default, matching VMD
`measure contacts`; use `--exclude-bonded false` to include them. Atom indices
in the table are one-based frontend indices.

`--cutoff` is expressed in `--unit`. Results retain the coordinate source unit
internally and are converted only at the command boundary. `--pbc raw` uses
direct Cartesian displacement. `--pbc minimum-image` requires a valid unit
cell and uses the exact closest lattice image, including skew triclinic cells.

The core does not scan all atom pairs. Raw coordinates use Cartesian bins of
cutoff width. Periodic coordinates use wrapped fractional bins whose search
radius is bounded from the reciprocal cell-vector norms, followed by the exact
minimum-image distance test. Results are sorted by atom index so output is
deterministic. Missing atoms are omitted.

The active-frame command has trajectory counterparts documented in
[Trajectory time-series analysis](TIME_SERIES_ANALYSIS.md). Parallel frame
execution and persistent contact result objects remain future work.

## Hydrogen bonds

```text
analyze hbonds --donors "chain A" --acceptors "chain B" \
  --cutoff 3.5 --angle 30 --pbc raw --unit angstrom
hbonds --donors all --cutoff 3.5 --angle 30
```

The cutoff is the donor-to-acceptor distance. `--angle` is the maximum allowed
deviation from a linear 180-degree D-H-A angle. If `--acceptors` is omitted,
the donor selection is considered for both roles and both directions are
tested. Only non-hydrogen donor/acceptor atoms are considered, and a donor must
have a topology bond to the reported hydrogen. This matches the observable
geometry and selection-role contract of VMD `measure hbonds`.

Typing is explicit and provenance-bearing. Boolean topology atom properties
`hbond_donor` and `hbond_acceptor` override all inference. A property with the
wrong type is an error. If absent, `element-bond-v1` treats N/O/S atoms bonded
to H as donors, while `element-charge-v1` treats neutral or negatively charged
N/O/S as acceptors. The response records both typing sources and
`typing_estimated`. These fallback rules are intentionally simple and can
overestimate chemically unavailable acceptors; force-field or curated typing
should use the explicit properties.

Raw and exact triclinic minimum-image modes apply consistently to the D-A
cutoff and both H-centered angle vectors. The result table preserves donor,
acceptor and hydrogen indices, D-A distance, and angular deviation.
