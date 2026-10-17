# Capability 지원표 v2 (다중 baseline)

<!-- Generated evidence projection. Do not edit manually. -->

공용 capability registry 전체를 baseline별로 보여 주는 snapshot이다. `검증됨` 행만 compatibility claim이며 PyMOL 또는 VMD 전체 parity 완료 주장이 아니다. 한 capability가 여러 baseline에 걸치면 각 baseline의 분모에 한 번씩 포함되고, 전체 분모에서는 한 번만 센다. 하위 호환 PyMOL OSS 전용 표는 [`CAPABILITY_SUPPORT.md`](CAPABILITY_SUPPORT.md)(manifest v1)에 그대로 유지한다.

Machine-readable data: [`capability-support-v2.json`](capability-support-v2.json)

검토일 `2026-10-14`.

## Baseline별 분모

| Baseline | 버전/commit | 기준일 | 분모 | 검증됨 | 진행 중 | 계획 | 사용 불가 |
|---|---|---|---:|---:|---:|---:|---:|
| `pymol-incentive-3.1.8` | `3.1.8` | 2026-10-02 | 35 | 0 | 5 | 0 | 30 |
| `pymol-oss-3.1.0` | `f51e58f6b08308c41c85b9d12a23231f49ca325a` | 2026-10-03 | 1942 | 137 | 467 | 0 | 1338 |
| `source-audit-2026-10` | `—` | 2026-10-17 | 522 | 0 | 72 | 0 | 450 |
| `vmd-1.9.3` | `1.9.3` | 2026-10-02 | 533 | 23 | 172 | 0 | 338 |
| `vmd-1.9.4a57` | `1.9.4a48` | 2026-10-02 | 573 | 23 | 172 | 0 | 378 |
| `vmd-plugin-catalog-current` | `—` | 2026-10-02 | 109 | 2 | 14 | 0 | 93 |

## 전체 상태 요약(중복 제거)

| 상태 | 개수 | 의미 |
|---|---:|---|
| 검증됨 | 137 | implementation이 compatible/robust이며 fixture, evidence, module과 public interface가 모두 연결됨 |
| 진행 중(미검증) | 621 | partial 구현이며 compatibility 완료 주장이 아님 |
| 인벤토리/계획 | 0 | inventory 또는 acceptance 설계 단계이며 사용자가 실행할 수 없음 |
| 사용 불가/보류 | 2225 | 보류, 제외, 접근 제한 또는 license 격리 상태 |

## Domain 요약

| Domain | 검증됨 | 진행 중 | 계획 | 사용 불가 | 전체 |
|---|---:|---:|---:|---:|---:|
| `analysis` | 0 | 67 | 0 | 197 | 264 |
| `automation` | 2 | 39 | 0 | 142 | 183 |
| `data` | 0 | 0 | 0 | 43 | 43 |
| `editing` | 0 | 1 | 0 | 172 | 173 |
| `io` | 0 | 54 | 0 | 173 | 227 |
| `object_state` | 0 | 71 | 0 | 71 | 142 |
| `pbc` | 0 | 2 | 0 | 59 | 61 |
| `performance` | 0 | 0 | 0 | 60 | 60 |
| `plugin` | 0 | 2 | 0 | 40 | 42 |
| `rendering` | 3 | 55 | 0 | 296 | 354 |
| `representation` | 101 | 70 | 0 | 537 | 708 |
| `scene` | 31 | 57 | 0 | 121 | 209 |
| `selection` | 0 | 172 | 0 | 86 | 258 |
| `trajectory` | 0 | 31 | 0 | 84 | 115 |
| `ui` | 0 | 0 | 0 | 144 | 144 |

## Capability 행

| Capability | 제목 | Baseline | 우선순위 | 상태 | GUI | CLI | Python | Acceptance | Platform | Evidence |
|---|---|---|---:|---|:---:|:---:|:---:|---|---|---:|
| `analysis.alignment.all_to_target` | Align all objects to a target | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `analysis.alignment.atom_swap` | Symmetric atom swapping for RMSD | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.alignment.ce` | Combinatorial Extension alignment | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.alignment.multi_object` | Align multiple objects to one reference | `pymol-oss-3.1.0`, `vmd-plugin-catalog-current` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `analysis.alignment.residue_structural` | Residue/structure-guided refined alignment | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.alignment.sequence_refined` | Sequence-guided refined alignment | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.atom_property.iterate` | Iterate atom properties read-only | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `analysis.center.mass` | Selection center of mass | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `analysis.charge.sum_formal` | Sum formal charges | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `analysis.charge.sum_partial` | Sum partial charges | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `analysis.chemical_motifs` | Chemical motif search | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.collective_variables` | Collective-variable analysis via PLUMED | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.contact_map` | Residue contact map plot | `vmd-plugin-catalog-current` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.contacts.find_pairs` | Find atom pairs API | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `analysis.contacts.interchain` | Inter-chain contact distances | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `analysis.coordinates.iterate_state` | Iterate atom coordinates read-only | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `analysis.delphi_force` | DelPhi electrostatic force | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.density_distribution` | 1-D/2-D molecular density distribution | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.density_profile` | 1-D density profile | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.electron_transfer_pathways` | Electron-transfer pathway analysis | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.electrostatics.vacuum_esp` | Protein contact potential (vacuum) | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.essential_dynamics` | Interactive essential dynamics | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.fit.explicit_pairs` | Explicit-pair rigid fit | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `analysis.fit.identifier_matched` | Identifier-matched rigid fit | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `analysis.geometry.angle` | Scalar atom angle query | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `analysis.geometry.dihedral` | Scalar atom dihedral query | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `analysis.geometry.distance` | Scalar atom distance query | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `unwritten` | — | 2 |
| `analysis.interface_extraction` | Protein-protein interface extraction | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.ir_spectrum` | IR spectral density | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.kinetic_model` | Thermodynamic/kinetic model construction | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.mass.compute` | Compute molecular mass | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `analysis.measure.avpos` | Returns the average position of each of the selected atoms, for the selected fra | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.measure.center` | Returns the geometric center of atoms in selection using the given weight  | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.measure.cluster` | Performs a cluster analysis (find clusters of timesteps that are similar with re | `vmd-1.9.3`, `vmd-1.9.4a57`, `vmd-plugin-catalog-current` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.measure.dipole` | Compute the dipole moment vector of the atoms in selection from their respective | `vmd-1.9.3`, `vmd-1.9.4a57`, `vmd-plugin-catalog-current` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.measure.energy` | energy_term atom_list [parameters] [options]: Returns the specified energy term  | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `analysis.measure.fit` | Returns a 4x4 transformation matrix which, when applied to the atoms in selectio | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `analysis.measure.gofr` | Calculates the atomic radial pair distribution function and the number integral  | `vmd-1.9.3`, `vmd-1.9.4a57`, `vmd-plugin-catalog-current` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.measure.hbonds` | Find all hydrogen bonds in the given selection(s), using simple geometric criter | `vmd-1.9.3`, `vmd-1.9.4a57`, `vmd-plugin-catalog-current` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.measure.imprp` | Returns the improper dihedral angle defined by four atoms | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.measure.inertia` | Returns the center of mass and the principles axes of inertia for the selected a | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `analysis.measure.inverse` | Returns the inverse of the given 4x4 matrix | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `analysis.measure.manage` | measure command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.measure.pbc2onc` | center [frame frame last ]: Computes the transformation matrix that transforms c | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.measure.pbcneighbors` | center cutoff [ options ]: Returns all image atoms that are within cutoff Å of t | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.measure.rgyr` | Returns the radius of gyration of atoms in selection using the given weight  | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `analysis.measure.rmsf` | Returns the root mean square position fluctuation for each selected atom in the  | `vmd-1.9.3`, `vmd-1.9.4a57`, `vmd-plugin-catalog-current` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.measure.sumweights` | Returns the sum of the list of weights (or data field to use for the weights) fo | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `analysis.measure.surface` | selection gridsize radius depth : Returns a list of atom indices comprising the  | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.measure.symmetry` | This function evaluates the molecular symmetry of an atom selection | `vmd-1.9.3`, `vmd-1.9.4a57`, `vmd-plugin-catalog-current` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.measurement_object.angle` | Angle measurement object | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 2 |
| `analysis.measurement_object.dihedral` | Dihedral measurement object | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 2 |
| `analysis.measurement_object.distance` | Distance measurement object | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `unwritten` | — | 2 |
| `analysis.measurement_object.distance.all_pairs` | All interatomic distances | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.measurement_object.distance.bonded` | Bond distances | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.measurement_object.distance.centroid` | Centroid distance | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `analysis.measurement_object.distance.exclusion_filtered` | Exclusion-filtered distances | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.measurement_object.distance.pi_cation` | Pi-cation interactions | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `analysis.measurement_object.distance.pi_interactions` | Pi and pi-cation interactions | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `analysis.measurement_object.distance.pi_pi` | Pi-pi interactions | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `analysis.measurement_object.distance.polar_contacts` | Polar-contact distances | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `analysis.measurement_object.distance.vdw_ratio` | VDW-ratio contact distances | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.membrane` | Membrane analysis | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.network_view` | Protein interaction network view | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.normal_modes` | Normal-mode visualization and comparison | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pka_propka` | pKa prediction via PROPKA | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.auto_measure` | PyMOL cmd.auto_measure | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.get_assembly_ids` | PyMOL cmd.get_assembly_ids | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.get_atom_coords` | PyMOL cmd.get_atom_coords | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.get_bonds` | PyMOL cmd.get_bonds | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.get_coords` | PyMOL cmd.get_coords | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.get_coordset` | PyMOL cmd.get_coordset | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.get_drag_object_name` | PyMOL cmd.get_drag_object_name | `source-audit-2026-10` | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.get_legal_name` | PyMOL cmd.get_legal_name | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.get_modal_draw` | PyMOL cmd.get_modal_draw | `source-audit-2026-10` | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.get_movie_length` | PyMOL cmd.get_movie_length | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.get_movie_locked` | PyMOL cmd.get_movie_locked | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.get_names_of_type` | PyMOL cmd.get_names_of_type | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.get_object_settings` | PyMOL cmd.get_object_settings | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.get_object_state` | PyMOL cmd.get_object_state | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.get_object_ttt` | PyMOL cmd.get_object_ttt | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.get_raw_alignment` | PyMOL cmd.get_raw_alignment | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.get_selection_state` | PyMOL cmd.get_selection_state | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.get_unused_name` | PyMOL cmd.get_unused_name | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.get_volume_field` | PyMOL cmd.get_volume_field | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_querying.get_volume_histogram` | PyMOL cmd.get_volume_histogram | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_seqalign.alignment_mapping` | PyMOL seqalign.alignment_mapping | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.pymol_seqalign.needle_alignment` | PyMOL seqalign.needle_alignment | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.query.atom_ids` | Query atom ID list | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `analysis.query.atom_indices` | Query object atom indices | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `analysis.query.atom_source_id` | Query single-atom source ID | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.query.chains` | Query chain identifiers | `pymol-oss-3.1.0`, `vmd-plugin-catalog-current` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `analysis.query.extent` | Query selection extent | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `analysis.query.overlap` | Sum VDW overlap | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.query.phi_psi` | Query backbone phi/psi | `pymol-oss-3.1.0`, `vmd-plugin-catalog-current` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `analysis.rms.current` | Current-coordinate RMS | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57`, `vmd-plugin-catalog-current` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `analysis.rms.fitted` | Fitted RMS without coordinate mutation | `pymol-oss-3.1.0`, `vmd-plugin-catalog-current` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `analysis.rna_secondary_structure` | RNA secondary structure via RNAView | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.salt_bridges` | Salt-bridge detection over a trajectory | `vmd-plugin-catalog-current` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.secondary_structure.dss` | Assign secondary structure (dss) | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `analysis.secondary_structure.legacy_ss` | Legacy secondary structure routine | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_helix_phi_exclude` | Setting ss_helix_phi_exclude | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_helix_phi_include` | Setting ss_helix_phi_include | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_helix_phi_target` | Setting ss_helix_phi_target | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_helix_psi_exclude` | Setting ss_helix_psi_exclude | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_helix_psi_include` | Setting ss_helix_psi_include | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_helix_psi_target` | Setting ss_helix_psi_target | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_strand_phi_exclude` | Setting ss_strand_phi_exclude | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_strand_phi_include` | Setting ss_strand_phi_include | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_strand_phi_target` | Setting ss_strand_phi_target | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_strand_psi_exclude` | Setting ss_strand_psi_exclude | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_strand_psi_include` | Setting ss_strand_psi_include | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_strand_psi_target` | Setting ss_strand_psi_target | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.coulomb_cutoff` | Setting coulomb_cutoff | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.coulomb_dielectric` | Setting coulomb_dielectric | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.coulomb_units_factor` | Setting coulomb_units_factor | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.fit_iterations` | Setting fit_iterations | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.fit_kabsch` | Setting fit_kabsch | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.fit_tolerance` | Setting fit_tolerance | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.gaussian_b_adjust` | Setting gaussian_b_adjust | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.gaussian_b_floor` | Setting gaussian_b_floor | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.gaussian_resolution` | Setting gaussian_resolution | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.h_bond_cone` | Setting h_bond_cone | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.h_bond_cutoff_center` | Setting h_bond_cutoff_center | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.h_bond_cutoff_edge` | Setting h_bond_cutoff_edge | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.h_bond_exclusion` | Setting h_bond_exclusion | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.h_bond_from_proton` | Setting h_bond_from_proton | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.h_bond_max_angle` | Setting h_bond_max_angle | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.h_bond_power_a` | Setting h_bond_power_a | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.h_bond_power_b` | Setting h_bond_power_b | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.halogen_bond_as_acceptor_max_acceptor_angle` | Setting halogen_bond_as_acceptor_max_acceptor_angle | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.halogen_bond_as_acceptor_min_acceptor_angle` | Setting halogen_bond_as_acceptor_min_acceptor_angle | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.halogen_bond_as_acceptor_min_donor_angle` | Setting halogen_bond_as_acceptor_min_donor_angle | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.halogen_bond_as_donor_min_acceptor_angle` | Setting halogen_bond_as_donor_min_acceptor_angle | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.halogen_bond_as_donor_min_donor_angle` | Setting halogen_bond_as_donor_min_donor_angle | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.halogen_bond_distance` | Setting halogen_bond_distance | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.heavy_neighbor_cutoff` | Setting heavy_neighbor_cutoff | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.neighbor_cutoff` | Setting neighbor_cutoff | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.polar_neighbor_cutoff` | Setting polar_neighbor_cutoff | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.salt_bridge_distance` | Setting salt_bridge_distance | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.signal_processing` | Signal processing of time series | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.stereochemistry.assign` | Assign R/S stereo property | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.surface_area.relative_per_residue` | Relative per-residue SASA | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.surface_area.sasa` | Solvent-accessible surface area | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57`, `vmd-plugin-catalog-current` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `analysis.trajectory_comparison` | Interactive trajectory comparison | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_bias.adaptive_biasing_force` | VMD Colvars method: Adaptive Biasing Force | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_bias.adaptive_linear_bias_experiment_directed_simulati_on` | VMD Colvars method: Adaptive Linear Bias/Experiment Directed Simulati on | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_bias.extended_system_adaptive_biasing_force_eabf` | VMD Colvars method: Extended-system Adaptive Biasing Force (eABF) | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_bias.harmonic_restraints` | VMD Colvars method: Harmonic restraints | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_bias.linear_restraints` | VMD Colvars method: Linear restraints | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_bias.metadynamics` | VMD Colvars method: Metadynamics | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_bias.multidimensional_histograms` | VMD Colvars method: Multidimensional histograms | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_bias.probability_distribution_restraints` | VMD Colvars method: Probability distribution-restraints | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_bias.scripted_biases` | VMD Colvars method: Scripted biases | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_component.angle` | VMD Colvars component angle | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_component.cartesian` | VMD Colvars component cartesian | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_component.coordnum` | VMD Colvars component coordNum | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_component.dihedral` | VMD Colvars component dihedral | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_component.dipoleangle` | VMD Colvars component dipoleAngle | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_component.distance` | VMD Colvars component distance | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_component.distancedir` | VMD Colvars component distanceDir | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_component.distanceinv` | VMD Colvars component distanceInv | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_component.distancepairs` | VMD Colvars component distancePairs | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_component.distancevec` | VMD Colvars component distanceVec | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_component.distancexy` | VMD Colvars component distanceXY | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_component.distancez` | VMD Colvars component distanceZ | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_component.hbond` | VMD Colvars component hBond | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_colvars_component.selfcoordnum` | VMD Colvars component selfCoordNum | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_hbonds_option.ang` | VMD hbonds option -ang | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_hbonds_option.detailout` | VMD hbonds option -detailout | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_hbonds_option.dist` | VMD hbonds option -dist | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_hbonds_option.frames` | VMD hbonds option -frames | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_hbonds_option.outdir` | VMD hbonds option -outdir | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_hbonds_option.outfile` | VMD hbonds option -outfile | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_hbonds_option.plot` | VMD hbonds option -plot | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_hbonds_option.polar` | VMD hbonds option -polar | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_hbonds_option.sel1` | VMD hbonds option -sel1 | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_hbonds_option.sel2` | VMD hbonds option -sel2 | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_hbonds_option.type` | VMD hbonds option -type | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_hbonds_option.writefile` | VMD hbonds option -writefile | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_mdffi.calcdiffmap` | VMD mdffi -calcdiffmap | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_mdffi.calcspatialccmap` | VMD mdffi -calcspatialccmap | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_mdffi.calcsynthmap` | VMD mdffi -calcsynthmap | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_mdffi.savespatialccmap` | VMD mdffi -savespatialccmap | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_mdffi.savesynthmap` | VMD mdffi -savesynthmap | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_mdffi.verbose` | VMD mdffi -verbose | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_mdffi.weight` | VMD mdffi --weight | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_measure.fitrmsd` | VMD measure fitrmsd | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_measure.minsize` | VMD measure minsize | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_measure.none` | VMD measure none | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_measure.numshared` | VMD measure numshared | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_measure.rgyrd` | VMD measure rgyrd | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_measure.storenum` | VMD measure storenum | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_measure.storesize` | VMD measure storesize | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_saltbr_option.comdist` | VMD saltbr option -comdist | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_saltbr_option.frames` | VMD saltbr option -frames | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_saltbr_option.ondist` | VMD saltbr option -ondist | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_saltbr_option.outdir` | VMD saltbr option -outdir | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_saltbr_option.sel` | VMD saltbr option -sel | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_saltbr_option.writefiles` | VMD saltbr option -writefiles | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl.transabout` | VMD Tcl command transabout | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl.transmult` | VMD Tcl command transmult | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl.transvec` | VMD Tcl command transvec | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl.transvecinv` | VMD Tcl command transvecinv | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl.vecadd` | VMD Tcl command vecadd | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl.veclength` | VMD Tcl command veclength | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl.vecmean` | VMD Tcl command vecmean | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl.vecscale` | VMD Tcl command vecscale | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl.vecstddev` | VMD Tcl command vecstddev | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl.vecsub` | VMD Tcl command vecsub | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl.vecsum` | VMD Tcl command vecsum | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl.vectrans` | VMD Tcl command vectrans | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl_library.trans_from_offset` | VMD Tcl library procedure trans_from_offset | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl_library.trans_from_rotate` | VMD Tcl library procedure trans_from_rotate | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl_library.trans_to_offset` | VMD Tcl library procedure trans_to_offset | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl_library.trans_to_rotate` | VMD Tcl library procedure trans_to_rotate | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl_library.transaxis` | VMD Tcl library procedure transaxis | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl_library.transidentity` | VMD Tcl library procedure transidentity | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl_library.transoffset` | VMD Tcl library procedure transoffset | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl_library.transtranspose` | VMD Tcl library procedure transtranspose | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl_library.veccross` | VMD Tcl library procedure veccross | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl_library.vecdist` | VMD Tcl library procedure vecdist | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl_library.vecdot` | VMD Tcl library procedure vecdot | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl_library.vecinvert` | VMD Tcl library procedure vecinvert | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl_library.veclength2` | VMD Tcl library procedure veclength2 | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl_library.vecmul` | VMD Tcl library procedure vecmul | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl_library.vecnorm` | VMD Tcl library procedure vecnorm | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_tcl_library.veczero` | VMD Tcl library procedure veczero | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_volgradient.mol` | VMD volgradient -mol | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_volgradient.vol` | VMD volgradient -vol | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_volmap.average` | VMD volmap average | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_volmap.coulombmsm` | VMD volmap coulombmsm | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_volmap.coulombpotential` | VMD volmap coulombpotential | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_volmap.maximum` | VMD volmap maximum | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.vmd_volmap.minimum` | VMD volmap minimum | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.volume_interior` | Interior volume of a container molecule | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `analysis.volume_measure` | Molecular volume measurement | `vmd-plugin-catalog-current` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `density_fit.cross_correlation` | Map-model cross correlation | `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `density_fit.mdff.cc` | Computes the MDFF cross correlation between an selection from an all-atom molecu | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `density_fit.mdff.manage` | mdffi command family | `vmd-1.9.3`, `vmd-1.9.4a57`, `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `density_fit.mdff.sim` | Compute a simulated density map from an atom selection on an all-atom molecular  | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `density_fit.rigid_fit` | Rigid-body fit into density | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `density_fit.simulate_map` | Simulated density map from atoms | `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `docking.autodock` | Docking via AutoDock/AutoGrid | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `measurement.label.addspring` | Add a spring connecting the atom(s) atomID and atomID2 of the molecule molID  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `measurement.label.delete` | Delete labels in the given category | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `measurement.label.graph` | Retrieve the values of the labels for all timesteps | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `measurement.label.hide` | Turn off labels in the given category | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `measurement.label.list` | List all labels in the given category | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `measurement.label.list_2` | Return a list of available categories | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `measurement.label.manage` | label command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `measurement.label.show` | Turn on labels in the given category | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `measurement.label.textsize` | Get/set the text size for all labels, which is 1.0 by default | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `measurement.label.textthickness` | Get/set the text line thickness for all labels, which is 1.0 by default | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plot.heatmap` | 2-D heat-map plot | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plot.multiplot` | Scriptable 2-D data plot | `vmd-plugin-catalog-current` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plot.namd_log` | Plot NAMD log quantities | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plot.ramachandran` | Ramachandran plot | `vmd-plugin-catalog-current` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plot.timeline` | Per-residue time-series timeline plot | `vmd-plugin-catalog-current` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.bundled.apbs_electrostatics` | APBS electrostatics plugin | `pymol-oss-3.1.0`, `vmd-plugin-catalog-current` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `sequence.multiple_alignment` | Multiple sequence alignment and display | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `simulation.binding_free_energy` | Binding free energy estimation | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `simulation.fep.analysis` | FEP simulation analysis | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `simulation.namd.energy` | Interaction energy via NAMD | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `app.query.version` | Query application version | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `automation.command.alias` | Register a literal command alias | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `automation.command.dispatch` | Dispatch command text from Python | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `automation.command.introspect` | Inspect command-to-API binding | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `automation.command.register` | Register a Python function as a command | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.command.register_autocomplete` | Register command argument completion | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `automation.command.session_ephemeral` | Do not persist dynamic commands in sessions | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.embedding.context_manager` | Context-managed embedded lifecycle | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `automation.embedding.finish_launching` | Threaded finish-launching helper | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `automation.embedding.headless` | Headless embedded launch loop | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `automation.embedding.manage` | Embedded application lifecycle | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.embedding.multi_instance` | Multiple embedded instances | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `automation.embedding.singleton` | Embedded singleton instance | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `automation.numpy.atomselect` | atomselect(molid, frame, selection) : Returns an array of int's representing fla | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.numpy.direct` | reference to the given set of atom coordinates | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.pymol_area.pymol2_instances` | PyMOL multiple independent instances API (pymol2) | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `automation.pymol_area.remote_http_server` | PyMOL embedded HTTP command server | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.pymol_area.remote_rpc_server` | PyMOL XML-RPC remote control server and CGO helper RPC | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.pymol_commanding.async` | PyMOL cmd.async_ | `source-audit-2026-10` | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.script.execute` | Execute an external script | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `automation.script.literal_python_block` | Literal Python block in command script | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | macos | 4 |
| `automation.script.namespace.global` | Run in the global PyMOL namespace | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.script.namespace.local` | Run with local variable isolation | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.script.namespace.main` | Run in the main Python namespace | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.script.namespace.module` | Run as a synthetic Python module | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.script.namespace.private` | Run with private local variables | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.script.pml_file` | Execute a PML command script file | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | macos | 4 |
| `automation.script.python_file` | Execute a Python script file | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `automation.script.threaded_spawn` | Spawn a Python script thread | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `automation.script.trust_policy` | External script trust and security policy | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `automation.startup.command` | Execute startup command argument | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.startup.config_discovery` | Discover startup configuration scripts | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.startup.deferred_order` | Deterministic deferred startup order | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.startup.disable_automatic` | Disable startup scripts and plugins | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.startup.manage` | Startup automation lifecycle | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.startup.python_foreground` | Execute startup Python script in foreground | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.startup.python_threaded` | Execute startup Python script in a thread | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `automation.startup.stdin` | Read command stream from standard input | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `automation.vmd_cmdline.args` | VMD command-line option -args | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_cmdline.debug` | VMD command-line option -debug | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_cmdline.dispdev` | VMD command-line option -dispdev | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_cmdline.dist` | VMD command-line option -dist | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_cmdline.e` | VMD command-line option -e | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_cmdline.eofexit` | VMD command-line option -eofexit | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_cmdline.f` | VMD command-line option -f | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_cmdline.h` | VMD command-line option -h | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_cmdline.m` | VMD command-line option -m | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_cmdline.nt` | VMD command-line option -nt | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_cmdline.python` | VMD command-line option -python | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_cmdline.startup` | VMD command-line option -startup | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_colvars_cv.config` | VMD Colvars cv config | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_colvars_cv.configfile` | VMD Colvars cv configfile | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_colvars_cv.delete` | VMD Colvars cv delete | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_colvars_cv.frame` | VMD Colvars cv frame | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_colvars_cv.list` | VMD Colvars cv list | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_colvars_cv.list_biases` | VMD Colvars cv list biases | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_colvars_cv.load` | VMD Colvars cv load | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_colvars_cv.molid` | VMD Colvars cv molid | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_colvars_cv.printframe` | VMD Colvars cv printframe | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_colvars_cv.printframelabels` | VMD Colvars cv printframelabels | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_colvars_cv.reset` | VMD Colvars cv reset | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_colvars_cv.save` | VMD Colvars cv save | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_colvars_cv.update` | VMD Colvars cv update | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_colvars_cv.version` | VMD Colvars cv version | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdbabelbin` | VMD environment variable VMDBABELBIN | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdcavemem` | VMD environment variable VMDCAVEMEM | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdcustomizestartup` | VMD environment variable VMDCUSTOMIZESTARTUP | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmddir` | VMD environment variable VMDDIR | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmddisablestereo` | VMD environment variable VMDDISABLESTEREO | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdfilechooser` | VMD environment variable VMDFILECHOOSER | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdforceconsoletty` | VMD environment variable VMDFORCECONSOLETTY | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdforcecpucount` | VMD environment variable VMDFORCECPUCOUNT | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdfreevrmem` | VMD environment variable VMDFREEVRMEM | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdgdisplay` | VMD environment variable VMDGDISPLAY | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdglslverbose` | VMD environment variable VMDGLSLVERBOSE | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdhtmlviewer` | VMD environment variable VMDHTMLVIEWER | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdimageviewer` | VMD environment variable VMDIMAGEVIEWER | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdimmersadeskflip` | VMD environment variable VMDIMMERSADESKFLIP | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdmacenableeextensions` | VMD environment variable VMDMACENABLEEEXTENSIONS | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdmaxaasamples` | VMD environment variable VMDMAXAASAMPLES | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdmsecdelayhack` | VMD environment variable VMDMSECDELAYHACK | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdmsmsusefile` | VMD environment variable VMDMSMSUSEFILE | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdnocuda` | VMD environment variable VMDNOCUDA | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdpreferstereo` | VMD environment variable VMDPREFERSTEREO | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdscrdist` | VMD environment variable VMDSCRDIST | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdscrheight` | VMD environment variable VMDSCRHEIGHT | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdscrpos` | VMD environment variable VMDSCRPOS | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdscrsize` | VMD environment variable VMDSCRSIZE | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdshearstereo` | VMD environment variable VMDSHEARSTEREO | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdsimplegraphics` | VMD environment variable VMDSIMPLEGRAPHICS | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdtmpdir` | VMD environment variable VMDTMPDIR | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_env.vmdwiregl` | VMD environment variable VMDWIREGL | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_python_callback.display_update` | VMD Python callback display_update | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_python_callback.frame` | VMD Python callback frame | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_python_callback.help` | VMD Python callback help | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_python_callback.initialize_structure` | VMD Python callback initialize_structure | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_python_callback.pick_atom` | VMD Python callback pick_atom | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_python_callback.pick_value` | VMD Python callback pick_value | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_python_callback.timestep` | VMD Python callback timestep | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_python_callback.trajectory` | VMD Python callback trajectory | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_startup.vmdrc` | VMD user startup script .vmdrc / vmd.rc | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl.colvars` | VMD Tcl command colvars | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl.cv` | VMD Tcl command cv | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl.gopython` | VMD Tcl command gopython | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl.pointlight` | VMD Tcl command pointlight | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl.rawtimestep` | VMD Tcl command rawtimestep | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl.rotmat` | VMD Tcl command rotmat | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl.spaceball` | VMD Tcl command spaceball | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl.tkrender` | VMD Tcl command tkrender | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl.vmd_label` | VMD Tcl command vmd_label | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl.vmd_menu` | VMD Tcl command vmd_menu | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl.vmd_scale` | VMD Tcl command vmd_scale | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl.vmdbench` | VMD Tcl command vmdbench | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl.vmdcollab` | VMD Tcl command vmdcollab | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl.vmdcon` | VMD Tcl command vmdcon | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl.volgradient` | VMD Tcl command volgradient | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl_callback.vmd_frame` | VMD Tcl callback variable vmd_frame | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl_callback.vmd_graph_label` | VMD Tcl callback variable vmd_graph_label | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl_callback.vmd_initialize_structure` | VMD Tcl callback variable vmd_initialize_structure | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl_callback.vmd_logfile` | VMD Tcl callback variable vmd_logfile | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl_callback.vmd_molecule` | VMD Tcl callback variable vmd_molecule | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl_callback.vmd_pick_atom_silent` | VMD Tcl callback variable vmd_pick_atom_silent | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl_callback.vmd_pick_client` | VMD Tcl callback variable vmd_pick_client | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl_callback.vmd_pick_event` | VMD Tcl callback variable vmd_pick_event | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl_callback.vmd_pick_mol_silent` | VMD Tcl callback variable vmd_pick_mol_silent | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl_callback.vmd_pick_shift_state` | VMD Tcl callback variable vmd_pick_shift_state | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl_callback.vmd_quit` | VMD Tcl callback variable vmd_quit | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl_callback.vmd_timestep` | VMD Tcl callback variable vmd_timestep | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl_callback.vmd_trajectory_read` | VMD Tcl callback variable vmd_trajectory_read | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl_library.coordtrans` | VMD Tcl library procedure coordtrans | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl_library.vmd_install_extension` | VMD Tcl library procedure vmd_install_extension | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_tcl_library.vmd_set_center` | VMD Tcl library procedure vmd_set_center | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_vmdinfo.cpuaffinity` | VMD vmdinfo cpuaffinity | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_vmdinfo.dispdev` | VMD vmdinfo dispdev | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_vmdinfo.freemem` | VMD vmdinfo freemem | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_vmdinfo.nodecount` | VMD vmdinfo nodecount | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_vmdinfo.nodename` | VMD vmdinfo nodename | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_vmdinfo.noderank` | VMD vmdinfo noderank | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_vmdinfo.numcpus` | VMD vmdinfo numcpus | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `automation.vmd_vmdinfo.numcudadevices` | VMD vmdinfo numcudadevices | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule_api.clearreps` | clearReps() : Removes all reps from this molecule | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule_api.construct` | __init__(id=None) : Creating a new Molecule instance with no arguments will crea | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule_api.files` | files() : | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule_api.get_accessions` | get_accessions(molid) : Returns a list of accessiosn corresponding to get_filena | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule_api.get_databases` | get_databases(molid) : Returns a list of databases corresponding to get_filename | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule_api.get_filenames` | get_filenames(molid) : Returns a list of filenames that have been loaded into th | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule_api.get_filetypes` | get_filetypes(molid) : Returns a list of filetypes corresponding to get_filename | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule_api.get_remarks` | get_remarks(molid) : Returns a list of remarks corresponding to get_filenames  | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule_api.new` | new(name) : Creates a new empty molecule with the given name and returns its id | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule_api.numreps` | numReps() : Returns the number of molecular representations (reps) in the given  | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule_api.reps` | reps() : Returns a list of MoleculeRep objects, one for each rep in the molecule | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule_api.types` | types() : Returns a list of filenames and file types, respectively, for the file | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `presentation.demo_script` | Scripted live demos | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `script.api.sync` | Synchronize command queue | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `script.command.declare_typed` | Declare typed command | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | macos | 6 |
| `script.feedback.control` | Output feedback control | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `script.flow.manage` | sleep command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `script.flow.sleep` | sleep time seconds | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `script.flow.wait` | wait time seconds | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `script.log.close` | Close command log | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `script.log.console` | Turn on logging and direct it to the VMD text console window | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `script.log.manage` | logfile command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `script.log.open` | Open command log | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `script.log.resume` | Resume command log | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `script.log.write` | Write log entry | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `script.pml.abort` | Abort script execution | `pymol-oss-3.1.0` | P1 | 검증됨 (`compatible`) | — | 예 | 예 | `compatible` | macos | 4 |
| `script.pml.embedded_data_block` | Embedded data block | `pymol-oss-3.1.0` | P2 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | macos | 4 |
| `script.pml.skip_block` | Skip command block | `pymol-oss-3.1.0` | P2 | 검증됨 (`compatible`) | — | 예 | 예 | `compatible` | macos | 4 |
| `script.setting.log_box_selections` | Setting log_box_selections | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `script.setting.log_conformations` | Setting log_conformations | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `script.setting.logging` | Setting logging | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `script.setting.robust_logs` | Setting robust_logs | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `script.setting.security` | Setting security | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `script.setting.stop_on_exceptions` | Setting stop_on_exceptions | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `script.shell.change_directory` | Change working directory | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `script.shell.list_directory` | List directory | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `script.shell.print_directory` | Print working directory | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `script.shell.system` | Execute shell command | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `security.session_prompt.accept` | Accept untrusted session content | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `security.session_prompt.decline` | Decline untrusted session content | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `trajectory.timeline_composition.api` | Timeline composition Python API | `pymol-incentive-3.1.8` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `trajectory.timestep_access.manage` | gettimestep command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `trajectory.timestep_access.timestep` | retrieve timestep as a Tcl byte array for use in compiled analysis plugins | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `data.per_residue_import` | Import per-residue values into an atom field | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.map.combine` | Combine maps | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.combine.average` | Map combine average | `pymol-oss-3.1.0`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.combine.difference` | Map combine difference | `pymol-oss-3.1.0`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.combine.maximum` | Map combine maximum | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.combine.minimum` | Map combine minimum | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.combine.sum` | Map combine sum | `pymol-oss-3.1.0`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.generate` | Generate map from atoms | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.generate.coulomb` | Map generator coulomb | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.generate.coulomb_local` | Map generator coulomb_local | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.generate.coulomb_neutral` | Map generator coulomb_neutral | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.generate.gaussian` | Map generator gaussian | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.generate.gaussian_max` | Map generator gaussian_max | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.generate.state_policy` | Map generation state policy | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.generate.vdw` | Map generator vdw | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.resample.double` | Double map sampling | `pymol-oss-3.1.0`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.resample.halve` | Halve map sampling | `pymol-oss-3.1.0`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.set_border` | Set map border level | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.trim` | Trim map extent | `pymol-oss-3.1.0`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.pme_potential` | Particle-mesh Ewald potential map | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.volmap.coulomb` | , coulombmsm : Creates a map of the electrostatic field of the atom selection, m | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.volmap.density` | creates a map of the weighted atomic density at each gridpoint | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.volmap.distance` | creates a map for which each gridpoint contains the distance between that point  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.volmap.ils` | Creates a free energy map of the distribution of a weakly-interacting monoatomic | `vmd-1.9.3`, `vmd-1.9.4a57`, `vmd-plugin-catalog-current` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.volmap.interp` | creates a map with the atomic weights interpolated onto a grid | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.volmap.manage` | volmap command family | `vmd-1.9.3`, `vmd-1.9.4a57`, `vmd-plugin-catalog-current` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.volmap.mask` | Creates a map which is set to 0 or 1 depending on whether they are within a spec | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.volmap.occupancy` | Each grid point is set to either 0 or 1, depending on whether it contains onbe o | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.voltool.binary_mask` | Binary threshold mask | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.voltool.center_of_mass` | Map center of mass | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.voltool.clamp` | Clamp map values | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.voltool.gaussian_smooth` | Gaussian-smooth map | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.voltool.histogram` | Map value histogram | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.voltool.info` | Map information query | `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.voltool.manage` | Volumetric map toolkit | `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.voltool.multiply` | Multiply maps | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.voltool.offset` | Add scalar to map | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.voltool.potential_from_density` | Potential map from density | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.voltool.rescale_range` | Rescale map value range | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.voltool.scale` | Scale map values | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.voltool.selection_mask` | Mask map around selection | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.voltool.sigma_normalize` | Sigma-normalize map | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `volume.voltool.transform` | Move map by transform or position | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.atom.geometry_assign` | Assign atom geometry and valence | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.atom_name.uniquify` | Uniquify atom names within residues | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.atom_property.alter` | Alter atom properties by expression | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57`, `vmd-plugin-catalog-current` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.atom_property.assign_amber99` | Assign Amber99 atomic properties | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.atom_property.expand_from_ca` | Expand CA properties to residues | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.atom_property.vdw_fit` | Fit VDW radii to avoid overlap | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.bond.create` | Create bond | `pymol-oss-3.1.0`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.bond.delete` | Delete bonds | `pymol-oss-3.1.0`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.bond.rebond_distance` | Distance-based rebonding | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.bond.valence_cycle` | Cycle picked bond valence | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.bond.valence_set` | Set bond valence | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.builder.fragment` | Load fragment-library structure | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.builder.nucleic_acid` | Build nucleic acid from sequence | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.builder.peptide` | Build peptide from sequence | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.builder.pseudoatom` | Create pseudoatom | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.chemistry.check_forcefield` | Check force-field parameters | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.chemistry.fix` | Recompute chemistry | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.clipboard.ring` | Selection cut/copy/paste ring | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.coordinates.alter_state` | Alter atom coordinates by expression | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.coordinates.reference` | Store and recall reference coordinates | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.coordinates.rotate` | Rotate coordinates or object matrix | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.coordinates.translate` | Translate coordinates or object matrix | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.coordinates.update` | Transfer coordinates between selections | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.dihedral.set` | Set dihedral angle | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.auto_remove_hydrogens` | Setting auto_remove_hydrogens | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.clean_electro_mode` | Setting clean_electro_mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.edit_light` | Setting edit_light | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.editor_auto_dihedral` | Setting editor_auto_dihedral | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.editor_auto_measure` | Setting editor_auto_measure | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.editor_auto_origin` | Setting editor_auto_origin | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.editor_bond_cycle_mode` | Setting editor_bond_cycle_mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.editor_label_fragments` | Setting editor_label_fragments | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.matrix_mode` | Setting matrix_mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.secondary_structure` | Setting secondary_structure | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.suspend_undo` | Setting suspend_undo | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.suspend_undo_atom_count` | Setting suspend_undo_atom_count | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.hydrogens.add` | Add hydrogens | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.hydrogens.fix` | Reposition hydrogens | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.hydrogens.refill_picked` | Refill hydrogens on picked atom | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.minimize.mmff` | MMFF94 energy minimization | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.object.fuse` | Fuse fragments by bond | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.attach` | Attach atom to picked atom | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.clear` | Clear editing picks | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.conformation_redo` | Redo editing conformation | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.conformation_undo` | Undo editing conformation | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.drag` | Interactive coordinate dragging | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.edit` | Pick atoms or bond for editing | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.invert` | Invert picked stereocenter | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.remove` | Remove picked atom or bond | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.replace` | Replace picked atom | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.torsion` | Rotate picked torsion | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.protection.deprotect` | Remove editing protection | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.protection.protect` | Protect atoms from editing | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pymol_chempy.bond_amber` | PyMOL chempy bond_amber module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_chempy.bond_mmff` | PyMOL chempy bond_mmff module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_chempy.bonds` | PyMOL chempy bonds module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_chempy.charge` | PyMOL chempy charge module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_chempy.cpv` | PyMOL chempy cpv module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_chempy.dictdb` | PyMOL chempy dictdb module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_chempy.hetatm` | PyMOL chempy hetatm module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_chempy.mass` | PyMOL chempy mass module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_chempy.models` | PyMOL chempy models module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_chempy.neighbor` | PyMOL chempy neighbor module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_chempy.place` | PyMOL chempy place module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_chempy.protein` | PyMOL chempy protein module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_chempy.protein_amber` | PyMOL chempy protein_amber module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_chempy.protein_amber99` | PyMOL chempy protein_amber99 module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_chempy.protein_mmff` | PyMOL chempy protein_mmff module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_chempy.protein_residues` | PyMOL chempy protein_residues module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_chempy.solvate` | PyMOL chempy solvate module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_chempy.water_amber` | PyMOL chempy water_amber module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_chempy.water_residues` | PyMOL chempy water_residues module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_editing.add_bond` | PyMOL cmd.add_bond | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_editing.alter_list` | PyMOL cmd.alter_list | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_editing.push_undo` | PyMOL cmd.push_undo | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_editing.set_discrete` | PyMOL cmd.set_discrete | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_editing.set_object_ttt` | PyMOL cmd.set_object_ttt | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_editing.set_state_order` | PyMOL cmd.set_state_order | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_editing.transform_object` | PyMOL cmd.transform_object | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_editing.transform_selection` | PyMOL cmd.transform_selection | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_editor.add2po` | PyMOL editor.add2pO | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_editor.attach_amino_acid` | PyMOL editor.attach_amino_acid | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_editor.attach_fragment` | PyMOL editor.attach_fragment | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_editor.attach_nuc_acid` | PyMOL editor.attach_nuc_acid | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_editor.attach_o5_phosphate` | PyMOL editor.attach_O5_phosphate | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_editor.bond_double_stranded` | PyMOL editor.bond_double_stranded | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_editor.bond_single_stranded` | PyMOL editor.bond_single_stranded | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_editor.build_peptide` | PyMOL editor.build_peptide | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_editor.combine_fragment` | PyMOL editor.combine_fragment | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_editor.extend_nuc_acid` | PyMOL editor.extend_nuc_acid | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.pymol_editor.rename_three_to_one` | PyMOL editor.rename_three_to_one | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `editing.residue.mse_to_met` | Convert MSE to MET | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.activate` | Activate sculpting restraints | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.deactivate` | Deactivate sculpting | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.iterate` | Iterate sculpting minimization | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.purge` | Purge sculpting data | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.auto_sculpt` | Setting auto_sculpt | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_angl_weight` | Setting sculpt_angl_weight | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_auto_center` | Setting sculpt_auto_center | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_avd_excl` | Setting sculpt_avd_excl | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_avd_gap` | Setting sculpt_avd_gap | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_avd_range` | Setting sculpt_avd_range | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_avd_weight` | Setting sculpt_avd_weight | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_bond_weight` | Setting sculpt_bond_weight | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_field_mask` | Setting sculpt_field_mask | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_hb_overlap` | Setting sculpt_hb_overlap | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_hb_overlap_base` | Setting sculpt_hb_overlap_base | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_line_weight` | Setting sculpt_line_weight | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_max_max` | Setting sculpt_max_max | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_max_min` | Setting sculpt_max_min | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_max_scale` | Setting sculpt_max_scale | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_max_weight` | Setting sculpt_max_weight | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_memory` | Setting sculpt_memory | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_min_max` | Setting sculpt_min_max | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_min_min` | Setting sculpt_min_min | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_min_scale` | Setting sculpt_min_scale | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_min_weight` | Setting sculpt_min_weight | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_nb_interval` | Setting sculpt_nb_interval | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_plan_weight` | Setting sculpt_plan_weight | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_pyra_inv_weight` | Setting sculpt_pyra_inv_weight | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_pyra_weight` | Setting sculpt_pyra_weight | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_tors_tolerance` | Setting sculpt_tors_tolerance | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_tors_weight` | Setting sculpt_tors_weight | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_tri_max` | Setting sculpt_tri_max | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_tri_min` | Setting sculpt_tri_min | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_tri_mode` | Setting sculpt_tri_mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_tri_scale` | Setting sculpt_tri_scale | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_tri_weight` | Setting sculpt_tri_weight | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_vdw_scale` | Setting sculpt_vdw_scale | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_vdw_scale14` | Setting sculpt_vdw_scale14 | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_vdw_vis_max` | Setting sculpt_vdw_vis_max | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_vdw_vis_mid` | Setting sculpt_vdw_vis_mid | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_vdw_vis_min` | Setting sculpt_vdw_vis_min | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_vdw_vis_mode` | Setting sculpt_vdw_vis_mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_vdw_weight` | Setting sculpt_vdw_weight | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_vdw_weight14` | Setting sculpt_vdw_weight14 | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpting` | Setting sculpting | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpting_cycles` | Setting sculpting_cycles | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `modeling.cellulose` | Cellulose crystallite builder | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.chirality_check` | Detect and fix chirality errors | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.cis_peptide` | Detect and fix cis peptide bonds | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.coarse_grain` | Coarse-grain/all-atom conversion | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.dowser_water` | Place internal waters with Dowser | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.forcefield_toolkit` | CHARMM-compatible parameter development | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.inorganic_builder` | Inorganic structure builder | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.ionize` | Add ions for simulation | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.ionize_mead` | Ion placement for highly charged systems | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.membrane` | Lipid membrane builder | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.membrane_mixer` | Mixed membrane replica builder | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.merge_structures` | Merge structures | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.mutate_residue` | Mutate residues | `vmd-plugin-catalog-current` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.nanotube` | Carbon nanotube builder | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.parameterization` | Force-field parameterization workflow | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.psf_auto` | Automatic PSF topology building | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.psfgen` | Topology/PSF generation | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.sidechain_check` | Asn/Gln/His orientation and protonation check | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.solvate` | Solvate a structure | `vmd-plugin-catalog-current` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.structure_editor` | Interactive structure editor | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `modeling.topology_tools` | Topology manipulation toolkit | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `topology.angle.add` | Add angle term | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `topology.angle.delete` | Delete angle term | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `topology.angle.delete_all` | Delete all angle terms | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `topology.bond.delete_all` | Delete all bonds | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `topology.dihedral.add` | Add dihedral term | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `topology.dihedral.delete` | Delete dihedral term | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `topology.dihedral.delete_all` | Delete all dihedral terms | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `topology.improper.add` | Add improper term | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `topology.improper.delete` | Delete improper term | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `topology.improper.delete_all` | Delete all improper terms | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `topology.query.angles` | Query angle terms | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `topology.query.bonds` | Query bond list | `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `topology.query.dihedrals` | Query dihedral terms | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `topology.query.impropers` | Query improper terms | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `data.pdb_record_query` | Query PDB record information | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `io.fetch.async` | Asynchronous fetch | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.chain_code` | Fetch single chain by five-letter code | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.remote` | Fetch structure or map by identifier | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.type.2fofc` | Fetch type 2fofc | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.type.bcif` | Fetch type bcif | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.type.cid` | Fetch type cid | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.type.cif` | Fetch type cif | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.type.emd` | Fetch type emd | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.type.fofc` | Fetch type fofc | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.type.pdb` | Fetch type pdb | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.type.pdb1` | Fetch type pdb1 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.type.sid` | Fetch type sid | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.acnt.read` | ACNT read | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.aln.read` | ALN read | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.aln.write` | ALN write | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.bcif.read` | BCIF read | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.brix.read` | BRIX read | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.cc1.read` | CC1 read | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.ccp4.read` | CCP4 read | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.ccp4.write` | CCP4 write | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `io.format.cif.read` | CIF read | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.cif.write` | CIF write | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.cml.read` | CML read | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.crd.read` | CRD read | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.cube.read` | PyMOL load of CUBE files through the molfile plugin | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `io.format.dae.read` | DAE read | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `io.format.dcd.read` | PyMOL load of DCD files through the molfile plugin | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `io.format.dtr.read` | PyMOL load of DTR files through the molfile plugin | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `io.format.dx.read` | DX read | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.fasta.read` | FASTA read | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.fasta.write` | FASTA write | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.fld.read` | FLD read | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.g96.read` | PyMOL load of G96 files through the molfile plugin | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `io.format.grd.read` | GRD read | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.gro.read` | PyMOL load of GRO files through the molfile plugin | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `io.format.idx.read` | IDX read | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.mae.read` | MAE read | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `io.format.mae.write` | MAE write | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.map.read` | MAP read | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.map.write` | MAP write | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `io.format.mmod.read` | MMOD read | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.mmod.write` | MMOD write | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.mmtf.read` | MMTF read | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.mmtf.write` | MMTF write | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.moe.read` | MOE read | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `io.format.mol.read` | MOL read | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.mol.write` | MOL write | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.mol2.read` | MOL2 read | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.mol2.write` | MOL2 write | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.mrc.read` | MRC read | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.mrc.write` | MRC write | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `io.format.mtz.read` | MTZ read | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `io.format.p1m.read` | P1M read | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.pdb.read` | PDB read | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.pdb.write` | PDB write | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.pdbml.read` | PDBML read | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.pdbqt.read` | PDBQT read | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.phi.read` | PHI read | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.phypo.read` | PHYPO read | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `io.format.pkl.write` | PKL write | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.pkla.write` | PKLA write | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.ply.read` | PLY read | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.pml.read` | PML read | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | macos | 4 |
| `io.format.pmo.read` | PMO read | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.pqr.read` | PQR read | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.pqr.write` | PQR write | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.pwg.read` | PWG read | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.py.read` | PY read | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `io.format.r3d.read` | R3D read | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.rst.read` | RST read | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.sdf.read` | SDF read | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.sdf.write` | SDF write | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.spider.read` | PyMOL load of SPIDER files through the molfile plugin | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `io.format.stl.read` | STL read | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `io.format.top.read` | TOP read | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.trj.read` | TRJ read | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.trr.read` | PyMOL load of TRR files through the molfile plugin | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `io.format.vdb.read` | VDB read | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.vis.read` | VIS read | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `io.format.xplor.read` | XPLOR read | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.xtc.read` | PyMOL load of XTC files through the molfile plugin | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `io.format.xyz.read` | XYZ read | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.xyz.write` | XYZ write | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.geometry.dump_text` | Dump surface/map geometry as text | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.auto_detect` | Load format auto-detection | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `io.load.batch_glob` | Batch glob load | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `io.load.compression` | Compressed input | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.discrete` | Discrete multi-model load | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.dynamic_molfile_plugin` | Dynamic molfile fallback | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 3 |
| `io.load.extension_alias` | Load extension alias normalization | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `io.load.file` | Load file or URL | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.load.finish_deferred` | Deferred object finalization | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.memory` | Load in-memory content | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `io.load.multiplex` | Multiplex multi-model load | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.object_api` | Load developer object | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.object_api.brick` | ChemPy brick volume object | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.object_api.callback` | Python render callback object | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.object_api.cgo` | compiled graphics object | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.object_api.chempymap` | ChemPy map object | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.object_api.model` | ChemPy model object | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.remote_url` | Remote URL input | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.state_append` | Load into object state | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `io.load.trajectory.attach` | Attach trajectory | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.load.trajectory.image_shift` | AMBER trajectory image and shift | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.trajectory.range` | Trajectory frame range and stride | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `io.load.trajectory.selection` | Trajectory coordinate subset | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.model.export` | Export in-memory model | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.model.import` | Import in-memory model | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.mtz.load_maps` | Load MTZ as maps | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.pymol_chempy.arc` | PyMOL chempy arc module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_chempy.brick` | PyMOL chempy brick module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_chempy.cc1` | PyMOL chempy cc1 module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_chempy.cif` | PyMOL chempy cif module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_chempy.gamess1` | PyMOL chempy gamess1 module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_chempy.gms` | PyMOL chempy gms module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_chempy.io` | PyMOL chempy io module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_chempy.lst` | PyMOL chempy lst module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_chempy.mae` | PyMOL chempy mae module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_chempy.map` | PyMOL chempy map module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_chempy.mmd` | PyMOL chempy mmd module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_chempy.mol` | PyMOL chempy mol module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_chempy.mol2` | PyMOL chempy mol2 module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_chempy.pdb` | PyMOL chempy pdb module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_chempy.pkl` | PyMOL chempy pkl module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_chempy.sdf` | PyMOL chempy sdf module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_chempy.xyz` | PyMOL chempy xyz module | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_exporting.get_cifstr` | PyMOL cmd.get_cifstr | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_exporting.multifilenamegen` | PyMOL cmd.multifilenamegen | `source-audit-2026-10` | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_importing.finish_object` | PyMOL cmd.finish_object | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_importing.load_brick` | PyMOL cmd.load_brick | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_importing.load_callback` | PyMOL cmd.load_callback | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_importing.load_cgo` | PyMOL cmd.load_cgo | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_importing.load_coords` | PyMOL cmd.load_coords | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_importing.load_coordset` | PyMOL cmd.load_coordset | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_importing.load_map` | PyMOL cmd.load_map | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_importing.load_mmtf` | PyMOL cmd.load_mmtf | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_importing.load_object` | PyMOL cmd.load_object | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_importing.read_mmodstr` | PyMOL cmd.read_mmodstr | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_importing.read_mol2str` | PyMOL cmd.read_mol2str | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_importing.read_xplorstr` | PyMOL cmd.read_xplorstr | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `io.pymol_lazyio.get_mmtfstr` | PyMOL lazyio.get_mmtfstr | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `io.save.auto_detect` | Save format auto-detection | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `io.save.compression` | Compressed output | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.save.file` | Save file | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.save.multi_entry` | Append multi-entry output | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.save.multifile_template` | Object/state filename template export | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.save.selection` | Save selection | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `io.save.state.all` | Save all states | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.save.state.current` | Save current state | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.script.load_embedded` | Load embedded data block | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.assembly` | Setting assembly | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.atom_type_format` | Setting atom_type_format | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.auto_classify_atoms` | Setting auto_classify_atoms | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.auto_dss` | Setting auto_dss | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.auto_rename_duplicate_objects` | Setting auto_rename_duplicate_objects | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.chem_comp_cartn_use` | Setting chem_comp_cartn_use | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.cif_keepinmemory` | Setting cif_keepinmemory | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.cif_metalc_as_zero_order_bonds` | Setting cif_metalc_as_zero_order_bonds | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.cif_use_auth` | Setting cif_use_auth | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.connect_bonded` | Setting connect_bonded | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.connect_cutoff` | Setting connect_cutoff | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.connect_mode` | Setting connect_mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.default_2fofc_map_rep` | Setting default_2fofc_map_rep | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.default_buster_names` | Setting default_buster_names | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.default_fofc_map_rep` | Setting default_fofc_map_rep | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.default_phenix_names` | Setting default_phenix_names | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.default_phenix_no_fill_names` | Setting default_phenix_no_fill_names | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.default_refmac_names` | Setting default_refmac_names | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.fetch_host` | Setting fetch_host | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.fetch_path` | Setting fetch_path | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.fetch_type_default` | Setting fetch_type_default | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.ignore_pdb_segi` | Setting ignore_pdb_segi | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.load_atom_props_default` | Setting load_atom_props_default | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.load_object_props_default` | Setting load_object_props_default | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.moe_separate_chains` | Setting moe_separate_chains | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.multiplex` | Setting multiplex | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.normalize_ccp4_maps` | Setting normalize_ccp4_maps | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.normalize_grd_maps` | Setting normalize_grd_maps | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.normalize_o_maps` | Setting normalize_o_maps | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_conect_all` | Setting pdb_conect_all | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_conect_nodup` | Setting pdb_conect_nodup | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_discrete_chains` | Setting pdb_discrete_chains | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_echo_tags` | Setting pdb_echo_tags | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_formal_charges` | Setting pdb_formal_charges | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_hetatm_guess_valences` | Setting pdb_hetatm_guess_valences | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_hetatm_sort` | Setting pdb_hetatm_sort | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_honor_model_number` | Setting pdb_honor_model_number | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_ignore_conect` | Setting pdb_ignore_conect | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_insertions_go_first` | Setting pdb_insertions_go_first | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_insure_orthogonal` | Setting pdb_insure_orthogonal | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_literal_names` | Setting pdb_literal_names | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_no_end_record` | Setting pdb_no_end_record | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_reformat_names_mode` | Setting pdb_reformat_names_mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_retain_ids` | Setting pdb_retain_ids | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_standard_order` | Setting pdb_standard_order | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_truncate_residue_name` | Setting pdb_truncate_residue_name | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_unbond_cations` | Setting pdb_unbond_cations | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_use_ter_records` | Setting pdb_use_ter_records | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pqr_no_chain_id` | Setting pqr_no_chain_id | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.preserve_chempy_ids` | Setting preserve_chempy_ids | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.rank_assisted_sorts` | Setting rank_assisted_sorts | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.retain_order` | Setting retain_order | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.sdf_write_zero_order_bonds` | Setting sdf_write_zero_order_bonds | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.swap_dsn6_bytes` | Setting swap_dsn6_bytes | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.validate_object_names` | Setting validate_object_names | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.string.read_mol` | Read MOL text | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `io.string.read_pdb` | Read PDB text | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `io.string.read_sdf` | Read SDF text | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `io.string.write_pdb` | Write PDB text | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `session.cache.optimize` | Optimize caches before session serialization | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `session.dirty_state` | Session dirty-state tracking | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.embed.data` | Session embedded-data policy | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.export.compatibility` | Export sessions for older PyMOL versions | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `session.extension.hooks` | Extension session save and restore hooks | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.file.load.pse` | Load a PSE session file | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.file.save.pse` | Save a PSE session file | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.migration` | Session version check and migration | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `session.partial` | Partial session export and restore | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.path.current` | Current session-file path tracking | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.presentation.chain` | Chain numbered presentation sessions | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.presentation.load.psw` | Load and auto-start a PSW presentation session | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.presentation.save.psw` | Save a PSW presentation session | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.restore.memory` | Restore application state from memory | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `session.security.movie_commands` | Lock commands restored from untrusted sessions | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `session.serialization.encoding` | Session binary and compression encoding | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `session.serialize.memory` | Serialize application state in memory | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `object.copy.exact` | Exact molecular-object copy | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.copy.merge_renamed` | Copy selection into object with identifier conflict repair | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.create.copy_properties` | Copy object properties while creating from a selection | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `object.create.from_selection` | Create object or state from selection | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.delete.named` | Delete named objects or selections | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `object.edit.remove_atoms` | Remove selected atoms | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.edit.sort_atoms` | Canonical atom reorder | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.edit.uniquify_identifier` | Repair identifier collisions | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.extract.selection` | Move selected atoms into a new object | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.auto_name` | Dot-prefix automatic grouping | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.hierarchy.group.add` | Add objects to a group | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.group.auto` | Context-sensitive group add or toggle | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.hierarchy.group.close` | Collapse a group in the object panel | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.group.empty` | Empty a group without deleting members | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.group.excise` | Delete a group while retaining its members | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.group.manage` | Hierarchical group-object lifecycle | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.group.open` | Expand a group in the object panel | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.group.purge_members` | Delete all members of a group | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.group.raise` | Raise a nested group to the top level | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `object.hierarchy.group.remove` | Remove members from a group | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.group.toggle` | Toggle group expansion in the object panel | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.group.ungroup` | Return objects to the top level | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.matrix.copy` | Copy object transformation matrix | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.matrix.look_at` | Orient object toward target | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.matrix.query` | Query object matrix | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.matrix.reset` | Reset object transformation matrix | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.molecule.active` | Make molecule(s) active | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule.cancel` | Cancel loading trajectories | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule.fix` | Fix molecule(s) | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `object.molecule.free` | Unfix molecule(s) | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `object.molecule.inactive` | Make molecule(s) inactive | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule.manage` | mol command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule.reanalyze` | Re-analyze structure after bonding and atom name changes | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule.top` | Set the top molecule | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule.volume` | molecule_number < volumeset_name > < Origin > < a > < b > < c > #a #b #c < Data  | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule_info.get` | molinfo molecule_id get {list of keywords} | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule_info.index` | Returns the id of the n 'th molecule | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule_info.list` | Access and, in some cases, modify information about a given molecule | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule_info.manage` | molinfo command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule_info.num` | Returns the number of loaded molecules | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object.molecule_info.top` | Returns the id of the top molecule | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object.order.manage` | Object and selection panel ordering | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `object.order.move_bottom` | Move object-panel names to the bottom | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `object.order.move_top` | Move object-panel names to the top | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `object.order.natural_sort` | Natural-sort object-panel names | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.order.relative` | Reorder names at their current location | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `object.query.atom_count` | Selection atom count | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `object.query.covering_objects` | Objects covered by a selection | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `object.query.discrete_count` | Discrete-object count | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.query.names` | Object and selection name query | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `object.query.state_count` | Selection state count | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `object.query.type` | Named entity type query | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `object.rename` | Rename object or named selection | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `object.split.chains` | Split objects by chain | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.state.delete` | Delete coordinate states | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.state.join` | Join objects into one multi-state object | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.state.join.discrete` | Join heterogeneous objects as discrete states | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.state.join.identical_topology` | Join states assuming identical topology | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.state.join.identifier_intersection` | Join states by identifier intersection | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.state.join.sequence_alignment` | Join states by sequence alignment | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.state.split` | Split states into single-state objects | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.title.get` | Query state title | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.title.set` | Set state title | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.visibility.disable.named` | Disable names matched by a pattern | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `object.visibility.disable.selection` | Disable objects covered by an atom selection | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.visibility.enable.named` | Enable names matched by a pattern | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `object.visibility.enable.parents` | Enable an object's parent groups | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `object.visibility.enable.selection` | Enable objects covered by an atom selection | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.visibility.manage` | Object and named-selection enabled state | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `object_state.pymol_creating.map_generate` | PyMOL cmd.map_generate | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.pymol_creating.set_raw_alignment` | PyMOL cmd.set_raw_alignment | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.accession` | VMD molinfo accession | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.angles` | VMD molinfo angles | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.basis` | VMD molinfo basis | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.center` | VMD molinfo center | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.center_matrix` | VMD molinfo center_matrix | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.crossterms` | VMD molinfo crossterms | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.database` | VMD molinfo database | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.dihedrals` | VMD molinfo dihedrals | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.filename` | VMD molinfo filename | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.filespec` | VMD molinfo filespec | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.filetype` | VMD molinfo filetype | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.frame` | VMD molinfo frame | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.global_matrix` | VMD molinfo global_matrix | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.homo` | VMD molinfo homo | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.impropers` | VMD molinfo impropers | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.last` | VMD molinfo last | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.name` | VMD molinfo name | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.numatoms` | VMD molinfo numatoms | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.numavailorbs` | VMD molinfo numavailorbs | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.numframes` | VMD molinfo numframes | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.numreps` | VMD molinfo numreps | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.numvolumedata` | VMD molinfo numvolumedata | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.orbenergies` | VMD molinfo orbenergies | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.orboccupancies` | VMD molinfo orboccupancies | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.overlap_matrix` | VMD molinfo overlap_matrix | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.qmcharges` | VMD molinfo qmcharges | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.remarks` | VMD molinfo remarks | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.rotate_matrix` | VMD molinfo rotate_matrix | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.scale_matrix` | VMD molinfo scale_matrix | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.timesteps` | VMD molinfo timesteps | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.view_matrix` | VMD molinfo view_matrix | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.wavef_energy` | VMD molinfo wavef_energy | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.wavef_excitation` | VMD molinfo wavef_excitation | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.wavef_spin` | VMD molinfo wavef_spin | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.wavef_tree` | VMD molinfo wavef_tree | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.wavef_type` | VMD molinfo wavef_type | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_molinfo.wavefunction` | VMD molinfo wavefunction | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_tcl_library.restore_reps` | VMD Tcl library procedure restore_reps | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_tcl_library.restore_viewpoint` | VMD Tcl library procedure restore_viewpoint | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_tcl_library.save_atomselmacros` | VMD Tcl library procedure save_atomselmacros | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_tcl_library.save_colors` | VMD Tcl library procedure save_colors | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_tcl_library.save_display` | VMD Tcl library procedure save_display | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_tcl_library.save_labels` | VMD Tcl library procedure save_labels | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_tcl_library.save_materials` | VMD Tcl library procedure save_materials | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_tcl_library.save_reps` | VMD Tcl library procedure save_reps | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_tcl_library.save_state` | VMD Tcl library procedure save_state | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `object_state.vmd_tcl_library.save_viewpoint` | VMD Tcl library procedure save_viewpoint | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance.addrep` | molecule_number : Using the current default settings for the atom selection, col | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance.colupdate` | Update the calculated color for the specified rep each time the molecule's times | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance.delrep` | Deletes the given representation from the specified molecule | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance.drawframes` | Draw multiple trajectory frames or coordinate sets simultaneously | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance.modcolor` | Change the current coloring method for the given representation in the specified | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance.modmaterial` | Change the current material for the given representation in the specified molecu | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance.modrep` | Using the current default settings for the atom selection, coloring, and renderi | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance.modselect` | Change the current selection for the given representation in the specified molec | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance.modstyle` | Change the current rendering method (style) for the given representation in the  | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance.repindex` | Return the rep_number for the rep with the given name, or -1 if no rep with that | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance.repname` | Returns the name of the given rep | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance.scaleminmax` | molecule_number rep_number [min max \| auto] : Get/set the color scale range for  | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance.selupdate` | Update the selection for the specified rep each time the molecule's timestep cha | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance.showrep` | molecule_number rep_number [on \| off] : Get/set whether the given rep is shown o | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance.smoothrep` | molecule_number rep_number [n] : Get/set the window size for on-the-fly smoothin | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance_clip.clipplane` | mol clipplane center clipplane_id rep_number molecule_number [ vector ] | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance_clip.clipplane_color` | mol clipplane color clipplane_id rep_number molecule_number [ vector ] | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance_clip.clipplane_normal` | mol clipplane normal clipplane_id rep_number molecule_number [ vector ] | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance_clip.clipplane_status` | mol clipplane status clipplane_id rep_number molecule_number [ boolean ] | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance_default.color` | Change the default atom coloring method setting | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance_default.default` | Set the default settings for color, style, selection, or material to the supplie | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance_default.material` | Change the default material setting | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance_default.representation` | Change the default rendering method setting | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance_default.selection` | Change the default atom selection setting | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.box_draw` | Draw periodic box | `source-audit-2026-10`, `vmd-plugin-catalog-current` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.symmetry.copy` | Copy crystal symmetry | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `pbc.symmetry.expand` | Generate symmetry mates | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `pbc.symmetry.query` | Query crystal symmetry | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `pbc.symmetry.set` | Set crystal symmetry | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `pbc.unwrap` | Unwrap periodic jumps | `pymol-oss-3.1.0`, `source-audit-2026-10`, `vmd-plugin-catalog-current` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `pbc.vmd_pbctools.check_cell` | VMD pbctools pbc check_cell | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools.get` | VMD pbctools pbc get | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools.join` | VMD pbctools pbc join | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools.namd2vmd` | VMD pbctools pbc namd2vmd | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools.readxst` | VMD pbctools pbc readxst | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools.set` | VMD pbctools pbc set | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools.vmd2namd` | VMD pbctools pbc vmd2namd | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools.writexst` | VMD pbctools pbc writexst | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.alignx` | VMD pbctools option -alignx | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.all` | VMD pbctools option -all | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.bondlist` | VMD pbctools option -bondlist | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.border` | VMD pbctools option -border | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.center` | VMD pbctools option -center | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.centersel` | VMD pbctools option -centersel | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.check` | VMD pbctools option -check | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.color` | VMD pbctools option -color | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.compound` | VMD pbctools option -compound | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.compoundref` | VMD pbctools option -compoundref | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.draw` | VMD pbctools option -draw | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.first` | VMD pbctools option -first | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.last` | VMD pbctools option -last | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.material` | VMD pbctools option -material | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.molid` | VMD pbctools option -molid | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.namd` | VMD pbctools option -namd | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.noalignx` | VMD pbctools option -noalignx | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.nobondlist` | VMD pbctools option -nobondlist | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.noborder` | VMD pbctools option -noborder | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.nocheck` | VMD pbctools option -nocheck | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.nocompound` | VMD pbctools option -nocompound | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.nocompoundref` | VMD pbctools option -nocompoundref | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.nodraw` | VMD pbctools option -nodraw | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.noref` | VMD pbctools option -noref | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.noskipfirst` | VMD pbctools option -noskipfirst | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.noverbose` | VMD pbctools option -noverbose | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.now` | VMD pbctools option -now | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.off` | VMD pbctools option -off | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.on` | VMD pbctools option -on | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.orthorhombic` | VMD pbctools option -orthorhombic | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.parallelepiped` | VMD pbctools option -parallelepiped | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.rectangular` | VMD pbctools option -rectangular | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.ref` | VMD pbctools option -ref | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.resolution` | VMD pbctools option -resolution | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.sel` | VMD pbctools option -sel | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.shiftcenter` | VMD pbctools option -shiftcenter | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.shiftcenterrel` | VMD pbctools option -shiftcenterrel | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.skipfirst` | VMD pbctools option -skipfirst | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.step0` | VMD pbctools option -step0 | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.step2frame` | VMD pbctools option -step2frame | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.stride` | VMD pbctools option -stride | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.style` | VMD pbctools option -style | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.toggle` | VMD pbctools option -toggle | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.verbose` | VMD pbctools option -verbose | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.vmd` | VMD pbctools option -vmd | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.vmd_pbctools_option.width` | VMD pbctools option -width | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `pbc.wrap` | Wrap into periodic box | `pymol-oss-3.1.0`, `source-audit-2026-10`, `vmd-plugin-catalog-current` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `parallel.mpi.allgather` | Perform a parallel allgather operation across all MPI ranks, taking the user def | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `parallel.mpi.allreduce` | Perform a parallel reduction across all MPI ranks by calling the user-supplied r | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `parallel.mpi.barrier` | Perform a barrier synchronization across all MPI ranks in the running VMD job | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `parallel.mpi.for` | startcount endcount user_worker_procedure object : Invoke VMD parallel work sche | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `parallel.mpi.manage` | parallel command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `parallel.mpi.nodecount` | Return the total number of MPI ranks in the currently running VMD job | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `parallel.mpi.nodename` | Return the hostname of the current compute node | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `parallel.mpi.noderank` | Return the MPI rank of the current compute node | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `performance.setting.async_builds` | Setting async_builds | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.ati_bugs` | Setting ati_bugs | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.auto_defer_atom_count` | Setting auto_defer_atom_count | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.auto_defer_builds` | Setting auto_defer_builds | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.backface_cull` | Setting backface_cull | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.cache_display` | Setting cache_display | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.cache_max` | Setting cache_max | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.cache_memory` | Setting cache_memory | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `performance.setting.cache_mode` | Setting cache_mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.cgo_debug` | Setting cgo_debug | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.cgo_shader_ub_color` | Setting cgo_shader_ub_color | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.cgo_shader_ub_flags` | Setting cgo_shader_ub_flags | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.cgo_shader_ub_normal` | Setting cgo_shader_ub_normal | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.cgo_use_shader` | Setting cgo_use_shader | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.debug_pick` | Setting debug_pick | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.defer_builds_mode` | Setting defer_builds_mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.defer_updates` | Setting defer_updates | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.fast_idle` | Setting fast_idle | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.hash_max` | Setting hash_max | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.idle_delay` | Setting idle_delay | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.max_threads` | Setting max_threads | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.max_ups` | Setting max_ups | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.nb_spheres_use_shader` | Setting nb_spheres_use_shader | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.no_idle` | Setting no_idle | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.nonbonded_use_shader` | Setting nonbonded_use_shader | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.normal_workaround` | Setting normal_workaround | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.nvidia_bugs` | Setting nvidia_bugs | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.pick32bit` | Setting pick32bit | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.pick_shading` | Setting pick_shading | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.pickable` | Setting pickable | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.ray_hint_camera` | Setting ray_hint_camera | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.ray_triangle_fudge` | Setting ray_triangle_fudge | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.render_as_cylinders` | Setting render_as_cylinders | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.shaders_from_disk` | Setting shaders_from_disk | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.slow_idle` | Setting slow_idle | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.static_singletons` | Setting static_singletons | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.suspend_deferred` | Setting suspend_deferred | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.suspend_updates` | Setting suspend_updates | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.test1` | Setting test1 | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.test2` | Setting test2 | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.triangle_max_passes` | Setting triangle_max_passes | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.trilines` | Setting trilines | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.use_geometry_shaders` | Setting use_geometry_shaders | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.use_shaders` | Setting use_shaders | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.use_tessellation_shaders` | Setting use_tessellation_shaders | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.surface_cache` | Cached surfaces for animation | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `preset.performance_level` | Performance quality preset | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render_cache.clear` | Cache clear | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `render_cache.disable` | Cache disable | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `render_cache.enable` | Cache enable | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `render_cache.manage` | Precomputed result cache | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `render_cache.read_only` | Cache read_only | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `collaboration.biocore` | BioCoRE collaboration service | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `plugin.manager.discover_register` | Discover and register plugins | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | macos | 6 |
| `plugin.manager.install_file` | Install plugin from file | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `plugin.manager.legacy_tk` | Legacy Tk plugin host | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `plugin.manager.load` | Load plugin module | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | macos | 6 |
| `plugin.manager.menu_item` | Plugin menu item registration | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `plugin.manager.preferences` | Plugin manager preferences | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `plugin.manager.repository_fetch` | Fetch script from repository | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `plugin.manager.startup_path` | Plugin startup path | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `plugin.vmd_bundled_tool.aligntool` | VMD bundled plugin aligntool | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.bdtk` | VMD bundled plugin bdtk | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.blast` | VMD bundled plugin blast | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.bossconvert` | VMD bundled plugin bossconvert | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.cionize` | VMD bundled plugin cionize | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.clustalw` | VMD bundled plugin clustalw | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.drugui` | VMD bundled plugin drugui | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.extendedpdb` | VMD bundled plugin extendedpdb | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.fmtool` | VMD bundled plugin fmtool | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.idatm` | VMD bundled plugin idatm | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.jmvexport` | VMD bundled plugin jmvexport | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.mafft` | VMD bundled plugin mafft | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.moltoptools` | VMD bundled plugin moltoptools | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.multiseqdialog` | VMD bundled plugin multiseqdialog | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.nlenergy` | VMD bundled plugin nlenergy | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.phylotree` | VMD bundled plugin phylotree | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.psipred` | VMD bundled plugin psipred | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.readcharmmpar` | VMD bundled plugin readcharmmpar | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.readcharmmtop` | VMD bundled plugin readcharmmtop | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.ruler` | VMD bundled plugin ruler | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.runante` | VMD bundled plugin runante | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.runsqm` | VMD bundled plugin runsqm | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.seqedit` | VMD bundled plugin seqedit | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.simpleedit` | VMD bundled plugin simpleedit | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.stamp` | VMD bundled plugin stamp | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.stingtool` | VMD bundled plugin stingtool | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.structurecheck` | VMD bundled plugin structurecheck | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.torsionplot` | VMD bundled plugin torsionplot | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.trunctraj` | VMD bundled plugin trunctraj | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.vmd_bundled_tool.volutil` | VMD bundled plugin volutil | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `plugin.wizard.launch` | Launch wizard | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `plugin.wizard.refresh` | Refresh wizard | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `plugin.wizard.replace` | Replace wizard | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.background.gradient_colors` | Background top/bottom colors | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.background.gradient_mode` | Background gradient/grid mode | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.background.interactive_alpha` | Interactive/snapshot background alpha | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `color.background.ray_alpha` | Ray background alpha override | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `color.background.ray_alpha.mode.inherit` | Ray Opaque Background mode inherit | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.background.ray_alpha.mode.opaque` | Ray Opaque Background mode opaque | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.background.ray_alpha.mode.transparent` | Ray Opaque Background mode transparent | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.background.solid` | Solid background color | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.space.cmyk` | Color space cmyk | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.space.manage` | Display color space | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.space.pymol` | Color space pymol | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.space.rgb` | Color space rgb | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.image.dimension_units` | Physical image dimensions | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.dimension_units.mode.centimeters` | Image dimensions in centimeters | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.dimension_units.mode.inches` | Image dimensions in inches | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.dimension_units.mode.millimeters` | Image dimensions in millimeters | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.dimension_units.mode.pixels` | Image dimensions in pixels | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.filename_extension` | Image filename extension policy | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.format` | Image encoding selection | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.format.mode.guess` | Image format guess | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.format.mode.png` | Image format png | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.format.mode.ppm` | Image format ppm | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `export.image.memory_buffer` | Return encoded image buffer | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `export.image.prior_buffer` | Prior image-buffer policy | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.image.prior_buffer.mode.render_current` | Image prior-buffer mode render_current | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.image.prior_buffer.mode.require_prior` | Image prior-buffer mode require_prior | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.image.prior_buffer.mode.try_prior` | Image prior-buffer mode try_prior | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.image.source` | Image render-source selection | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `export.image.source.mode.ray` | Image source ray | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.source.mode.viewport_or_draw` | Image source viewport_or_draw | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.write` | Write rendered image | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `export.movie.camera_flight` | Camera flight movie | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.movie.frame_sequence` | Export movie frame sequence | `pymol-oss-3.1.0`, `vmd-plugin-catalog-current` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.movie.frame_sequence.dimensions` | Frame-sequence dimensions | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.frame_sequence.format` | Frame-sequence image format | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.frame_sequence.format.mode.png` | Frame-sequence format png | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.frame_sequence.format.mode.ppm` | Frame-sequence format ppm | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.frame_sequence.modal` | Frame-sequence modal execution | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.frame_sequence.preserve` | Frame-sequence preserve policy | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.frame_sequence.range` | Frame-sequence inclusive range | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.movie.frame_sequence.render_mode` | Frame-sequence render mode | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.movie.frame_sequence.render_mode.mode.default_setting` | Frame-sequence render mode default_setting | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.frame_sequence.render_mode.mode.draw` | Frame-sequence render mode draw | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.frame_sequence.render_mode.mode.normal` | Frame-sequence render mode normal | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.frame_sequence.render_mode.mode.ray` | Frame-sequence render mode ray | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.mode_selector` | Legacy/Timeline movie export selector | `pymol-incentive-3.1.8` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.movie.overlay_option` | Include or exclude overlays in movie export | `pymol-incentive-3.1.8` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.movie.video` | Encode movie video | `pymol-oss-3.1.0`, `vmd-plugin-catalog-current` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.encoder` | Movie encoder selection | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.encoder.mode.convert` | Movie encoder convert | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.encoder.mode.ffmpeg` | Movie encoder ffmpeg | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.encoder.mode.mpeg_encode` | Movie encoder mpeg_encode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.even_dimensions` | Movie even-dimension normalization | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.format` | Movie output format | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.format.mode.gif` | Movie format gif | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.format.mode.mov` | Movie format mov | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.format.mode.mp4` | Movie format mp4 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.format.mode.mpeg1` | Movie format mpeg1 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.format.mode.webm` | Movie format webm | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.frame_rate` | Movie frame rate | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.quality` | Movie encoding quality | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.temp_frames` | Movie temporary-frame lifecycle | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.render.aasamples` | Query or set the number of antialiasing samples to be used by this renderer, if  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.aosamples` | Query or set the number of ambient occlusion lighting samples to be used by this | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.art` | render ART | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.command_get` | method : Get the default command string | `vmd-1.9.3` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.command_set` | method command : Set new default command | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.default` | method : Get the original default command | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.formats` | List a renderer's available image output formats/modes | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.gelato` | Gelato | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.hasaa` | Query whether or not a renderer has controllable antialiasing feature | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.list` | List the available rendering methods | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.manage` | render command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.output_format` | Set a renderer's active image output format/mode | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.postscript` | PostScript | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.pov3` | POV3 | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.radiance` | Radiance | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.raster3d` | Raster3D | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.rayshade` | Rayshade | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.render_file` | Render the global scene to filename using method and execute the default command | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.render_file_command` | Render the global scene to filename , then execute ` command ' | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.renderman` | Renderman | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.snapshot` | snapshot | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.stl` | render STL | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.tachyon` | Tachyon | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.tachyoninternal` | TachyonInternal | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.vrml` | VRML-1 | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.vrml_2` | VRML-2 | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.render.wavefront` | Wavefront | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `export.scene.api.string` | Scene serialization string API | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.compile_feature_negotiation` | Scene exporter compile-feature negotiation | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.dispatch` | Scene export dispatch | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.external.gltf_conversion` | External COLLADA-to-glTF conversion | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.format` | Scene geometry format | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.format.mode.collada` | Scene format collada | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.format.mode.gltf` | Scene format gltf | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.format.mode.idtf` | Scene format idtf | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.format.mode.mtl` | Scene format mtl | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.format.mode.obj` | Scene format obj | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.format.mode.povray` | Scene format povray | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.format.mode.stl` | Scene format stl | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `export.scene.format.mode.vrml1` | Scene format vrml1 | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.format.mode.vrml2` | Scene format vrml2 | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `import.image.png.display` | Display PNG image | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `preset.modernize_rendering` | Modern rendering preset | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ambient_occlusion.manage` | Ambient-occlusion state | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ambient_occlusion.setting.ambient_occlusion_mode` | Ambient Occlusion Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ambient_occlusion.setting.ambient_occlusion_mode.mode.atom_map` | Ambient Occlusion Mode mode atom_map | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ambient_occlusion.setting.ambient_occlusion_mode.mode.disabled` | Ambient Occlusion Mode mode disabled | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ambient_occlusion.setting.ambient_occlusion_mode.mode.per_atom` | Ambient Occlusion Mode mode per_atom | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ambient_occlusion.setting.ambient_occlusion_mode.mode.vertex_map` | Ambient Occlusion Mode mode vertex_map | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ambient_occlusion.setting.ambient_occlusion_scale` | Ambient Occlusion Scale | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ambient_occlusion.setting.ambient_occlusion_smooth` | Ambient Occlusion Smooth | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.antialias.manage` | Antialias and color-correction state | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.antialias.setting.antialias` | Antialias | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.antialias.setting.antialias.mode.adaptive` | Antialias mode adaptive | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.antialias.setting.antialias.mode.none` | Antialias mode none | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.antialias.setting.antialias.mode.uniform_2x_adaptive` | Antialias mode uniform_2x_adaptive | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.antialias.setting.antialias.mode.uniform_3x_adaptive` | Antialias mode uniform_3x_adaptive | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.antialias.setting.antialias.mode.uniform_4x_adaptive` | Antialias mode uniform_4x_adaptive | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.antialias.setting.antialias_shader` | Antialias Shader | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.clipboard.copy` | Copy image to clipboard | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.color_correction.setting.gamma` | Gamma | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.auto_copy_images` | Auto Copy Images | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.batch_prefix` | Batch Prefix | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.bg_image_filename` | Bg Image Filename | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.bg_image_linear` | Bg Image Linear | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.bg_image_mode` | Bg Image Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.bg_image_mode.mode.center` | Bg Image Mode mode center | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.bg_image_mode.mode.fit` | Bg Image Mode mode fit | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.bg_image_mode.mode.stretch` | Bg Image Mode mode stretch | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.bg_image_mode.mode.tile` | Bg Image Mode mode tile | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.bg_image_tilesize` | Bg Image Tilesize | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.cache_frames` | Cache Frames | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.collada_background_box` | Collada Background Box | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.collada_export_lighting` | Collada Export Lighting | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.collada_geometry_mode` | Collada Geometry Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.collada_geometry_mode.mode.blender_polylist` | Collada Geometry Mode mode blender_polylist | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.collada_geometry_mode.mode.standard_141` | Collada Geometry Mode mode standard_141 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.draw_frames` | Draw Frames | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.geometry_export_mode` | Geometry Export Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.geometry_export_mode.mode.full_scene` | Geometry Export Mode mode full_scene | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.geometry_export_mode.mode.geometry_material_only` | Geometry Export Mode mode geometry_material_only | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.image_copy_always` | Image Copy Always | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.image_dots_per_inch` | Image Dots Per Inch | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.movie_fps` | Movie Fps | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.movie_quality` | Movie Quality | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.png_file_gamma` | Png File Gamma | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.png_screen_gamma` | Png Screen Gamma | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.ray_default_renderer` | Ray Default Renderer | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.ray_default_renderer.mode.builtin` | Ray Default Renderer mode builtin | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.ray_default_renderer.mode.default` | Ray Default Renderer mode default | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.ray_default_renderer.mode.povray` | Ray Default Renderer mode povray | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.ray_orthoscopic` | Ray Orthoscopic | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.ray_orthoscopic.mode.inherit` | Ray Orthoscopic mode inherit | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.ray_orthoscopic.mode.orthographic` | Ray Orthoscopic mode orthographic | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.ray_orthoscopic.mode.perspective` | Ray Orthoscopic mode perspective | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.ray_trace_frames` | Ray Trace Frames | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.show_alpha_checker` | Show Alpha Checker | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.single_image` | Single Image | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.fog.manage` | Fog and depth-cue state | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.fog.setting.depth_cue` | Depth Cue | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.fog.setting.fog` | Fog | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.fog.setting.fog_start` | Fog Start | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.fog.setting.ray_trace_fog` | Ray Trace Fog | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.fog.setting.ray_trace_fog_start` | Ray Trace Fog Start | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.image.capture.window` | Capture complete application window | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `render.image.draw.aspect_preserving_size` | Draw aspect-preserving dimensions | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `render.image.draw.opengl` | Draw OpenGL image | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.image.draw.requires_graphics_context` | Draw graphics-context requirement | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.image.focal_blur` | Focal blur image | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.image.ray` | Ray-traced image | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.image.ray.antialias` | Ray antialias override | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.image.ray.aspect_preserving_size` | Ray aspect-preserving dimensions | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `render.image.ray.execution.async` | Asynchronous ray execution | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.image.ray.execution.synchronous` | Synchronous ray execution | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `render.image.ray.renderer` | Ray renderer selection | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.image.ray.renderer.mode.builtin` | Ray renderer builtin | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `render.image.ray.renderer.mode.default_setting` | Ray renderer default_setting | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.image.ray.renderer.mode.geometry_count` | Ray renderer geometry_count | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.image.ray.renderer.mode.povray` | Ray renderer povray | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.image.ray.stereo_pair` | Ray stereo angle and shift | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.image.ray.stops_animation` | Ray animation stop policy | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.light.disable` | Turn a light off | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `render.light.enable` | Turn a light on | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `render.light.manage` | light command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `render.light.num` | Return the number of lights available | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `render.light.position_default` | Return default position | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `render.light.position_get` | Return current position | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `render.light.position_set` | Set light position | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `render.light.rotate` | Rotate a light (at infinity) angle degrees about a given axis | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `render.light.status` | Return the pair on/off highlight/unhighlight | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `render.lighting.manage` | Shared lighting state | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.lighting.panel` | Interactive lighting settings panel | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.lighting.ray_shadow_preset.black` | Ray-shadow preset black | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.ray_shadow_preset.heavy` | Ray-shadow preset heavy | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.ray_shadow_preset.light` | Ray-shadow preset light | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.ray_shadow_preset.manage` | Ray-shadow utility presets | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.ray_shadow_preset.matte` | Ray-shadow preset matte | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.ray_shadow_preset.medium` | Ray-shadow preset medium | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.ray_shadow_preset.none` | Ray-shadow preset none | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.ray_shadow_preset.occlusion` | Ray-shadow preset occlusion | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.ray_shadow_preset.occlusion2` | Ray-shadow preset occlusion2 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.ray_shadow_preset.soft` | Ray-shadow preset soft | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.ambient` | Ambient | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.lighting.setting.light_1` | Light | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.lighting.setting.light_2` | Light2 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.light_3` | Light3 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.light_4` | Light4 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.light_5` | Light5 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.light_6` | Light6 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.light_7` | Light7 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.light_8` | Light8 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.light_9` | Light9 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.light_count` | Light Count | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.lighting.setting.light_count.mode.extended_precomputed_or_ray` | Light count branch extended_precomputed_or_ray | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.light_count.mode.interactive_direct` | Light count branch interactive_direct | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.precomputed_lighting` | Precomputed Lighting | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.two_sided_lighting` | Two Sided Lighting | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.lighting.setting.two_sided_lighting.mode.auto` | Two Sided Lighting mode auto | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.two_sided_lighting.mode.both_faces` | Two Sided Lighting mode both_faces | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.two_sided_lighting.mode.front_only` | Two Sided Lighting mode front_only | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.manage` | Material response state | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.material.preset.default` | Material preset default | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.preset.manage` | Material preset lifecycle | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.preset.metal` | Material preset metal | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.preset.plastic` | Material preset plastic | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.preset.rubber` | Material preset rubber | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.preset.xray` | Material preset xray | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.direct` | Direct | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.material.setting.power` | Power | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.ray_direct_shade` | Ray Direct Shade | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.ray_label_specular` | Ray Label Specular | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.ray_legacy_lighting` | Ray Legacy Lighting | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.ray_spec_local` | Ray Spec Local | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.reflect` | Reflect | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.material.setting.reflect_power` | Reflect Power | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.shininess` | Shininess | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.spec_count` | Spec Count | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.spec_direct` | Spec Direct | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.spec_direct_power` | Spec Direct Power | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.spec_power` | Spec Power | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.spec_reflect` | Spec Reflect | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.specular` | Specular | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.specular_intensity` | Specular Intensity | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material_library.add` | name : create a new material with the given name | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `render.material_library.ambient` | ambient | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `render.material_library.copy` | name : create a new material copied from the selected material name | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `render.material_library.delete` | name : Delete the given material | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `render.material_library.diffuse` | diffuse | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `render.material_library.manage` | material command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `render.material_library.mirror` | mirror | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `render.material_library.opacity` | opacity | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `render.material_library.rename` | oldname newname : rename the given material | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `render.material_library.settings` | name : Return a list of the five material settings for the material of the given | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `render.ray.interior.manage` | Ray interior material state | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_color` | Ray Interior Color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_mode` | Ray Interior Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_mode.mode.ignore_triangle_interiors` | Ray Interior Mode mode ignore_triangle_interiors | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_mode.mode.normal` | Ray Interior Mode mode normal | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_reflect` | Ray Interior Reflect | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_texture` | Ray Interior Texture | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_texture.mode.inherit` | Ray Interior Texture mode inherit | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_texture.mode.texture_0` | Ray Interior Texture mode texture_0 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_texture.mode.texture_1` | Ray Interior Texture mode texture_1 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_texture.mode.texture_2` | Ray Interior Texture mode texture_2 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_texture.mode.texture_3` | Ray Interior Texture mode texture_3 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_texture.mode.texture_4` | Ray Interior Texture mode texture_4 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_texture.mode.texture_5` | Ray Interior Texture mode texture_5 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.setting.volume` | Ray Volume | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.manage` | Ray style and texture state | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_blend_blue` | Ray Blend Blue | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_blend_colors` | Ray Blend Colors | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_blend_green` | Ray Blend Green | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_blend_red` | Ray Blend Red | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_color_ramps` | Ray Color Ramps | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_oversample_cutoff` | Ray Oversample Cutoff | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_pixel_scale` | Ray Pixel Scale | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_texture` | Ray Texture | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_texture.mode.texture_0` | Ray Texture mode texture_0 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_texture.mode.texture_1` | Ray Texture mode texture_1 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_texture.mode.texture_2` | Ray Texture mode texture_2 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_texture.mode.texture_3` | Ray Texture mode texture_3 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_texture.mode.texture_4` | Ray Texture mode texture_4 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_texture.mode.texture_5` | Ray Texture mode texture_5 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_texture_settings` | Ray Texture Settings | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_color` | Ray Trace Color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_depth_factor` | Ray Trace Depth Factor | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_disco_factor` | Ray Trace Disco Factor | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_gain` | Ray Trace Gain | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_mode` | Ray Trace Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_mode.mode.color_outline` | Ray Trace Mode mode color_outline | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_mode.mode.monochrome_outline` | Ray Trace Mode mode monochrome_outline | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_mode.mode.normal` | Ray Trace Mode mode normal | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_mode.mode.quantized_outline` | Ray Trace Mode mode quantized_outline | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_slope_factor` | Ray Trace Slope Factor | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.shadow.manage` | Ray shadow state | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.shadow.setting.ray_clip_shadows` | Ray Clip Shadows | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.shadow.setting.ray_hint_shadow` | Ray Hint Shadow | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.shadow.setting.ray_improve_shadows` | Ray Improve Shadows | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.shadow.setting.ray_interior_shadows` | Ray Interior Shadows | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.shadow.setting.ray_shadow` | Ray Shadow | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.shadow.setting.ray_shadow_decay_factor` | Ray Shadow Decay Factor | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.shadow.setting.ray_shadow_decay_range` | Ray Shadow Decay Range | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.shadow.setting.ray_shadow_fudge` | Ray Shadow Fudge | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.manage` | Transparency compositing state | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.transparency.setting.ray_max_passes` | Ray Max Passes | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.ray_scatter` | Ray Scatter | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.ray_trace_persist_cutoff` | Ray Trace Persist Cutoff | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.ray_trace_trans_cutoff` | Ray Trace Trans Cutoff | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.ray_transparency_contrast` | Ray Transparency Contrast | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.ray_transparency_oblique` | Ray Transparency Oblique | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.ray_transparency_oblique_power` | Ray Transparency Oblique Power | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.ray_transparency_shadows` | Ray Transparency Shadows | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.ray_transparency_spec_cut` | Ray Transparency Spec Cut | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.ray_transparency_specular` | Ray Transparency Specular | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.transparency_global_sort` | Transparency Global Sort | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.transparency_mode` | Transparency Mode | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.transparency.setting.transparency_mode.mode.fast_sorted` | Transparency Mode mode fast_sorted | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.transparency_mode.mode.multi_layer` | Transparency Mode mode multi_layer | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.transparency_mode.mode.single_layer` | Transparency Mode mode single_layer | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.transparency_mode.mode.weighted_blended_oit` | Transparency Mode mode weighted_blended_oit | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.transparency_picking_mode` | Transparency Picking Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.transparency_picking_mode.mode.auto` | Transparency Picking Mode mode auto | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.transparency_picking_mode.mode.pickable` | Transparency Picking Mode mode pickable | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.viewport.refresh` | Refresh interactive viewport | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `rendering.pymol_area.vector_font` | PyMOL stroke vector font for CGO text | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `rendering.pymol_cgo.cyl_text` | PyMOL cgo.cyl_text | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `rendering.pymol_cgo.from_plystr` | PyMOL cgo.from_plystr | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `rendering.pymol_cgo.from_r3d` | PyMOL cgo.from_r3d | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `rendering.pymol_cgo.measure_text` | PyMOL cgo.measure_text | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `rendering.pymol_cgo.molauto` | PyMOL cgo.molauto | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `rendering.pymol_cgo.torus` | PyMOL cgo.torus | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `rendering.pymol_cgo.wire_text` | PyMOL cgo.wire_text | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `rendering.vmd_graphics.colour` | VMD graphics colour | `source-audit-2026-10` | P2 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `rendering.vmd_graphics.pickpoint` | VMD graphics pickpoint | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `rendering.vmd_graphics.radius2` | VMD graphics radius2 | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `rendering.vmd_graphics.top` | VMD graphics top | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `rendering.vmd_material.outline` | VMD material outline | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `rendering.vmd_material.outlinewidth` | VMD material outlinewidth | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `rendering.vmd_material.transmode` | VMD material transmode | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.surface.setting.transparency` | Transparency | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 3 |
| `setting.service.bond.get` | Get per-bond setting | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `setting.service.bond.set` | Set per-bond setting | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `setting.service.bond.unset` | Unset per-bond setting | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `setting.service.get` | Get setting value | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `setting.service.manage` | Setting service | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `setting.service.scope.atom` | Atom setting scope | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `setting.service.scope.global` | Global setting scope | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `setting.service.scope.object` | Object setting scope | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `setting.service.scope.state` | State setting scope | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `setting.service.set` | Set setting value | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `setting.service.unset` | Unset setting value | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `setting.service.unset_deep` | Deep unset of scoped settings | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `annotation.label_chains` | Label chains | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `annotation.label_segments` | Label segments | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.assignment.atom_selection` | Atom-selection color assignment | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 7 |
| `color.assignment.cache_invalidation` | Color cache invalidation | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.assignment.deep_reset` | Deep color reset and assignment | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.assignment.manage` | Color assignment | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 7 |
| `color.assignment.new_object_auto` | Automatic new-object carbon color | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 7 |
| `color.assignment.object_api` | Explicit object-color API | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.assignment.object_pattern` | Object-pattern color assignment | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 7 |
| `color.assignment.object_query` | Object-color query | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.category.category_add` | Adds colors for the named color category, item name, using the colorname color | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.category.category_get` | Get the color of the object specified by category and name  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.category.category_set` | Set the color of the object specified by category and name to color  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.category.change` | Reset rgb of color to default value | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.category.manage` | color command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.category.restype_assign` | Set the residue type for resname to restype  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.desaturate` | Desaturate colors | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.info.categories_list` | returns a list of available categories | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.info.colorinfo` | returns the information about the color scales | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.info.colorinfo_category` | returns a list of names for the given category | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.info.colorinfo_max` | returns the total number of colors available (1057) | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.info.colorinfo_num` | returns the number of base solid colors (33) | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.info.manage` | colorinfo command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.input.hex_rgb` | Explicit hexadecimal RGB | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.input.named` | Named color lookup | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 6 |
| `color.input.numeric_index` | Numeric color-index lookup | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.input.resolve` | Color token resolution | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 6 |
| `color.input.special.atomic` | Special color: atomic | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.input.special.auto` | Special color: auto | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.input.special.back` | Special color: back | `pymol-oss-3.1.0` | P1 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `color.input.special.current` | Special color: current | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.input.special.default` | Special color: default | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.input.special.front` | Special color: front | `pymol-oss-3.1.0` | P1 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `color.input.special.object` | Special color: object | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.method.backbone` | Backbone | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.charge` | Charge | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.conformation` | Conformation | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.fragment` | Fragment | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.mass` | Mass | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.name` | Name | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.physicaltime` | PhysicalTime | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.pos` | Pos | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.posx` | PosX | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.posy` | PosY | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.posz` | PosZ | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.resname` | ResName | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.restype` | ResType | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.segname` | SegName | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.throb` | Throb | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.timestep` | Timestep | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.type` | Type | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.user` | User | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.user2` | User2 | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.user3` | User3 | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.user4` | User4 | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.method.velocity` | Velocity | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.preset.atomic_carbon` | Element colors with chosen carbon color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbab` | util.cbab (slate carbons) | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbac` | util.cbac (cyan carbons) | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbag` | util.cbag (carbon carbons) | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbak` | util.cbak (pink carbons) | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbam` | util.cbam (lightmagenta carbons) | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbao` | util.cbao (brightorange carbons) | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbap` | util.cbap (purple carbons) | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbas` | util.cbas (salmon carbons) | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbaw` | util.cbaw (hydrogen carbons) | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbay` | util.cbay (yellow carbons) | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_hydrogen` | Element colors with chosen hydrogen color | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_non_carbon` | Element colors for non-carbons | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.by_surface_area` | Color by surface area | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.chain_distinct` | Distinct color per chain | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.chainbow` | Rainbow within each chain | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.legacy_rainbow` | Legacy rainbow coloring | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.object_distinct` | Distinct color per object | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.secondary_structure` | Color by secondary structure | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.ramp.automatic_range` | Automatic ramp range | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.ramp.builtin_palette` | Built-in ramp spectra | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `color.ramp.explicit_colors` | Explicit ramp colors and levels | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `color.ramp.export_samples` | Ramp sample export | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.ramp.manage` | Spatial color-ramp object | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `color.ramp.map` | Map-value color ramp | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `color.ramp.proximity` | Molecular-proximity color ramp | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.ramp.recursive_special_colors` | Recursive/special ramp colors | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.ramp.state_scope` | Ramp source-state scope | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.ramp.update` | Ramp range/color update | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `color.scale.max` | Set maximum of color scale to x , in the range 0  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.scale.method` | Set type of scale to use for coloring objects by values | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.scale.method.bgr` | BGR - Blue to green to red | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.scale.method.blkw` | BlkW - Black to white | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.scale.method.bwg` | BWG - Blue to white to green | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.scale.method.bwr` | BWR - Blue to white to red | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.scale.method.gwb` | GWB - Green to white to blue | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.scale.method.gwr` | GWR - Green to white to red | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.scale.method.rgb` | RGB - Red to green to blue | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.scale.method.rwb` | RWB - Red to white to blue | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.scale.method.rwg` | RWG - Red to white to green | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.scale.method.wblk` | WBlk - White to black | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.scale.midpoint` | Set midpoint of color scale to x , in the range 0  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.scale.min` | Set minimum of color scale to x , in the range 0  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `color.spectrum.arbitrary_expression` | Arbitrary expression spectrum | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 5 |
| `color.spectrum.builtin_palette` | Built-in spectrum palettes | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.spectrum.builtin_property` | Built-in numeric property spectrum | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 7 |
| `color.spectrum.by_residue` | Per-residue spectrum coloring | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.spectrum.explicit_colors` | Explicit spectrum color list | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.spectrum.interpolation` | Spectrum interpolation spaces | `pymol-oss-3.1.0` | P1 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.spectrum.manage` | Property spectrum coloring | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.spectrum.nonnumeric_enumeration` | Nonnumeric spectrum enumeration | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 5 |
| `color.spectrum.range` | Spectrum range and clamping | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.table.builtin_catalog` | Built-in color catalog | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.table.define` | Custom color definition | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.table.define.fixed_mode` | Fixed custom color mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.table.define.replace` | Custom color replacement | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.table.define.rgb_255` | 8-bit RGB definition | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.table.define.rgb_normalized` | Normalized RGB definition | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.table.lut_clamp` | Color-table LUT clamping | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.table.query.index` | Color index query | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.table.query.indices` | Custom/all color index query | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.table.query.rgb` | Color RGB tuple query | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.table.session_persistence` | Color-table session persistence | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `display.roving.setting.roving_byres` | Setting roving_byres | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_cartoon` | Setting roving_cartoon | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_delay` | Setting roving_delay | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_detail` | Setting roving_detail | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_isomesh` | Setting roving_isomesh | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_isosurface` | Setting roving_isosurface | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_labels` | Setting roving_labels | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_lines` | Setting roving_lines | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_map1_level` | Setting roving_map1_level | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_map1_name` | Setting roving_map1_name | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_map2_level` | Setting roving_map2_level | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_map2_name` | Setting roving_map2_name | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_map3_level` | Setting roving_map3_level | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_map3_name` | Setting roving_map3_name | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_nb_spheres` | Setting roving_nb_spheres | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_nonbonded` | Setting roving_nonbonded | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_origin` | Setting roving_origin | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_origin_z` | Setting roving_origin_z | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_origin_z_cushion` | Setting roving_origin_z_cushion | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_polar_contacts` | Setting roving_polar_contacts | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_polar_cutoff` | Setting roving_polar_cutoff | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_ribbon` | Setting roving_ribbon | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_selection` | Setting roving_selection | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_spheres` | Setting roving_spheres | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_sticks` | Setting roving_sticks | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.atom.dots` | PyMOL-compatible dots representation | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.atom.ellipsoid` | PyMOL-compatible ellipsoids representation | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.atom.nonbonded` | PyMOL-compatible nonbonded representation | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.atom.nonbonded_spheres` | PyMOL-compatible nb_spheres representation | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.atom.spheres` | PyMOL-compatible spheres representation | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.atom.spheres.backend.shader` | Sphere shader path | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.atom.spheres.color_override` | Sphere color override | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.atom.spheres.mode` | Sphere rendering mode | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.atom.spheres.mode.auto` | Automatic shader sphere mode | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.atom.spheres.mode.cube` | Cube sphere mode | `pymol-oss-3.1.0` | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.point_fixed_normal` | Fixed point spheres with normals | `pymol-oss-3.1.0` | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.point_fixed_square` | Fixed square point spheres | `pymol-oss-3.1.0` | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.point_scaled_round` | Scaled round point spheres | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.point_scaled_round_normal` | Scaled round point spheres with normals | `pymol-oss-3.1.0` | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.point_scaled_square` | Scaled square point spheres | `pymol-oss-3.1.0` | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.point_scaled_square_normal` | Scaled square point spheres with normals | `pymol-oss-3.1.0` | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.removed_4` | Removed sphere mode 4 fallback | `pymol-oss-3.1.0` | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.removed_5` | Removed sphere mode 5 fallback | `pymol-oss-3.1.0` | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.shader_impostor` | GLSL impostor sphere mode | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.atom.spheres.mode.tetrahedron` | Tetrahedron sphere mode | `pymol-oss-3.1.0` | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.triangles` | Triangle sphere mode | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.atom.spheres.point_max_size` | Maximum point-sphere size | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.point_size` | Fixed point-sphere size | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.quality` | Sphere tessellation quality | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.scale` | Sphere VDW radius scale | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.atom.spheres.solvent_radius` | Solvent-expanded sphere radius | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.transparency` | Sphere transparency | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.backbone.cartoon` | PyMOL-compatible cartoon representation | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.backbone.cartoon.setting.all_alt` | Cartoon All Alt | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.color` | Cartoon Color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.cylindrical_helices` | Cartoon Cylindrical Helices | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.cylindrical_helices.mode.curved` | Cartoon Cylindrical Helices mode curved | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.cylindrical_helices.mode.off` | Cartoon Cylindrical Helices mode off | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.cylindrical_helices.mode.straight` | Cartoon Cylindrical Helices mode straight | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.debug` | Cartoon Debug | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.discrete_colors` | Cartoon Discrete Colors | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.dumbbell_length` | Cartoon Dumbbell Length | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.dumbbell_radius` | Cartoon Dumbbell Radius | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.dumbbell_width` | Cartoon Dumbbell Width | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.fancy_helices` | Cartoon Fancy Helices | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.fancy_sheets` | Cartoon Fancy Sheets | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.flat_cycles` | Cartoon Flat Cycles | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.flat_sheets` | Cartoon Flat Sheets | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.gap_cutoff` | Cartoon Gap Cutoff | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.backbone.cartoon.setting.helix_radius` | Cartoon Helix Radius | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.highlight_color` | Cartoon Highlight Color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ladder_color` | Cartoon Ladder Color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ladder_mode` | Cartoon Ladder Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ladder_mode.mode.off` | Cartoon Ladder Mode mode off | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ladder_mode.mode.on` | Cartoon Ladder Mode mode on | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ladder_radius` | Cartoon Ladder Radius | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.loop_cap` | Cartoon Loop Cap | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.loop_cap.mode.enum_flat` | Cartoon Loop Cap mode enum_flat | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.loop_cap.mode.enum_round` | Cartoon Loop Cap mode enum_round | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.loop_cap.mode.none` | Cartoon Loop Cap mode none | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.loop_quality` | Cartoon Loop Quality | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.loop_radius` | Cartoon Loop Radius | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_as_cylinders` | Cartoon Nucleic Acid As Cylinders | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_as_cylinders.mode.off` | Cartoon Nucleic Acid As Cylinders mode off | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_as_cylinders.mode.optimization` | Cartoon Nucleic Acid As Cylinders mode optimization | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_as_cylinders.mode.optimization_and_strands` | Cartoon Nucleic Acid As Cylinders mode optimization_and_strands | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_as_cylinders.mode.strand_geometry` | Cartoon Nucleic Acid As Cylinders mode strand_geometry | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_color` | Cartoon Nucleic Acid Color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_mode` | Cartoon Nucleic Acid Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_mode.mode.extend_3prime` | Cartoon Nucleic Acid Mode mode extend_3prime | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_mode.mode.extend_5prime` | Cartoon Nucleic Acid Mode mode extend_5prime | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_mode.mode.extend_both` | Cartoon Nucleic Acid Mode mode extend_both | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_mode.mode.phosphorus` | Cartoon Nucleic Acid Mode mode phosphorus | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_mode.mode.sugar` | Cartoon Nucleic Acid Mode mode sugar | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.oval_length` | Cartoon Oval Length | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.oval_quality` | Cartoon Oval Quality | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.oval_width` | Cartoon Oval Width | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.power` | Cartoon Power | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.power_b` | Cartoon Power B | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_quality` | Cartoon Putty Quality | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_radius` | Cartoon Putty Radius | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_range` | Cartoon Putty Range | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_scale_max` | Cartoon Putty Scale Max | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_scale_min` | Cartoon Putty Scale Min | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_scale_power` | Cartoon Putty Scale Power | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform` | Cartoon Putty Transform | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform.mode.absolute_linear` | Cartoon Putty Transform mode absolute_linear | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform.mode.absolute_nonlinear` | Cartoon Putty Transform mode absolute_nonlinear | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform.mode.implied_rms` | Cartoon Putty Transform mode implied_rms | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform.mode.normalized_linear` | Cartoon Putty Transform mode normalized_linear | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform.mode.normalized_nonlinear` | Cartoon Putty Transform mode normalized_nonlinear | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform.mode.relative_linear` | Cartoon Putty Transform mode relative_linear | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform.mode.relative_nonlinear` | Cartoon Putty Transform mode relative_nonlinear | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform.mode.scaled_linear` | Cartoon Putty Transform mode scaled_linear | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform.mode.scaled_nonlinear` | Cartoon Putty Transform mode scaled_nonlinear | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.rect_length` | Cartoon Rect Length | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.backbone.cartoon.setting.rect_width` | Cartoon Rect Width | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.backbone.cartoon.setting.refine` | Cartoon Refine | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.refine_normals` | Cartoon Refine Normals | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.refine_tips` | Cartoon Refine Tips | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_color` | Cartoon Ring Color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_finder` | Cartoon Ring Finder | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_mode` | Cartoon Ring Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_mode.mode.center_sphere` | Cartoon Ring Mode mode center_sphere | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_mode.mode.edge_bounded` | Cartoon Ring Mode mode edge_bounded | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_mode.mode.ladder_only` | Cartoon Ring Mode mode ladder_only | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_mode.mode.round_edge` | Cartoon Ring Mode mode round_edge | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_mode.mode.size_sphere` | Cartoon Ring Mode mode size_sphere | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_mode.mode.square_edge` | Cartoon Ring Mode mode square_edge | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_radius` | Cartoon Ring Radius | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_transparency` | Cartoon Ring Transparency | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_width` | Cartoon Ring Width | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.round_helices` | Cartoon Round Helices | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.sampling` | Cartoon Sampling | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.backbone.cartoon.setting.side_chain_helper` | Cartoon Side Chain Helper | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.smooth_cycles` | Cartoon Smooth Cycles | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.smooth_cylinder_cycles` | Cartoon Smooth Cylinder Cycles | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.smooth_cylinder_window` | Cartoon Smooth Cylinder Window | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.smooth_first` | Cartoon Smooth First | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.smooth_last` | Cartoon Smooth Last | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.smooth_loops` | Cartoon Smooth Loops | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.throw` | Cartoon Throw | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.trace_atoms` | Cartoon Trace Atoms | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.transparency` | Cartoon Transparency | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.tube_cap` | Cartoon Tube Cap | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.tube_cap.mode.enum_flat` | Cartoon Tube Cap mode enum_flat | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.tube_cap.mode.enum_round` | Cartoon Tube Cap mode enum_round | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.tube_cap.mode.none` | Cartoon Tube Cap mode none | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.tube_quality` | Cartoon Tube Quality | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.tube_radius` | Cartoon Tube Radius | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.use_shader` | Cartoon Use Shader | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.style_assign` | Cartoon style assignment | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `representation.backbone.cartoon.type.arrow` | Cartoon type arrow | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.type.automatic` | Cartoon type automatic | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 7 |
| `representation.backbone.cartoon.type.cylinder` | Cartoon type cylinder | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.type.dash` | Cartoon type dash | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.type.dumbbell` | Cartoon type dumbbell | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.type.loop` | Cartoon type loop | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 7 |
| `representation.backbone.cartoon.type.oval` | Cartoon type oval | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.type.putty` | Cartoon type putty | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.type.rectangle` | Cartoon type rectangle | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 7 |
| `representation.backbone.cartoon.type.skip` | Cartoon type skip | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.type.tube` | Cartoon type tube | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 7 |
| `representation.backbone.ribbon` | PyMOL-compatible ribbon representation | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.backbone.ribbon.setting.as_cylinders` | Ribbon As Cylinders | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.color` | Ribbon Color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.nucleic_acid_mode` | Ribbon Nucleic Acid Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.nucleic_acid_mode.mode.extend_3prime` | Ribbon Nucleic Acid Mode mode extend_3prime | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.nucleic_acid_mode.mode.extend_5prime` | Ribbon Nucleic Acid Mode mode extend_5prime | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.nucleic_acid_mode.mode.extend_both` | Ribbon Nucleic Acid Mode mode extend_both | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.nucleic_acid_mode.mode.phosphorus` | Ribbon Nucleic Acid Mode mode phosphorus | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.nucleic_acid_mode.mode.sugar` | Ribbon Nucleic Acid Mode mode sugar | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.power` | Ribbon Power | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.power_b` | Ribbon Power B | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.radius` | Ribbon Radius | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.sampling` | Ribbon Sampling | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.backbone.ribbon.setting.side_chain_helper` | Ribbon Side Chain Helper | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.throw` | Ribbon Throw | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.trace_atoms` | Ribbon Trace Atoms | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.transparency` | Ribbon Transparency | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.use_shader` | Ribbon Use Shader | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.width` | Ribbon Width | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.backbone.setting.trace_atoms_mode` | Trace Atoms Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.bond.lines` | PyMOL-compatible lines representation | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.lines.backend.cylinders` | Lines rendered as cylinders | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.lines.backend.shader` | Line shader path | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.lines.color_override` | Line color override | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.lines.radius_world` | Ray line radius | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.lines.smooth` | Line antialiasing | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.lines.stick_helper` | Line/stick overlap helper | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.lines.width_pixels` | Line width in pixels | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.shared.half_bonds` | Half-bond selection policy | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.shared.hide_long_bonds` | Long-bond hiding policy | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks` | PyMOL-compatible sticks representation | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.sticks.backend.cylinders` | Sticks rendered as cylinders | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.backend.debug` | Stick renderer debug mode | `pymol-oss-3.1.0` | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.backend.shader` | Stick shader path | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.ball.color` | Stick ball color override | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.ball.enabled` | Stick ball-and-stick atoms | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.sticks.ball.ratio` | Stick ball radius ratio | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.sticks.color_override` | Stick color override | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.sticks.endpoint_nub` | Stick endpoint nub size | `pymol-oss-3.1.0` | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.endpoint_overlap` | Stick endpoint overlap | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.endpoint_round_nub` | Rounded stick endpoint nubs | `pymol-oss-3.1.0` | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.good_geometry` | High-quality stick geometry policy | `pymol-oss-3.1.0` | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.hydrogen_radius_scale` | Hydrogen stick radius scale | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.sticks.quality` | Stick radial tessellation quality | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.radius` | Stick cylinder radius | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.sticks.transparency` | Stick transparency | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.sticks.valence.fixed_radius` | Fixed-radius valence sticks | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.valence.scale` | Stick valence radius scale | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.valence.enabled` | Bond-order valence display | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.valence.layout` | Line valence layout mode | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.valence.layout.fancy` | Neighbor-aware valence layout | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.valence.layout.simple` | Simple valence layout | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.valence.spacing` | Line valence spacing | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.valence.zero_order.dashed` | Dashed zero-order bonds | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.valence.zero_order.mode` | Zero-order bond appearance | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.valence.zero_order.scale` | Zero-order bond radius scale | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.valence.zero_order.skip` | Skip zero-order bonds | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.valence.zero_order.solid` | Solid zero-order bonds | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.cache.rebuild` | Representation geometry rebuild | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `representation.cache.rebuild.representation_scope` | Type-scoped representation rebuild | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.cache.rebuild.selection_scope` | Selection-scoped representation rebuild | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.color.recolor` | Representation color-cache refresh | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.color.recolor.representation_scope` | Type-scoped recolor | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.color.recolor.selection_scope` | Selection-scoped recolor | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.crystal.unit_cell` | PyMOL-compatible cell representation | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.custom.callback` | PyMOL-compatible callback representation | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.custom.cgo` | PyMOL-compatible cgo representation | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.defaults.auto_show_lines` | Auto-show lines on load | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.defaults.auto_show_nonbonded` | Auto-show nonbonded atoms on load | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.defaults.auto_show_spheres` | Auto-show spheres on load | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.dna_helical_params` | DNA from helical parameters | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.dot.setting.as_spheres` | Dot As Spheres | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.color` | Dot Color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.density` | Dot Density | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.density.mode.density_0` | Dot Density mode density_0 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.density.mode.density_1` | Dot Density mode density_1 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.density.mode.density_2` | Dot Density mode density_2 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.density.mode.density_3` | Dot Density mode density_3 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.density.mode.density_4` | Dot Density mode density_4 | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.hydrogens` | Dot Hydrogens | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.lighting` | Dot Lighting | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.mode` | Dot Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.normals` | Dot Normals | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.radius` | Dot Radius | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.solvent` | Dot Solvent | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.trim_dots` | Trim Dots | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.use_shader` | Dot Use Shader | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.width` | Dot Width | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.glycan_snfg` | 3D-SNFG glycan symbols | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.helix_cylinders` | Helix axis cylinders | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance.clone` | Clone representations | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance.num` | num(molid) : Returns the number of representations in the given molecule | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.instance_clip.manage` | Per-representation clipping planes | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.interaction_surface` | Interaction surface between groups | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.label.atom` | PyMOL-compatible labels representation | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `representation.label.content.clear` | Clear selected atom labels | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `representation.label.content.expression` | Evaluated atom label expression | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `representation.label.content.literal` | Literal atom label assignment | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `representation.label.content.property_namespace` | Label expression atom-property namespace | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.drag.atom` | Drag atom label | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `representation.label.setting.anchor` | Label Anchor | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.bg_color` | Label Bg Color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.bg_outline` | Label Bg Outline | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.bg_transparency` | Label Bg Transparency | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.color` | Label Color | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `representation.label.setting.connector` | Label Connector | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.connector_color` | Label Connector Color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.connector_ext_length` | Label Connector Ext Length | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.connector_mode` | Label Connector Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.connector_mode.mode.adaptive_bent` | Label Connector Mode mode adaptive_bent | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.connector_mode.mode.bent_lower_corner` | Label Connector Mode mode bent_lower_corner | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.connector_mode.mode.center_to_box` | Label Connector Mode mode center_to_box | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.connector_mode.mode.closest_box_point` | Label Connector Mode mode closest_box_point | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.connector_mode.mode.closest_corner_or_edge` | Label Connector Mode mode closest_corner_or_edge | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.connector_width` | Label Connector Width | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.digits` | Label Digits | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `representation.label.setting.float_labels` | Float Labels | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id` | Label Font Id | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `representation.label.setting.font_id.mode.gentium_italic` | Label Font Id mode gentium_italic | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.gentium_roman` | Label Font Id mode gentium_roman | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.legacy_fallback_default` | Label Font Id mode legacy_fallback_default | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.mono` | Label Font Id mode mono | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.mono_bold` | Label Font Id mode mono_bold | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.mono_bold_oblique` | Label Font Id mode mono_bold_oblique | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.mono_oblique` | Label Font Id mode mono_oblique | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.sans` | Label Font Id mode sans | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.sans_bold` | Label Font Id mode sans_bold | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.sans_bold_oblique` | Label Font Id mode sans_bold_oblique | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.sans_oblique` | Label Font Id mode sans_oblique | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.serif` | Label Font Id mode serif | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.serif_bold` | Label Font Id mode serif_bold | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.serif_bold_oblique` | Label Font Id mode serif_bold_oblique | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.serif_oblique` | Label Font Id mode serif_oblique | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.multiline_justification` | Label Multiline Justification | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.multiline_spacing` | Label Multiline Spacing | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.outline_color` | Label Outline Color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.padding` | Label Padding | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.pick_labels` | Pick Labels | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.pick_labels.mode.disabled` | Pick Labels mode disabled | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.pick_labels.mode.labels_and_geometry` | Pick Labels mode labels_and_geometry | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.pick_labels.mode.labels_only` | Pick Labels mode labels_only | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.placement_offset` | Label Placement Offset | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.position` | Label Position | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `representation.label.setting.position.mode.alignment` | Label Position mode alignment | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.position.mode.camera_displacement` | Label Position mode camera_displacement | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.position.mode.world_z_offset` | Label Position mode world_z_offset | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.ray_connector_flat` | Ray Label Connector Flat | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.relative_mode` | Label Relative Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.relative_mode.mode.normalized_screen` | Label Relative Mode mode normalized_screen | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.relative_mode.mode.pixel_screen` | Label Relative Mode mode pixel_screen | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.relative_mode.mode.world` | Label Relative Mode mode world | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.screen_point` | Label Screen Point | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.shadow_mode` | Label Shadow Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.shadow_mode.mode.cast` | Label Shadow Mode mode cast | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.shadow_mode.mode.cast_and_catch` | Label Shadow Mode mode cast_and_catch | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.shadow_mode.mode.catch` | Label Shadow Mode mode catch | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.shadow_mode.mode.none` | Label Shadow Mode mode none | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.size` | Label Size | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `representation.label.setting.size.mode.screen_points` | Label Size mode screen_points | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.size.mode.world_angstrom` | Label Size mode world_angstrom | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.z_target` | Label Z Target | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.z_target.mode.auto_connector` | Label Z Target mode auto_connector | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.z_target.mode.disabled` | Label Z Target mode disabled | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.z_target.mode.enabled` | Label Z Target mode enabled | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.contour.carve` | Map contour atom carve | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `representation.map.contour.create` | Map contour object creation | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.map.contour.gradient` | Map gradient object | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.contour.isodot` | Map isodot object | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.contour.isomesh` | Map isomesh object | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `representation.map.contour.isosurface` | Map isosurface object | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `representation.map.contour.object_update_policy` | Map contour object update policy | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.map.contour.selection_extent` | Map contour selection extent | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.map.contour.source_state` | Map contour source-state semantics | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.contour.target_state` | Map contour target-state semantics | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.gradient.setting.max_length` | Gradient Max Length | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.gradient.setting.min_length` | Gradient Min Length | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.gradient.setting.min_slope` | Gradient Min Slope | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.gradient.setting.normal_min_dot` | Gradient Normal Min Dot | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.gradient.setting.spacing` | Gradient Spacing | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.gradient.setting.step_size` | Gradient Step Size | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.gradient.setting.symmetry` | Gradient Symmetry | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.isolevel.manage` | Contour isolevel management | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.map.isolevel.query` | Contour isolevel query | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.map.isolevel.update` | Contour isolevel update | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.map.isosurface.mode.dots` | Isosurface mode dots | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.isosurface.mode.lines` | Isosurface mode lines | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.isosurface.mode.triangles_gradient_normals` | Isosurface mode triangles_gradient_normals | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.map.isosurface.mode.triangles_triangle_normals` | Isosurface mode triangles_triangle_normals | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.isosurface.setting.algorithm` | Isosurface Algorithm | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 3 |
| `representation.map.isosurface.setting.algorithm.mode.marching_cubes_basic` | Isosurface Algorithm mode marching_cubes_basic | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.isosurface.setting.algorithm.mode.marching_cubes_vtkm` | Isosurface Algorithm mode marching_cubes_vtkm | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.isosurface.setting.algorithm.mode.marching_tetrahedra` | Isosurface Algorithm mode marching_tetrahedra | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.map.isosurface.side.back` | Isosurface side back | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.isosurface.side.front` | Isosurface side front | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.mesh` | PyMOL-compatible mesh representation | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.measurement.angle` | PyMOL-compatible angles representation | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `representation.measurement.angle.setting.color` | Angle Color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.angle.setting.label_digits` | Label Angle Digits | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.angle.setting.label_position` | Angle Label Position | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.angle.setting.size` | Angle Size | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.dihedral` | PyMOL-compatible dihedrals representation | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `representation.measurement.dihedral.setting.color` | Dihedral Color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.dihedral.setting.label_digits` | Label Dihedral Digits | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.dihedral.setting.label_position` | Dihedral Label Position | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.dihedral.setting.size` | Dihedral Size | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes` | PyMOL-compatible dashes representation | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `representation.measurement.distance_dashes.backend_negotiation` | Distance-dash line/cylinder backend | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes.backend_negotiation.mode.cylinders` | Distance-dash backend cylinders | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes.backend_negotiation.mode.lines` | Distance-dash backend lines | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes.setting.as_cylinders` | Dash As Cylinders | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes.setting.color` | Dash Color | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.measurement.distance_dashes.setting.gap` | Dash Gap | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `representation.measurement.distance_dashes.setting.label_digits` | Label Distance Digits | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes.setting.length` | Dash Length | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `representation.measurement.distance_dashes.setting.radius` | Dash Radius | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes.setting.round_ends` | Dash Round Ends | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes.setting.transparency` | Dash Transparency | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes.setting.use_shader` | Dash Use Shader | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes.setting.width` | Dash Width | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `representation.measurement.label.drag` | Drag measurement label | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.label.numeric_format` | Measurement numeric-label formatting | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 3 |
| `representation.mesh.setting.as_cylinders` | Mesh As Cylinders | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.carve_cutoff` | Mesh Carve Cutoff | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.carve_selection` | Mesh Carve Selection | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.carve_state` | Mesh Carve State | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.clear_cutoff` | Mesh Clear Cutoff | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.clear_selection` | Mesh Clear Selection | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.clear_state` | Mesh Clear State | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.color` | Mesh Color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.cutoff` | Mesh Cutoff | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.grid_max` | Mesh Grid Max | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.lighting` | Mesh Lighting | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.mode` | Mesh Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.mode.mode.all` | Mesh Mode mode all | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.mode.mode.flags` | Mesh Mode mode flags | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.mode.mode.heavy_atoms` | Mesh Mode mode heavy_atoms | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.negative_color` | Mesh Negative Color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.negative_visible` | Mesh Negative Visible | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.normals` | Mesh Normals | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.quality` | Mesh Quality | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.radius` | Mesh Radius | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.skip` | Mesh Skip | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.solvent` | Mesh Solvent | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.type` | Mesh Type | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.type.mode.points` | Mesh Type mode points | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.type.mode.wire` | Mesh Type mode wire | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.use_shader` | Mesh Use Shader | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.width` | Mesh Width | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.alignment_as_cylinders` | Setting alignment_as_cylinders | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.auto_color_next` | Setting auto_color_next | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.auto_show_classified` | Setting auto_show_classified | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cell_centered` | Setting cell_centered | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cell_color` | Setting cell_color | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cgo_dot_radius` | Setting cgo_dot_radius | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cgo_dot_width` | Setting cgo_dot_width | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cgo_ellipsoid_quality` | Setting cgo_ellipsoid_quality | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cgo_lighting` | Setting cgo_lighting | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cgo_line_radius` | Setting cgo_line_radius | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cgo_line_width` | Setting cgo_line_width | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cgo_ray_width_scale` | Setting cgo_ray_width_scale | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cgo_sphere_quality` | Setting cgo_sphere_quality | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cgo_transparency` | Setting cgo_transparency | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.chromadepth` | Setting chromadepth | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cone_quality` | Setting cone_quality | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.dist_counter` | Setting dist_counter | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.draw_mode` | Setting draw_mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.dynamic_width` | Setting dynamic_width | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.dynamic_width_factor` | Setting dynamic_width_factor | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.dynamic_width_max` | Setting dynamic_width_max | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.dynamic_width_min` | Setting dynamic_width_min | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.ellipsoid_color` | Setting ellipsoid_color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.ellipsoid_probability` | Setting ellipsoid_probability | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.ellipsoid_quality` | Setting ellipsoid_quality | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.ellipsoid_scale` | Setting ellipsoid_scale | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.ellipsoid_transparency` | Setting ellipsoid_transparency | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.isomesh_auto_state` | Setting isomesh_auto_state | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.map_auto_expand_sym` | Setting map_auto_expand_sym | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.nb_spheres_quality` | Setting nb_spheres_quality | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.nb_spheres_size` | Setting nb_spheres_size | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.nonbonded_as_cylinders` | Setting nonbonded_as_cylinders | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.nonbonded_size` | Setting nonbonded_size | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.nonbonded_transparency` | Setting nonbonded_transparency | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.pymol_space_max_blue` | Setting pymol_space_max_blue | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.pymol_space_max_green` | Setting pymol_space_max_green | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.pymol_space_max_red` | Setting pymol_space_max_red | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.pymol_space_min_factor` | Setting pymol_space_min_factor | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.ramp_blend_nearby_colors` | Setting ramp_blend_nearby_colors | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.smooth_color_triangle` | Setting smooth_color_triangle | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.smooth_half_bonds` | Setting smooth_half_bonds | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.spheroid_fill` | Setting spheroid_fill | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.spheroid_scale` | Setting spheroid_scale | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.spheroid_smooth` | Setting spheroid_smooth | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.object.extent` | PyMOL-compatible extent representation | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.preset.licorice` | PyMOL-compatible licorice representation | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.preset.wire` | PyMOL-compatible wire representation | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.slice.setting.dynamic_grid` | Slice Dynamic Grid | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.slice.setting.dynamic_grid_resolution` | Slice Dynamic Grid Resolution | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.slice.setting.grid` | Slice Grid | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.slice.setting.height_map` | Slice Height Map | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.slice.setting.height_scale` | Slice Height Scale | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.slice.setting.track_camera` | Slice Track Camera | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.style.beads` | Beads | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.style.dynamicbonds` | DynamicBonds | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.style.fieldlines` | FieldLines | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.style.hbonds` | HBonds | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.style.orbital` | Orbital | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.style.paperchain` | PaperChain | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.style.polyhedra` | Polyhedra | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.style.solvent` | Solvent | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.style.trace` | Trace | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.style.tube` | Tube | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.style.twister` | Twister | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.surface.molecular` | PyMOL-compatible surface representation | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `unwritten` | — | 1 |
| `representation.surface.nanoshaper` | NanoShaper molecular surface | `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.surface.setting.best` | Surface Best | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.carve_cutoff` | Surface Carve Cutoff | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.carve_normal_cutoff` | Surface Carve Normal Cutoff | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.carve_selection` | Surface Carve Selection | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.carve_state` | Surface Carve State | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.cavity_cutoff` | Surface Cavity Cutoff | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.cavity_mode` | Surface Cavity Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.cavity_radius` | Surface Cavity Radius | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.circumscribe` | Surface Circumscribe | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.clear_cutoff` | Surface Clear Cutoff | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.clear_selection` | Surface Clear Selection | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.clear_state` | Surface Clear State | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.color` | Surface Color | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 3 |
| `representation.surface.setting.color_smoothing` | Surface Color Smoothing | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.color_smoothing_threshold` | Surface Color Smoothing Threshold | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.debug` | Surface Debug | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.miserable` | Surface Miserable | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.mode` | Surface Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.mode.mode.all` | Surface Mode mode all | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.mode.mode.flags` | Surface Mode mode flags | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.mode.mode.heavy_atoms` | Surface Mode mode heavy_atoms | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.mode.mode.visible` | Surface Mode mode visible | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.mode.mode.visible_heavy_atoms` | Surface Mode mode visible_heavy_atoms | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.negative_color` | Surface Negative Color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.negative_visible` | Surface Negative Visible | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.normal` | Surface Normal | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.optimize_subsets` | Surface Optimize Subsets | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.pick_surface` | Pick Surface | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.poor` | Surface Poor | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.proximity` | Surface Proximity | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.quality` | Surface Quality | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.ramp_above_mode` | Surface Ramp Above Mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.residue_cutoff` | Surface Residue Cutoff | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.smooth_edges` | Surface Smooth Edges | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.solvent` | Surface Solvent | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.trim_cutoff` | Surface Trim Cutoff | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.trim_factor` | Surface Trim Factor | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.type` | Surface Type | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.type.mode.completely_scribed` | Surface Type mode completely_scribed | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.type.mode.deterministic_alt_weighting` | Surface Type mode deterministic_alt_weighting | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.type.mode.deterministic_solid` | Surface Type mode deterministic_solid | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.type.mode.dots_conventional` | Surface Type mode dots_conventional | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.type.mode.pairwise_scribed` | Surface Type mode pairwise_scribed | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.type.mode.solid_conventional` | Surface Type mode solid_conventional | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.type.mode.triangle_mesh_conventional` | Surface Type mode triangle_mesh_conventional | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.use_shader` | Surface Use Shader | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.shared.setting.cavity_cull` | Cavity Cull | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.shared.setting.min_mesh_spacing` | Min Mesh Spacing | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.shared.setting.solvent_radius` | Solvent Radius | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.visibility.argument.selection_first` | Legacy selection-first disambiguation | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.visibility.as_exclusive` | Exclusive representation replacement | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.composite.licorice` | Licorice composite visibility | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.composite.wire` | Wire composite visibility | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.default.as_wire` | No-argument as default | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.visibility.default.hide_everything` | No-argument hide default | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.visibility.default.show_wire` | No-argument show default | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.visibility.hide` | Representation hide | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.independent_from_object` | Representation/object visibility independence | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.manage` | Per-selection representation visibility model | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.mask.everything` | All-representations aggregate mask | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.mask.multiple` | Multiple-representation mask | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `representation.visibility.nonmolecular_object` | Non-molecular object representation visibility | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.visibility.scope.name_pattern` | Object/name-pattern visibility scope | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.visibility.scope.selection` | Atom-selection visibility scope | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.show_additive` | Additive representation show | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.toggle_all_or_none` | All-or-none representation toggle | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.toggle_object` | Object visibility through toggle special case | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.carve` | Direct volume atom carve | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.create` | Direct volume object creation | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.volume.direct` | PyMOL-compatible volume representation | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `unwritten` | — | 2 |
| `representation.volume.legacy_numeric_ramp` | Legacy numeric volume ramp argument | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.object_overwrite` | Direct volume overwrite policy | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `representation.volume.ramp.builtin_presets` | Built-in volume ramp presets | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.volume.ramp.define` | Volume ramp definition | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `representation.volume.ramp.get` | Volume ramp query | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `representation.volume.ramp.panel` | Interactive volume ramp editor | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.ramp.set` | Volume ramp assignment | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.volume.selection_extent` | Direct volume selection extent | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.setting.bit_depth` | Volume Bit Depth | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.setting.data_range` | Volume Data Range | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.setting.layers` | Volume Layers | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.setting.mode` | Volume Mode | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.volume.setting.mode.mode.post_classified` | Volume Mode mode post_classified | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.volume.setting.mode.mode.pre_integrated` | Volume Mode mode pre_integrated | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `representation.volume.slice` | PyMOL-compatible slice representation | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `unwritten` | — | 2 |
| `representation.volume.slice.create` | Map slice object creation | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.volume.slice.source_state` | Slice source-state semantics | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.slice.target_state` | Slice target-state semantics | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.source_state` | Direct volume source-state semantics | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.target_state` | Direct volume target-state semantics | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.b_factor_putty` | Preset b_factor_putty | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.ball_and_stick` | Preset ball_and_stick | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.classified` | Preset classified | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.default` | Preset default | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.import_export` | Preset import/export | `pymol-incentive-3.1.8` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation_preset.interface` | Preset interface | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.ligand_cartoon` | Preset ligand_cartoon | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.ligand_sites` | Preset ligand_sites | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.ligand_sites_dots` | Preset ligand_sites_dots | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.ligand_sites_hq` | Preset ligand_sites_hq | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.ligand_sites_mesh` | Preset ligand_sites_mesh | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.ligand_sites_trans` | Preset ligand_sites_trans | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.ligand_sites_trans_hq` | Preset ligand_sites_trans_hq | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.ligands` | Preset ligands | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.manage` | Representation presets | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.pretty` | Preset pretty | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.pretty_solv` | Preset pretty_solv | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.pub_solv` | Preset pub_solv | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.publication` | Preset publication | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.simple` | Preset simple | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.simple_no_solv` | Preset simple_no_solv | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.technical` | Preset technical | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `annotation.callout` | Screen-stabilized callout | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `annotation.color_scale_bar` | Color scale bar | `vmd-plugin-catalog-current` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `annotation.curve.create` | Create curve object | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `annotation.dipole_arrow` | Dipole moment arrow | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `camera.clip.far_absolute` | Set far clip plane absolutely | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.clip.far_relative` | Move far clip plane relatively | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.clip.fit_atoms` | Fit clipping planes to selected atoms | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.clip.manage` | Camera clipping-slab management | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.clip.move_slab` | Move both clipping planes | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.clip.near_absolute` | Set near clip plane absolutely | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.clip.near_relative` | Move near clip plane relatively | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.clip.query` | Query clipping-plane positions | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `camera.clip.slab_thickness` | Set clipping-slab thickness | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.fly_navigation` | Flying camera navigation | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `camera.frame.center` | Center camera on a selection | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.frame.center.animate` | Animated center transition | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.frame.center.move_origin` | Center rotation-origin policy | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.frame.center.state_scope` | Center coordinate-state scope | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `camera.frame.orient` | Principal-axis camera orientation | `pymol-oss-3.1.0` | P1 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.frame.orient.animate` | Animated orientation transition | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.frame.orient.state_scope` | Orient coordinate-state scope | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.frame.zoom` | Frame a selection by zooming | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.frame.zoom.animate` | Animated zoom transition | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.frame.zoom.buffer` | Zoom framing buffer | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.frame.zoom.complete` | Complete no-center-clipping zoom | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.frame.zoom.state_scope` | Zoom coordinate-state scope | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `camera.named_view.clear_all` | Clear or list all named camera views | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `camera.named_view.delete` | Delete a named camera view | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `camera.named_view.manage` | Named camera-view lifecycle | `pymol-oss-3.1.0`, `vmd-plugin-catalog-current` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `camera.named_view.recall` | Recall a named camera view | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `camera.named_view.store` | Store a named camera view | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `camera.navigation.move_axis` | Translate camera along a primary axis | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57`, `vmd-plugin-catalog-current` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.navigation.rock` | Continuous Y-axis camera rocking | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.navigation.turn_axis` | Rotate camera about a primary axis | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57`, `vmd-plugin-catalog-current` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.origin.from_selection` | Rotation origin from selection | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.origin.manage` | Rotation-origin management | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.origin.object_scope` | Per-object rotation origin | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.origin.position` | Explicit rotation-origin position | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.origin.state_scope` | Rotation-origin coordinate-state scope | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.projection.field_of_view` | Vertical field-of-view control | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.projection.orthographic` | Perspective or orthographic projection | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.reset.global` | Reset global camera and framing | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.reset.object_transform` | Reset per-object transform | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `camera.rock.manage` | rock command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `camera.rotate.by` | Rotate at a rate of step degrees per redraw | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `camera.rotate.manage` | rotate command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `camera.rotate.stop` | Stop all rotation, similar to rock off, but it also stops mouse rotations as wel | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `camera.rotate.to` | Rotate the given axis to the absolute position angle  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `camera.state.camera_origin` | Camera-space rotation-origin state | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | — | — | `compatible` | macos | 4 |
| `camera.state.clip_planes` | Front and rear clip-plane state | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | — | — | `compatible` | macos | 4 |
| `camera.state.manage` | Complete camera-state model | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | — | — | `compatible` | macos | 4 |
| `camera.state.model_origin` | Model-space rotation-origin state | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | — | — | `compatible` | macos | 4 |
| `camera.state.projection_encoding` | Projection and field-of-view view encoding | `pymol-oss-3.1.0` | P1 | 검증됨 (`compatible`) | 예 | — | — | `compatible` | macos | 4 |
| `camera.state.restore_18` | Restore the 18-value PyMOL view | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.state.rotation` | Model-to-camera rotation state | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 검증됨 (`compatible`) | 예 | — | — | `compatible` | macos | 4 |
| `camera.state.serialize_18` | Serialize the 18-value PyMOL view | `pymol-oss-3.1.0` | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.stereo.anaglyph` | Anaglyph stereo mode | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.anaglyph_mode` | Anaglyph color-combination policy | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.angle` | Stereo angular-separation scale | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.checkerboard` | Checkerboard stereo mode | `pymol-oss-3.1.0` | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.chromadepth` | Chromadepth stereo mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.stereo.clone_dynamic` | Cloned dynamic stereo mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.stereo.column_interleaved` | Column-interleaved stereo mode | `pymol-oss-3.1.0` | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.crosseye` | Cross-eye stereo mode | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.custom` | Custom stereo mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.stereo.double_pump_mono` | Stereo-context monoscopic double pumping | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.stereo.dynamic` | Dynamic stereo mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.stereo.dynamic_strength` | Dynamic stereo strength | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.stereo.enabled` | Stereo enable/disable state | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.geowall` | GeoWall stereo mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.stereo.manage` | Stereoscopic display-mode management | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.openvr` | OpenVR stereo mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.stereo.quadbuffer` | Quad-buffered stereo mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.stereo.row_interleaved` | Row-interleaved stereo mode | `pymol-oss-3.1.0` | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.shift` | Stereo camera separation | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.side_by_side` | Side-by-side stereo mode | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.swap` | Swap stereo eyes | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.walleye` | Wall-eye stereo mode | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `camera.translate.manage` | translate command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `camera.translate.to` | Translate to the absolute position ( x , y , z ) in screen units | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `camera.viewport.query` | Query graphics viewport dimensions | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | — | — | `specified` | — | 2 |
| `camera.viewport.set` | Set graphics viewport dimensions | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | — | — | `specified` | — | 2 |
| `graphics.primitive.color_index` | graphics color colorId | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.color_name` | graphics color name | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.color_transparent_name` | Each of the above geometrical objects are drawn using the current color | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.cone` | Draw a cone with the center of the base at the first point and the tip at the se | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.cylinder` | Draws a cylinder of the given radius (default r 1) from the first point to the s | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.delete_all` | all : Deletes all graphics primitives | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.delete_one` | id : Deletes the graphics primitive with the given id | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.exists` | id : Returns whether the primitive with the given id exists | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.info` | Returns the text of a Tcl command which will recreate the graphics primitive wit | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.line` | Draws either a solid or dashed line of the given width from the first point to t | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.list` | Returns a list of valid graphics id's | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.manage` | graphics command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.material` | Sets the material to use for the corresponding graphics molecule | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.materials` | Material properties are used to make the graphical objects (lines, cylinders, et | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.point` | Draws a point at the given position | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.replace` | id : Causes the next graphics primitive to replace the one with the given id | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.size` | Displays the text string with the bottom left of the string starting at the give | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.sphere` | Draws a sphere of the given radius (default r 1) centered at the vertex | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.triangle` | Draws a triangle with endpoints at each of the three vertices | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.tricolor` | c1 c2 c3 : Draws a triangle with endpoints at each of the first three points | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `graphics.primitive.trinorm` | Draws a triangle with endpoints at each of the first three points | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.axes.location_get` | Get the current location | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.axes.location_set` | Position axes | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.axes.locations` | Return a list of possible locations | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.axes.manage` | axes command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.message.get` | Query a scene message | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `scene.message.set` | Set a scene message | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `scene.navigate.first` | Recall the first ordered scene | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.navigate.next` | Recall the next ordered scene | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.navigate.previous` | Recall the previous ordered scene | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.order.manage` | Ordered scene collection management | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.order.move_top` | Move scenes to the top | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.order.natural_sort` | Natural-sort scene names | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.order.relative` | Move scenes at their current relative location | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.pymol_experimenting.fast_minimize` | PyMOL cmd.fast_minimize | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_experimenting.load_coords` | PyMOL cmd.load_coords | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_movie.add_blank` | PyMOL movie.add_blank | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_movie.add_nutate` | PyMOL movie.add_nutate | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_movie.add_rock` | PyMOL movie.add_rock | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_movie.add_roll` | PyMOL movie.add_roll | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_movie.add_scenes` | PyMOL movie.add_scenes | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_movie.add_state_loop` | PyMOL movie.add_state_loop | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_movie.add_state_sweep` | PyMOL movie.add_state_sweep | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_movie.get_movie_fps` | PyMOL movie.get_movie_fps | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_movie.timed_roll` | PyMOL movie.timed_roll | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_util.b2vdw` | PyMOL util.b2vdw | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_util.cba` | PyMOL util.cba | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_util.color_carbon` | PyMOL util.color_carbon | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_util.enable_all_shaders` | PyMOL util.enable_all_shaders | `source-audit-2026-10` | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_util.ff_copy` | PyMOL util.ff_copy | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_util.find_surface_residues` | PyMOL util.find_surface_residues | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_util.get_sasa` | PyMOL util.get_sasa | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_util.ligand_zoom` | PyMOL util.ligand_zoom | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_util.mass_align` | PyMOL util.mass_align | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_util.phipsi` | PyMOL util.phipsi | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_util.protein_assign_charges_and_radii` | PyMOL util.protein_assign_charges_and_radii | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_viewing.ipython_image` | PyMOL cmd.ipython_image | `source-audit-2026-10` | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `scene.pymol_viewing.scene_recall_message` | PyMOL cmd.scene_recall_message | `source-audit-2026-10` | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `scene.query.list` | Query ordered scene names | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `scene.query.thumbnail` | Query a scene thumbnail | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `scene.snapshot.component.activity` | Scene object-activity channel | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `scene.snapshot.component.color` | Scene color channel | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.snapshot.component.frame` | Scene global-frame channel | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `scene.snapshot.component.message` | Scene playback-message channel | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `scene.snapshot.component.representation` | Scene representation-visibility channel | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `scene.snapshot.component.selection_scope` | Scene atom-channel selection scope | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `scene.snapshot.component.thumbnail` | Scene thumbnail channel | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `scene.snapshot.component.view` | Scene camera-view channel | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `scene.snapshot.delete` | Delete one or all scene snapshots | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `scene.snapshot.insert_after` | Store current state after the current scene | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.snapshot.insert_before` | Store current state before the current scene | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.snapshot.manage` | Named scene snapshot lifecycle | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `scene.snapshot.recall` | Recall a named scene snapshot | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `scene.snapshot.rename` | Rename a scene snapshot | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.snapshot.store` | Store a named scene snapshot | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `scene.snapshot.update` | Update the current scene while preserving its message | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.stage.location_get` | Get the current location | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.stage.location_set` | Set the location | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.stage.locations` | Get a list of possible locations | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.stage.manage` | stage command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.stage.panels_get` | Get the number of panels in use | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `scene.stage.panels_set` | Set number of panels in stage, up to 30 | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.display.aoambient` | Set ambient occlusion lighting factor to value  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.display.aodirect` | Set ambient occlusion direct lighting rescaling factor to value  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.display.distance` | Set the screen distance to value  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.display.dof` | Turn depth of field focal blur on or off | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.display.dof_fnumber` | Set depth of field aperture f/stop number to value  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.display.dof_focaldist` | Set depth of field focal plane distance to value  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.display.eyesep` | Set the eye separation to value  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.display.focallength` | Set the focal length to value  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.display.get` | Return the current value of the requested option | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.display.get_rendermodes` | Return a list of the available values for the given options | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.display.height` | Set the screen height to value  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.display.manage` | display command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.display.rendermode` | Set the rendering mode to mode  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.display.update` | Force a display update | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.display.update_off` | Turn display update off | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.display.update_on` | Turn display update on | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.display.update_status` | Return the display update status (on or off) | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.display.update_ui` | Similar to display update , but also forces updates of the GUI windows | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.query.position` | Query view center position | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.scale.by` | Multiply scene scaling factor by f  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.scale.manage` | scale command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.scale.to` | Set scene scaling factor to f  | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `view.setting.all_states` | Setting all_states | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.animation` | Setting animation | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.animation_duration` | Setting animation_duration | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.auto_zoom` | Setting auto_zoom | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.motion_bias` | Setting motion_bias | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.motion_hand` | Setting motion_hand | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.motion_linear` | Setting motion_linear | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.motion_power` | Setting motion_power | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.motion_simple` | Setting motion_simple | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.rock` | Setting rock | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.rock_delay` | Setting rock_delay | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.scene_animation` | Setting scene_animation | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.scene_animation_duration` | Setting scene_animation_duration | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.scene_current_name` | Setting scene_current_name | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.scene_frame_mode` | Setting scene_frame_mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.scene_loop` | Setting scene_loop | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.scenes_changed` | Setting scenes_changed | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.state` | Setting state | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.state_counter_mode` | Setting state_counter_mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.sweep_angle` | Setting sweep_angle | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.sweep_mode` | Setting sweep_mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.sweep_phase` | Setting sweep_phase | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.sweep_speed` | Setting sweep_speed | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.appearance.cartoon_color` | Cartoon-color selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.appearance.color` | Atom-color selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.appearance.enabled` | Enabled-object atom selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.appearance.representation` | Representation-visibility selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.appearance.ribbon_color` | Ribbon-color selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.appearance.visible` | Visible-atom selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.acceptor` | Hydrogen-bond acceptor selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.backbone` | Polymer backbone selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.donor` | Hydrogen-bond donor selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.guide` | Polymer guide-atom selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.hbond_acceptor_short` | Hydrogen-bond acceptor shorthand | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.hbond_donor_short` | Hydrogen-bond donor shorthand | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.hetatm` | HETATM-origin selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.hydrogen` | Hydrogen selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.inorganic` | Non-polymer inorganic selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.metal` | Metal selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.nucleic` | Nucleic-polymer selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.organic` | Non-polymer organic selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.polymer` | Polymer selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.protein` | Protein-polymer selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.sidechain` | Polymer sidechain selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.solvent` | Solvent selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.comparison.equal` | Numeric equality comparison | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.comparison.ge` | ge | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.comparison.greater_than` | Greater-than numeric comparison | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.comparison.le` | le | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.comparison.less_than` | Less-than numeric comparison | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.comparison.ne` | ne | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.comparison.range` | Numeric range/list comparison | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.comparison.regex_match` | =~ | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.constant.all` | All atoms | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.constant.none` | Empty atom set | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.coordinate.present` | Current-state coordinate presence | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.coordinate.state` | Coordinate-state selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.bond_steps` | Expand by bond steps | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.bound_to` | Direct bonded atoms including overlap | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.calpha` | Reduce expanded residues to C-alpha atoms | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.chain` | Expand to chains | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.fragment` | Expand to fragments | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.molecule` | Expand to bonded molecules | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.neighbor` | Direct bonded neighbors excluding source | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.object` | Expand to objects | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.residue` | Expand to residues | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.ring` | Expand to small rings | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.segment` | Expand to segments | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.unit_cell` | Expand to crystallographic unit cell | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.flag.action.clear` | Atom flag clear | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.flag.action.reset` | Atom flag reset | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.flag.action.set` | Atom flag set | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.flag.assign` | Atom flag assignment | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.function.abs` | abs(x) | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.function.acos` | acos(x) | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.function.asin` | asin(x) | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.function.atan` | atan(x) | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.function.ceil` | ceil(x) | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.function.cos` | cos(x) | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.function.cosh` | cosh(x) | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.function.exp` | exp(x) | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.function.floor` | floor(x) | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.function.log` | log(x) | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.function.log10` | log10(x) | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.function.sin` | sin(x) | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.function.sinh` | sinh(x) | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.function.sqr` | sqr(x) | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.function.sqrt` | sqrt(x) | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.function.tan` | tan(x) | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.function.tanh` | tanh(x) | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `selection.identifier.altloc` | Alternate-location selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.atom_name` | Atom-name selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.chain` | Chain selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.element` | Element selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.index` | Current object atom-index selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.label` | Atom-label selector | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.object` | Object/model selector | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.peptide_sequence` | Peptide-sequence selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.rank` | Load-order atom-rank selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.residue_id` | Residue-identifier selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.residue_name` | Residue-name selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.segment` | Segment selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.source_id` | Source atom-ID selector | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.keyword.acidic` | acidic | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.acyclic` | acyclic | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.aliphatic` | aliphatic | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.alpha` | alpha | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.amino` | amino | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.aromatic` | aromatic | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.at` | at | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.atomicnumber` | atomicnumber | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.basic` | basic | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.bonded` | bonded | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.buried` | buried | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.cg` | cg | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.charged` | charged | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.cyclic` | cyclic | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.large` | large | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.mass` | mass | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `selection.keyword.medium` | medium | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.neutral` | neutral | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.nfrag` | nfrag | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.keyword.numbonds` | numbonds | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.pfrag` | pfrag | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.keyword.phi` | phi | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.keyword.polar` | polar | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.psi` | psi | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.keyword.purine` | purine | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.pyrimidine` | pyrimidine | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.radius` | radius | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.rasmol` | rasmol | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.keyword.small` | small | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.surface` | surface | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 2 |
| `selection.keyword.ufx` | ufx | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.keyword.ufy` | ufy | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.keyword.ufz` | ufz | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.logical.and` | Logical conjunction | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.logical.not` | Logical negation | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.logical.or` | Logical disjunction | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.logical.subtract` | Selection subtraction | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.macro.slash` | Slash selection macro | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.macro.slash.absolute` | Leading-slash absolute macro | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.macro.slash.all_empty` | All-empty slash macro | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.macro.slash.name_alt_backtick` | Name-backtick altloc qualifier | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.macro.slash.numeric_resn_as_resi` | Numeric residue-name field as residue identifier | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.macro.slash.object_index_backtick` | Object-backtick atom index | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.macro.slash.relative` | Right-aligned relative macro | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.macro.slash.resn_resi_backtick` | Resn-backtick residue identifier | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.macro.slash.skip_empty_star` | Empty and wildcard macro fields | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.macro.slash.slash_limit` | Slash-count limit | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.match.identifiers` | Identifier-set matching | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.match.name_residue` | Name/residue matching | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.named.create_replace` | Create or replace a named selection | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.named.default_name` | Default selection-name shortcut | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.named.delete` | Delete named selections | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.named.deselect_all` | Disable every visible selection indicator | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.named.domain_scope` | Domain-scoped named selection | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.named.indicate` | Transient indicated selection | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.named.indicator_policy` | Creation-time selection indicator policy | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.named.list_query` | List named selections | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.named.manage` | Named-selection lifecycle | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.named.merge` | Merge into an existing named selection | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.named.pop` | Destructive atom-by-atom selection iteration | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.named.reference` | Explicit named-selection/object reference | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.named.select_list` | Direct identifier-list selection API | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.named.select_list.id` | Select-list by source atom ID | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.named.select_list.index` | Select-list by current atom index | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.named.select_list.rank` | Select-list by load-order atom rank | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.named.select_list.state_scope` | State-scoped select-list API | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.named.state_scope` | State-scoped named selection | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.numeric.b_factor` | B-factor selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.numeric.coordinate_x` | Model-space X selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.numeric.coordinate_y` | Model-space Y selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.numeric.coordinate_z` | Model-space Z selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.numeric.formal_charge` | Formal-charge selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.numeric.occupancy` | Occupancy selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.numeric.partial_charge` | Partial-charge selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.numeric.user_property` | User numeric-property selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.object.create` | Creates a new atom selection and returns its name | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `selection.object.delmacro` | name : Delete the macro corresponding to name  | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.object.frame_get` | Returns the animation frame associated with this selection | `vmd-1.9.3` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.object.frame_set` | frame : Set the frame for the selection | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.object.getbonds` | returns a list of bondlists | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.object.keywords` | Return a list of all recognized keywords in an atom selection text | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.object.lmoveby` | move each atom by an offset given in the list | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.object.lmoveto` | move each atom to a point given by the appropriate list element | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.object.macro` | name selection : Create a new singleword atom selection out of existing atom sel | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.object.manage` | atomselect command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `selection.object.moveto` | move all the atoms to a given location | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.object.setbonds` | Set the bonds for the atoms in the selection | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.object.transform` | Applies the given transformation matrix to the coordinates of each atom in the s | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.object.update` | Update the atom selection based on the frame for the selection (the frame can be | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `selection.pickability.mask` | Mask atoms from mouse picking | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.pickability.unmask` | Restore mouse pickability | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.position.first` | First selected atom | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.position.last` | Last selected atom | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.property.custom` | Custom atom-property selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.property.flag` | Atom-flag selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.property.numeric_type` | Numeric-type selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.property.secondary_structure` | Secondary-structure selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.property.stereochemistry` | Stereochemistry selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.property.text_type` | Text-type selector | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.pseudo.rotation_origin` | Rotation-origin pseudo-atom | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.pseudo.scene_center` | Scene-center pseudo-atom | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.pymol_selecting.objsele_state_iter` | PyMOL cmd.objsele_state_iter | `source-audit-2026-10` | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `selection.setting.active_selections` | Setting active_selections | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.auto_hide_selections` | Setting auto_hide_selections | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.auto_indicate_flags` | Setting auto_indicate_flags | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.auto_number_selections` | Setting auto_number_selections | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.auto_show_selections` | Setting auto_show_selections | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.hide_underscore_names` | Setting hide_underscore_names | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.mouse_selection_mode` | Setting mouse_selection_mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.sel_counter` | Setting sel_counter | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.selection_overlay` | Setting selection_overlay | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.selection_round_points` | Setting selection_round_points | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.selection_visible_only` | Setting selection_visible_only | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.selection_width` | Setting selection_width | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.selection_width_max` | Setting selection_width_max | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.selection_width_scale` | Setting selection_width_scale | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.suppress_hidden` | Setting suppress_hidden | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.spatial.around` | Atoms around a selection | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.spatial.beyond` | Beyond-distance selection | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.spatial.expand` | Distance expansion including source | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.spatial.near_to` | Within-distance selection excluding target | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.spatial.vdw_gap` | VDW-surface gap selection | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.spatial.within` | Within-distance selection | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.status.bonded` | Bonded-atom selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.status.fixed` | Fixed-atom selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.status.masked` | Masked-atom selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.status.protected` | Protected-atom selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.status.restrained` | Restrained-atom selector | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.surface_atoms.find` | Find surface atoms | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.value.atom_name_wildcard` | Atom-name wildcard override | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.value.case_chain_segment` | Chain and segment case policy | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.value.case_general` | General selection case policy | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.value.escape` | Selection value escape handling | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `selection.value.list.alpha` | Alphabetic value lists | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `selection.value.list.numeric` | Numeric value lists | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `selection.value.manage` | Selection value matching policy | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.value.mixed_residue` | Mixed residue identifier matching | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.value.quote` | Quoted selection values | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.value.range.alpha` | Alphabetic ranges | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `selection.value.range.numeric` | Numeric and open-ended ranges | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.value.wildcard` | Configurable alphabetic wildcard matching | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `selection.vmd_atomsel_method.booleans` | VMD Python atomsel.booleans | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomsel_method.center` | VMD Python atomsel.center | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomsel_method.contacts` | VMD Python atomsel.contacts | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomsel_method.fit` | VMD Python atomsel.fit | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomsel_method.functions` | VMD Python atomsel.functions | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomsel_method.get` | VMD Python atomsel.get | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomsel_method.inverse` | VMD Python atomsel.inverse | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomsel_method.keywords` | VMD Python atomsel.keywords | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomsel_method.macro` | VMD Python atomsel.macro | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomsel_method.minmax` | VMD Python atomsel.minmax | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomsel_method.move` | VMD Python atomsel.move | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomsel_method.moveby` | VMD Python atomsel.moveby | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomsel_method.rgyr` | VMD Python atomsel.rgyr | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomsel_method.rmsd` | VMD Python atomsel.rmsd | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomsel_method.rmsf` | VMD Python atomsel.rmsf | `source-audit-2026-10` | P1 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomsel_method.sasa` | VMD Python atomsel.sasa | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomsel_method.set` | VMD Python atomsel.set | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomsel_method.stringfunctions` | VMD Python atomsel.stringfunctions | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomsel_method.update` | VMD Python atomsel.update | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomsel_method.write` | VMD Python atomsel.write | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomselect_method.getbondorders` | VMD atomselect getbondorders | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomselect_method.getbondtypes` | VMD atomselect getbondtypes | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomselect_method.setbondorders` | VMD atomselect setbondorders | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomselect_method.setbondtypes` | VMD atomselect setbondtypes | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_atomselect_method.symboltable` | VMD atomselect symboltable | `source-audit-2026-10` | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_keyword.backbonetype` | VMD selection keyword backbonetype | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_keyword.beta_sheet` | VMD selection keyword beta_sheet | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_keyword.betasheet` | VMD selection keyword betasheet | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_keyword.pucker` | VMD selection keyword pucker | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_keyword.residuetype` | VMD selection keyword residuetype | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_keyword.vmd_fast_hydrogen` | VMD selection keyword vmd_fast_hydrogen | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_keyword.volume_sampled` | VMD volume-sampled selection keywords vol0..vol7, volindex0..volindex7, interpvol0..interpvol7 | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_keyword.vx` | VMD selection keyword vx | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_keyword.vy` | VMD selection keyword vy | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.vmd_keyword.vz` | VMD selection keyword vz | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.volume.interpvol` | interpvol | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `selection.volume.vol` | vol | `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `interactive_md.imd.connect` | host port : connect to an MD simulation running on the machine named host and li | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `interactive_md.imd.copyunitcell` | Enable or disable copying unit cell information from the previous frame when upd | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `interactive_md.imd.detach` | Disconnect from the simulation | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `interactive_md.imd.keep` | rate : Set the keep rate, i.e | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `interactive_md.imd.kill` | Disconnect from the simulation and also cause it to halt | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `interactive_md.imd.manage` | imd command family | `vmd-1.9.3`, `vmd-1.9.4a57`, `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `interactive_md.imd.pause` | Pause, unpause or toggle the paused state of the remote simulation | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `interactive_md.imd.transfer` | rate : Set the rate at which new coordinates are sent by the remote simulation t | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `simulation.fep.alanine_scan` | Alanine-scanning FEP setup | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `simulation.fep.prepare` | FEP simulation setup | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `simulation.imd_setup` | Interactive MD setup | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `simulation.md_setup_wizard` | Guided MD setup | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `simulation.namd.server` | NAMD server-mode interface | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `simulation.namd.setup` | NAMD job setup | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `simulation.qm_setup` | QM job setup | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `simulation.ss_restraints` | Secondary-structure restraints for MD | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `trajectory.coordinates.smooth` | Window-average coordinate states | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.coordinates.spheroid` | Spheroid motion average | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.fit.intra_states` | Fit all states to a reference state | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.morph.linear` | Linear morph | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.morph.manage` | Conformational morph trajectory | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.morph.rigimol` | RigiMOL morph | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.base_matrix.check` | Base matrix check | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.base_matrix.clear` | Base matrix clear | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.base_matrix.manage` | Movie base camera matrix | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.base_matrix.recall` | Base matrix recall | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.base_matrix.store` | Base matrix store | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.frame_command.append` | Append frame command | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.frame_command.dump` | Dump frame commands | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.frame_command.manage` | Per-frame command lifecycle | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.frame_command.replace` | Replace frame command | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.frame_command.security_lock` | Lock restored frame commands | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.image_cache.clear` | Clear rendered-frame cache | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.camera_scope` | Camera keyframe scope | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.clear` | Keyframe clear | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.interpolate` | Keyframe interpolate | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.interpolation_parameters` | Keyframe interpolation parameters | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.manage` | Camera and object keyframes | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.object_scope` | Object keyframe scope | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.purge` | Keyframe purge | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.reinterpolate` | Keyframe reinterpolate | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.reset` | Keyframe reset | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.scene_state_channels` | Keyframe scene/state channels | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.smooth` | Keyframe smooth | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.store` | Keyframe store | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.toggle` | Keyframe toggle | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.toggle_interp` | Keyframe toggle_interp | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.uninterpolate` | Keyframe uninterpolate | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.manage` | Movie timeline lifecycle | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.navigation` | Movie frame navigation | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.navigation.first` | Seek first movie frame | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.navigation.frame_query` | Query movie frame | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.navigation.last` | Seek last movie frame | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.navigation.middle` | Seek middle movie frame | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.navigation.seek` | Seek explicit movie frame | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.navigation.state_query` | Query mapped molecular state | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.navigation.step_backward` | Step movie backward | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.navigation.step_forward` | Step movie forward | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.play` | Start movie playback | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.playing_query` | Query movie playback state | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.session.persistence` | Persist movie timeline | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `trajectory.movie.setting.animate_by_frame` | Animate-by-frame policy | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.setting.auto_interpolate` | Automatic interpolation | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.setting.auto_store` | Automatic keyframe store | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.setting.delay` | Movie frame delay | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.setting.frame` | Current movie frame setting | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.setting.loop` | Movie loop policy | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `trajectory.movie.setting.mouse_restart_delay` | Mouse delay restart | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.setting.panel` | Movie panel mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.setting.panel_row_height` | Movie panel row height | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.setting.rock` | Movie rock override | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.setting.scene_restart_delay` | Scene delay restart | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.state_mapping.append` | Append state mapping | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.state_mapping.manage` | Frame-to-state mapping | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.state_mapping.range_syntax` | State-range mapping syntax | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.state_mapping.repeat_syntax` | Repeat-state mapping syntax | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.state_mapping.replace` | Replace state mapping | `pymol-oss-3.1.0` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.stop` | Stop movie playback | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.timeline_edit.copy` | Copy movie frames | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.timeline_edit.delete` | Delete movie frames | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.timeline_edit.freeze` | Movie-edit interpolation freeze | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.timeline_edit.insert` | Insert movie frames | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.timeline_edit.manage` | Movie timeline structural editing | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.timeline_edit.move` | Move movie frames | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.timeline_edit.negative_index` | Movie-edit relative indexing | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.timeline_edit.object_scope` | Movie-edit object scope | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.toggle` | Toggle movie playback | `pymol-oss-3.1.0` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie_generator.load` | Load numbered files as frames | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie_generator.nutate` | Nutate movie | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie_generator.pause` | Pause movie | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie_generator.rock` | Rock movie | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie_generator.roll` | Roll movie | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie_generator.screw` | Screw movie | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie_generator.sweep` | Sweep movie | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie_generator.tdroll` | Three-axis roll movie | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie_generator.zoom` | Zoom movie | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.multi_molecule_animation` | Animate multiple molecules together | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `trajectory.playback.delete` | Delete all frames from memory | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `trajectory.playback.delete_range` | Delete data for molecule_number , beginning with frame nb , ending with frame ne | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `trajectory.playback.dup` | Duplicate the given frame (default ``now'') of molecule molId and add the new fr | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `unwritten` | — | 1 |
| `trajectory.playback.manage` | animate command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 2 |
| `trajectory.playback.reverse` | Play animation backward | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 2 |
| `trajectory.playback.skip` | Set stride to n +1 frames | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 2 |
| `trajectory.playback.speed` | Set animation speed to n  | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 2 |
| `trajectory.playback.style` | Set to play animation once | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 2 |
| `trajectory.playback.style_rock` | Set to play animation forward and back continuously | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 2 |
| `trajectory.playback.styles` | Return a list of the available styles | `vmd-1.9.3`, `vmd-1.9.4a57` | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 2 |
| `trajectory.processing.standalone` | Standalone trajectory processing | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `trajectory.rms.current_states` | Per-state current-coordinate RMS | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `trajectory.rms.fitted_states` | Per-state fitted RMS | `pymol-oss-3.1.0`, `vmd-plugin-catalog-current` | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `trajectory.timeline.frame_count_query` | Query movie frame count | `pymol-oss-3.1.0` | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `trajectory.timeline_composition.clips` | Timeline clips | `pymol-incentive-3.1.8` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `trajectory.timeline_composition.manage` | Timeline compositions | `pymol-incentive-3.1.8` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `trajectory.vmd_animate.last` | VMD animate last | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `trajectory.vmd_animate.top` | VMD animate top | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `app.info.arch` | architecture type (in case you couldn't tell) | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `app.info.build_options` | options used to compile VMD | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `app.info.manage` | vmdinfo command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `app.info.versionmsg` | Full information about this version | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `device.tool.adddevice` | Add a device to a tool, using a name found in the sensor configuration file | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `device.tool.callback` | Enable callbacks for the tools | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `device.tool.change` | Change the type of a tool | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `device.tool.coordinate_scale` | Change the scale of the coordinates reported by a tool | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `device.tool.create` | Create a new tool | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `device.tool.delete` | Remove a tool | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `device.tool.manage` | tool command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `device.tool.offset` | Add a vector to a tool's position | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `device.tool.removedevice` | Remove a device from a tool, using a name found in the sensor configuration file | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `device.tool.rep` | Choose only a single representation for tugging or SMD | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `device.tool.scaleforce` | Increase or decrease the force on a force-feedback device | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.application.quit` | Quit application | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.application.reinitialize` | Reinitialize application state | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.application.splash` | Show splash information | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.contents_panel` | Contents panel | `pymol-incentive-3.1.8` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.diagnostics.frame_rate_reset` | Reset frame-rate meter | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.diagnostics.memory_dump` | Dump memory state | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.diagnostics.renderer` | Query renderer information | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.diagnostics.report` | System diagnostics report | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.help.command` | Command help | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.help.python_keyword` | Python keyword help redirect | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.help.setting` | Setting help | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.hotkey.manage` | user command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.hotkey.print` | Print out the current definition of the hot keys | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.keyboard.binding` | Key binding | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.menu.hide` | Turn a menu off | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.menu.list` | Return a list of the available menus | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.menu.location` | Return the x y location | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.menu.manage` | menu command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.menu.move` | Move a menu to the given ( x , y ) location | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.menu.show` | Turn a menu on | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.menu.status` | Return on if on, off if off | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.mouse.button_binding` | Mouse button binding | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.mouse.config_ring` | Mouse configuration ring | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.mouse.edit_mode` | Toggle editing mouse mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.mouse.mode_cycle` | Cycle mouse mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.mouse_mode.callback` | Turn the callbacks on or off | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.mouse_mode.light_rotate` | Set mouse mode to rotate light N  | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.mouse_mode.manage` | mouse command family | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.mouse_mode.pick_mode` | Set mouse mode to picking mode N , where N is one of the following: | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.mouse_mode.rocking` | Enable/disable persistent rotation of the scene with the mouse | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.mouse_mode.rotate` | Set mouse mode to rotation | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.mouse_mode.scale` | Set mouse mode to scaling | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.mouse_mode.stoprotation` | Stop any mouse-initiated scene rotation as well as any rocking initiated with th | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.mouse_mode.translate` | Set mouse mode to translation | `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.object_tree.group_arrow_prefix` | Show parent-navigation arrow prefixes for group members | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.object_tree.group_full_member_names` | Show full or prefix-stripped group member names | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_cut_laser` | Setting openvr_cut_laser | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_disable_clipping` | Setting openvr_disable_clipping | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_far_plane` | Setting openvr_far_plane | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_alpha` | Setting openvr_gui_alpha | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_back_alpha` | Setting openvr_gui_back_alpha | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_back_color` | Setting openvr_gui_back_color | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_distance` | Setting openvr_gui_distance | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_fov` | Setting openvr_gui_fov | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_overlay` | Setting openvr_gui_overlay | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_scene_alpha` | Setting openvr_gui_scene_alpha | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_scene_color` | Setting openvr_gui_scene_color | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_text` | Setting openvr_gui_text | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_use_alpha` | Setting openvr_gui_use_alpha | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_use_backdrop` | Setting openvr_gui_use_backdrop | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_laser_width` | Setting openvr_laser_width | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_near_plane` | Setting openvr_near_plane | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.output.clear` | Clear output buffer | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.palette_preview` | Color and material palette preview | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.preferences` | Preferences editor | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.presets_panel` | Presets panel | `pymol-incentive-3.1.8` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.pymol_mouse.button_actions` | PyMOL mouse button action codes accepted by the button command | `source-audit-2026-10` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `ui.pymol_wizard.annotation` | PyMOL annotation wizard | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `ui.pymol_wizard.benchmark` | PyMOL benchmark wizard | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.pymol_wizard.box` | PyMOL box wizard | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `ui.pymol_wizard.cleanup` | PyMOL cleanup wizard | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `ui.pymol_wizard.dragging` | PyMOL dragging wizard | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `ui.pymol_wizard.message` | PyMOL message wizard | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.pymol_wizard.nucmutagenesis` | PyMOL nucmutagenesis wizard | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `ui.pymol_wizard.openvr` | PyMOL openvr wizard | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.pymol_wizard.pseudoatom` | PyMOL pseudoatom wizard | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `ui.pymol_wizard.renaming` | PyMOL renaming wizard | `source-audit-2026-10` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `ui.pymol_wizard.stereodemo` | PyMOL stereodemo wizard | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.pymol_wizard.toggle` | PyMOL toggle wizard | `source-audit-2026-10` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.remote_control` | Mobile remote control | `vmd-plugin-catalog-current` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.scenes_panel` | Scenes panel | `pymol-incentive-3.1.8` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.sequence_viewer.setting.seq_view` | Setting seq_view | `pymol-oss-3.1.0`, `vmd-plugin-catalog-current` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_alignment` | Setting seq_view_alignment | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_color` | Setting seq_view_color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_discrete_by_state` | Setting seq_view_discrete_by_state | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_fill_char` | Setting seq_view_fill_char | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_fill_color` | Setting seq_view_fill_color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_format` | Setting seq_view_format | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_gap_mode` | Setting seq_view_gap_mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_label_color` | Setting seq_view_label_color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_label_mode` | Setting seq_view_label_mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_label_spacing` | Setting seq_view_label_spacing | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_label_start` | Setting seq_view_label_start | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_location` | Setting seq_view_location | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_overlay` | Setting seq_view_overlay | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_unaligned_color` | Setting seq_view_unaligned_color | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_unaligned_mode` | Setting seq_view_unaligned_mode | `pymol-oss-3.1.0` | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.setting.auto_overlay` | Setting auto_overlay | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.autoclose_dialogs` | Setting autoclose_dialogs | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.setting.button_mode` | Setting button_mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.button_mode_name` | Setting button_mode_name | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.colored_feedback` | Setting colored_feedback | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.display_scale_factor` | Setting display_scale_factor | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.grid_max` | Setting grid_max | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.grid_mode` | Setting grid_mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.grid_slot` | Setting grid_slot | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.internal_feedback` | Setting internal_feedback | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.internal_gui` | Setting internal_gui | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.internal_gui_control_size` | Setting internal_gui_control_size | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.internal_gui_mode` | Setting internal_gui_mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.setting.internal_gui_name_color_mode` | Setting internal_gui_name_color_mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.internal_gui_width` | Setting internal_gui_width | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.internal_prompt` | Setting internal_prompt | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.keep_alive` | Setting keep_alive | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.legacy_mouse_zoom` | Setting legacy_mouse_zoom | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.mouse_grid` | Setting mouse_grid | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.mouse_limit` | Setting mouse_limit | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.mouse_scale` | Setting mouse_scale | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.mouse_wheel_scale` | Setting mouse_wheel_scale | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.mouse_z_scale` | Setting mouse_z_scale | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.overlay` | Setting overlay | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.overlay_lines` | Setting overlay_lines | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.presentation` | Setting presentation | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.presentation_auto_quit` | Setting presentation_auto_quit | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.presentation_auto_start` | Setting presentation_auto_start | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.presentation_mode` | Setting presentation_mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.setting.scene_buttons` | Setting scene_buttons | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.sdof_drag_scale` | Setting sdof_drag_scale | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.show_frame_rate` | Setting show_frame_rate | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.show_progress` | Setting show_progress | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.text` | Setting text | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.virtual_trackball` | Setting virtual_trackball | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.wizard_prompt_mode` | Setting wizard_prompt_mode | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.wrap_output` | Setting wrap_output | `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.timeline_editor` | Timeline editor panel | `pymol-incentive-3.1.8` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.top_toolbar` | Top toolbar | `pymol-incentive-3.1.8` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
| `ui.window.control` | Window control | `pymol-oss-3.1.0`, `vmd-1.9.3`, `vmd-1.9.4a57` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.window.full_screen` | Full-screen mode | `pymol-incentive-3.1.8`, `pymol-oss-3.1.0` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.workspace_gizmo` | 3D workspace transform gizmo | `pymol-incentive-3.1.8` | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `unwritten` | — | 0 |
