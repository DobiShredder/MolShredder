# Capability 지원표

<!-- Generated evidence projection. Do not edit manually. -->

이 표는 evidence gate를 적용한 snapshot이며 PyMOL 또는 VMD 전체 parity 완료 주장이 아니다. `검증됨` 행만 compatibility claim이다. `진행 중` 행은 실행 가능할 수 있지만 기능이 불완전하거나 conformance evidence가 부족하다.

Machine-readable data: [`capability-support-v1.json`](capability-support-v1.json)

Baseline: `pymol-oss-3.1.0` commit `f51e58f6b08308c41c85b9d12a23231f49ca325a`; 검토일 `2026-10-14`.

이 v1 표의 분모는 공용 capability registry 중 pinned PyMOL baseline reference를 가진 row다. VMD와 Incentive PyMOL에서 정규화한 전용 row는 이 표에 포함하지 않으며, 이 표에서 VMD 지원 여부를 추론하지 않는다. 다중 baseline 표는 [`CAPABILITY_SUPPORT_V2.md`](CAPABILITY_SUPPORT_V2.md)(manifest v2)에서 baseline별 분모와 함께 본다.

## 상태 요약

| 상태 | 개수 | 의미 |
|---|---:|---|
| 검증됨 | 137 | implementation이 compatible/robust이며 fixture, evidence, module과 public interface가 모두 연결됨 |
| 진행 중(미검증) | 467 | partial 구현이며 compatibility 완료 주장이 아님 |
| 인벤토리/계획 | 0 | inventory 또는 acceptance 설계 단계이며 사용자가 실행할 수 없음 |
| 사용 불가/보류 | 1338 | 보류, 제외, 접근 제한 또는 license 격리 상태 |

## Domain 요약

| Domain | 검증됨 | 진행 중 | 계획 | 사용 불가 | 전체 |
|---|---:|---:|---:|---:|---:|
| `analysis` | 0 | 29 | 0 | 56 | 85 |
| `automation` | 2 | 28 | 0 | 24 | 54 |
| `data` | 0 | 0 | 0 | 18 | 18 |
| `editing` | 0 | 0 | 0 | 100 | 100 |
| `io` | 0 | 49 | 0 | 137 | 186 |
| `object_state` | 0 | 41 | 0 | 13 | 54 |
| `pbc` | 0 | 2 | 0 | 4 | 6 |
| `performance` | 0 | 0 | 0 | 51 | 51 |
| `plugin` | 0 | 2 | 0 | 9 | 11 |
| `rendering` | 3 | 54 | 0 | 233 | 290 |
| `representation` | 101 | 70 | 0 | 469 | 640 |
| `scene` | 31 | 50 | 0 | 42 | 123 |
| `selection` | 0 | 120 | 0 | 32 | 152 |
| `trajectory` | 0 | 22 | 0 | 61 | 83 |
| `ui` | 0 | 0 | 0 | 89 | 89 |

## Capability 행

| Capability | 제목 | 우선순위 | 상태 | GUI | CLI | Python | Acceptance | Platform | Evidence |
|---|---|---:|---|:---:|:---:|:---:|---|---|---:|
| `analysis.alignment.all_to_target` | Align all objects to a target | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `analysis.alignment.ce` | Combinatorial Extension alignment | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.alignment.multi_object` | Align multiple objects to one reference | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `analysis.alignment.residue_structural` | Residue/structure-guided refined alignment | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.alignment.sequence_refined` | Sequence-guided refined alignment | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.atom_property.iterate` | Iterate atom properties read-only | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `analysis.center.mass` | Selection center of mass | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `analysis.charge.sum_formal` | Sum formal charges | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `analysis.charge.sum_partial` | Sum partial charges | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `analysis.contacts.find_pairs` | Find atom pairs API | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `analysis.contacts.interchain` | Inter-chain contact distances | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `analysis.coordinates.iterate_state` | Iterate atom coordinates read-only | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `analysis.electrostatics.vacuum_esp` | Protein contact potential (vacuum) | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.fit.explicit_pairs` | Explicit-pair rigid fit | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `analysis.fit.identifier_matched` | Identifier-matched rigid fit | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `analysis.geometry.angle` | Scalar atom angle query | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `analysis.geometry.dihedral` | Scalar atom dihedral query | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `analysis.geometry.distance` | Scalar atom distance query | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `unwritten` | — | 2 |
| `analysis.mass.compute` | Compute molecular mass | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `analysis.measurement_object.angle` | Angle measurement object | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 2 |
| `analysis.measurement_object.dihedral` | Dihedral measurement object | P0 | 진행 중(미검증) (`partial`) | — | — | — | `unwritten` | — | 2 |
| `analysis.measurement_object.distance` | Distance measurement object | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `unwritten` | — | 2 |
| `analysis.measurement_object.distance.all_pairs` | All interatomic distances | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.measurement_object.distance.bonded` | Bond distances | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.measurement_object.distance.centroid` | Centroid distance | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `analysis.measurement_object.distance.exclusion_filtered` | Exclusion-filtered distances | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.measurement_object.distance.pi_cation` | Pi-cation interactions | P1 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `analysis.measurement_object.distance.pi_interactions` | Pi and pi-cation interactions | P1 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `analysis.measurement_object.distance.pi_pi` | Pi-pi interactions | P1 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `analysis.measurement_object.distance.polar_contacts` | Polar-contact distances | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `analysis.measurement_object.distance.vdw_ratio` | VDW-ratio contact distances | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.query.atom_ids` | Query atom ID list | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `analysis.query.atom_indices` | Query object atom indices | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `analysis.query.atom_source_id` | Query single-atom source ID | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.query.chains` | Query chain identifiers | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `analysis.query.extent` | Query selection extent | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `analysis.query.overlap` | Sum VDW overlap | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.query.phi_psi` | Query backbone phi/psi | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `analysis.rms.current` | Current-coordinate RMS | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `analysis.rms.fitted` | Fitted RMS without coordinate mutation | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `analysis.secondary_structure.dss` | Assign secondary structure (dss) | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `analysis.secondary_structure.legacy_ss` | Legacy secondary structure routine | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_helix_phi_exclude` | Setting ss_helix_phi_exclude | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_helix_phi_include` | Setting ss_helix_phi_include | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_helix_phi_target` | Setting ss_helix_phi_target | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_helix_psi_exclude` | Setting ss_helix_psi_exclude | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_helix_psi_include` | Setting ss_helix_psi_include | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_helix_psi_target` | Setting ss_helix_psi_target | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_strand_phi_exclude` | Setting ss_strand_phi_exclude | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_strand_phi_include` | Setting ss_strand_phi_include | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_strand_phi_target` | Setting ss_strand_phi_target | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_strand_psi_exclude` | Setting ss_strand_psi_exclude | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_strand_psi_include` | Setting ss_strand_psi_include | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.secondary_structure.setting.ss_strand_psi_target` | Setting ss_strand_psi_target | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.coulomb_cutoff` | Setting coulomb_cutoff | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.coulomb_dielectric` | Setting coulomb_dielectric | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.coulomb_units_factor` | Setting coulomb_units_factor | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.fit_iterations` | Setting fit_iterations | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.fit_kabsch` | Setting fit_kabsch | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.fit_tolerance` | Setting fit_tolerance | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.gaussian_b_adjust` | Setting gaussian_b_adjust | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.gaussian_b_floor` | Setting gaussian_b_floor | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.gaussian_resolution` | Setting gaussian_resolution | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.h_bond_cone` | Setting h_bond_cone | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.h_bond_cutoff_center` | Setting h_bond_cutoff_center | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.h_bond_cutoff_edge` | Setting h_bond_cutoff_edge | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.h_bond_exclusion` | Setting h_bond_exclusion | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.h_bond_from_proton` | Setting h_bond_from_proton | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.h_bond_max_angle` | Setting h_bond_max_angle | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.h_bond_power_a` | Setting h_bond_power_a | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.h_bond_power_b` | Setting h_bond_power_b | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.halogen_bond_as_acceptor_max_acceptor_angle` | Setting halogen_bond_as_acceptor_max_acceptor_angle | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.halogen_bond_as_acceptor_min_acceptor_angle` | Setting halogen_bond_as_acceptor_min_acceptor_angle | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.halogen_bond_as_acceptor_min_donor_angle` | Setting halogen_bond_as_acceptor_min_donor_angle | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.halogen_bond_as_donor_min_acceptor_angle` | Setting halogen_bond_as_donor_min_acceptor_angle | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.halogen_bond_as_donor_min_donor_angle` | Setting halogen_bond_as_donor_min_donor_angle | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.halogen_bond_distance` | Setting halogen_bond_distance | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.heavy_neighbor_cutoff` | Setting heavy_neighbor_cutoff | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.neighbor_cutoff` | Setting neighbor_cutoff | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.polar_neighbor_cutoff` | Setting polar_neighbor_cutoff | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.setting.salt_bridge_distance` | Setting salt_bridge_distance | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.stereochemistry.assign` | Assign R/S stereo property | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.surface_area.relative_per_residue` | Relative per-residue SASA | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `analysis.surface_area.sasa` | Solvent-accessible surface area | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `plugin.bundled.apbs_electrostatics` | APBS electrostatics plugin | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `app.query.version` | Query application version | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `automation.command.alias` | Register a literal command alias | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `automation.command.dispatch` | Dispatch command text from Python | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `automation.command.introspect` | Inspect command-to-API binding | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `automation.command.register` | Register a Python function as a command | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.command.register_autocomplete` | Register command argument completion | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `automation.command.session_ephemeral` | Do not persist dynamic commands in sessions | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.embedding.context_manager` | Context-managed embedded lifecycle | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `automation.embedding.finish_launching` | Threaded finish-launching helper | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `automation.embedding.headless` | Headless embedded launch loop | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `automation.embedding.manage` | Embedded application lifecycle | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.embedding.multi_instance` | Multiple embedded instances | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `automation.embedding.singleton` | Embedded singleton instance | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `automation.script.execute` | Execute an external script | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `automation.script.literal_python_block` | Literal Python block in command script | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | macos | 4 |
| `automation.script.namespace.global` | Run in the global PyMOL namespace | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.script.namespace.local` | Run with local variable isolation | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.script.namespace.main` | Run in the main Python namespace | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.script.namespace.module` | Run as a synthetic Python module | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.script.namespace.private` | Run with private local variables | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.script.pml_file` | Execute a PML command script file | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | macos | 4 |
| `automation.script.python_file` | Execute a Python script file | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `automation.script.threaded_spawn` | Spawn a Python script thread | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `automation.script.trust_policy` | External script trust and security policy | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `automation.startup.command` | Execute startup command argument | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.startup.config_discovery` | Discover startup configuration scripts | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.startup.deferred_order` | Deterministic deferred startup order | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.startup.disable_automatic` | Disable startup scripts and plugins | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.startup.manage` | Startup automation lifecycle | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.startup.python_foreground` | Execute startup Python script in foreground | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `automation.startup.python_threaded` | Execute startup Python script in a thread | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `automation.startup.stdin` | Read command stream from standard input | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `script.api.sync` | Synchronize command queue | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `script.command.declare_typed` | Declare typed command | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | macos | 6 |
| `script.feedback.control` | Output feedback control | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `script.log.close` | Close command log | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `script.log.open` | Open command log | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `script.log.resume` | Resume command log | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `script.log.write` | Write log entry | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `script.pml.abort` | Abort script execution | P1 | 검증됨 (`compatible`) | — | 예 | 예 | `compatible` | macos | 4 |
| `script.pml.embedded_data_block` | Embedded data block | P2 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | macos | 4 |
| `script.pml.skip_block` | Skip command block | P2 | 검증됨 (`compatible`) | — | 예 | 예 | `compatible` | macos | 4 |
| `script.setting.log_box_selections` | Setting log_box_selections | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `script.setting.log_conformations` | Setting log_conformations | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `script.setting.logging` | Setting logging | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `script.setting.robust_logs` | Setting robust_logs | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `script.setting.security` | Setting security | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `script.setting.stop_on_exceptions` | Setting stop_on_exceptions | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `script.shell.change_directory` | Change working directory | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `script.shell.list_directory` | List directory | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `script.shell.print_directory` | Print working directory | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `script.shell.system` | Execute shell command | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `security.session_prompt.accept` | Accept untrusted session content | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `security.session_prompt.decline` | Decline untrusted session content | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `volume.map.combine` | Combine maps | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.combine.average` | Map combine average | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.combine.difference` | Map combine difference | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.combine.maximum` | Map combine maximum | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.combine.minimum` | Map combine minimum | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.combine.sum` | Map combine sum | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.generate` | Generate map from atoms | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.generate.coulomb` | Map generator coulomb | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.generate.coulomb_local` | Map generator coulomb_local | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.generate.coulomb_neutral` | Map generator coulomb_neutral | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.generate.gaussian` | Map generator gaussian | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.generate.gaussian_max` | Map generator gaussian_max | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.generate.state_policy` | Map generation state policy | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.generate.vdw` | Map generator vdw | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.resample.double` | Double map sampling | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.resample.halve` | Halve map sampling | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.set_border` | Set map border level | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `volume.map.trim` | Trim map extent | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.atom.geometry_assign` | Assign atom geometry and valence | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.atom_name.uniquify` | Uniquify atom names within residues | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.atom_property.alter` | Alter atom properties by expression | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.atom_property.assign_amber99` | Assign Amber99 atomic properties | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.atom_property.expand_from_ca` | Expand CA properties to residues | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.atom_property.vdw_fit` | Fit VDW radii to avoid overlap | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.bond.create` | Create bond | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.bond.delete` | Delete bonds | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.bond.rebond_distance` | Distance-based rebonding | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.bond.valence_cycle` | Cycle picked bond valence | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.bond.valence_set` | Set bond valence | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.builder.fragment` | Load fragment-library structure | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.builder.nucleic_acid` | Build nucleic acid from sequence | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.builder.peptide` | Build peptide from sequence | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.builder.pseudoatom` | Create pseudoatom | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.chemistry.check_forcefield` | Check force-field parameters | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.chemistry.fix` | Recompute chemistry | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.clipboard.ring` | Selection cut/copy/paste ring | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.coordinates.alter_state` | Alter atom coordinates by expression | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.coordinates.reference` | Store and recall reference coordinates | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.coordinates.rotate` | Rotate coordinates or object matrix | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.coordinates.translate` | Translate coordinates or object matrix | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.coordinates.update` | Transfer coordinates between selections | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.dihedral.set` | Set dihedral angle | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.auto_remove_hydrogens` | Setting auto_remove_hydrogens | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.clean_electro_mode` | Setting clean_electro_mode | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.edit_light` | Setting edit_light | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.editor_auto_dihedral` | Setting editor_auto_dihedral | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.editor_auto_measure` | Setting editor_auto_measure | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.editor_auto_origin` | Setting editor_auto_origin | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.editor_bond_cycle_mode` | Setting editor_bond_cycle_mode | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.editor_label_fragments` | Setting editor_label_fragments | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.matrix_mode` | Setting matrix_mode | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.secondary_structure` | Setting secondary_structure | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.suspend_undo` | Setting suspend_undo | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.editor.setting.suspend_undo_atom_count` | Setting suspend_undo_atom_count | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.hydrogens.add` | Add hydrogens | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.hydrogens.fix` | Reposition hydrogens | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.hydrogens.refill_picked` | Refill hydrogens on picked atom | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.minimize.mmff` | MMFF94 energy minimization | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.object.fuse` | Fuse fragments by bond | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.attach` | Attach atom to picked atom | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.clear` | Clear editing picks | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.conformation_redo` | Redo editing conformation | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.conformation_undo` | Undo editing conformation | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.drag` | Interactive coordinate dragging | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.edit` | Pick atoms or bond for editing | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.invert` | Invert picked stereocenter | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.remove` | Remove picked atom or bond | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.replace` | Replace picked atom | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.pick.torsion` | Rotate picked torsion | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.protection.deprotect` | Remove editing protection | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.protection.protect` | Protect atoms from editing | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.residue.mse_to_met` | Convert MSE to MET | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.activate` | Activate sculpting restraints | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.deactivate` | Deactivate sculpting | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.iterate` | Iterate sculpting minimization | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.purge` | Purge sculpting data | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.auto_sculpt` | Setting auto_sculpt | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_angl_weight` | Setting sculpt_angl_weight | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_auto_center` | Setting sculpt_auto_center | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_avd_excl` | Setting sculpt_avd_excl | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_avd_gap` | Setting sculpt_avd_gap | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_avd_range` | Setting sculpt_avd_range | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_avd_weight` | Setting sculpt_avd_weight | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_bond_weight` | Setting sculpt_bond_weight | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_field_mask` | Setting sculpt_field_mask | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_hb_overlap` | Setting sculpt_hb_overlap | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_hb_overlap_base` | Setting sculpt_hb_overlap_base | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_line_weight` | Setting sculpt_line_weight | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_max_max` | Setting sculpt_max_max | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_max_min` | Setting sculpt_max_min | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_max_scale` | Setting sculpt_max_scale | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_max_weight` | Setting sculpt_max_weight | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_memory` | Setting sculpt_memory | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_min_max` | Setting sculpt_min_max | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_min_min` | Setting sculpt_min_min | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_min_scale` | Setting sculpt_min_scale | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_min_weight` | Setting sculpt_min_weight | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_nb_interval` | Setting sculpt_nb_interval | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_plan_weight` | Setting sculpt_plan_weight | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_pyra_inv_weight` | Setting sculpt_pyra_inv_weight | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_pyra_weight` | Setting sculpt_pyra_weight | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_tors_tolerance` | Setting sculpt_tors_tolerance | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_tors_weight` | Setting sculpt_tors_weight | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_tri_max` | Setting sculpt_tri_max | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_tri_min` | Setting sculpt_tri_min | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_tri_mode` | Setting sculpt_tri_mode | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_tri_scale` | Setting sculpt_tri_scale | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_tri_weight` | Setting sculpt_tri_weight | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_vdw_scale` | Setting sculpt_vdw_scale | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_vdw_scale14` | Setting sculpt_vdw_scale14 | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_vdw_vis_max` | Setting sculpt_vdw_vis_max | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_vdw_vis_mid` | Setting sculpt_vdw_vis_mid | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_vdw_vis_min` | Setting sculpt_vdw_vis_min | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_vdw_vis_mode` | Setting sculpt_vdw_vis_mode | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_vdw_weight` | Setting sculpt_vdw_weight | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpt_vdw_weight14` | Setting sculpt_vdw_weight14 | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpting` | Setting sculpting | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `editing.sculpt.setting.sculpting_cycles` | Setting sculpting_cycles | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.async` | Asynchronous fetch | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.chain_code` | Fetch single chain by five-letter code | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.remote` | Fetch structure or map by identifier | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.type.2fofc` | Fetch type 2fofc | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.type.bcif` | Fetch type bcif | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.type.cid` | Fetch type cid | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.type.cif` | Fetch type cif | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.type.emd` | Fetch type emd | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.type.fofc` | Fetch type fofc | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.type.pdb` | Fetch type pdb | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.type.pdb1` | Fetch type pdb1 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.fetch.type.sid` | Fetch type sid | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.acnt.read` | ACNT read | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.aln.read` | ALN read | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.aln.write` | ALN write | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.bcif.read` | BCIF read | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.brix.read` | BRIX read | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.cc1.read` | CC1 read | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.ccp4.read` | CCP4 read | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.ccp4.write` | CCP4 write | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `io.format.cif.read` | CIF read | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.cif.write` | CIF write | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.cml.read` | CML read | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.crd.read` | CRD read | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.dae.read` | DAE read | P1 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `io.format.dx.read` | DX read | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.fasta.read` | FASTA read | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.fasta.write` | FASTA write | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.fld.read` | FLD read | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.grd.read` | GRD read | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.idx.read` | IDX read | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.mae.read` | MAE read | P0 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `io.format.mae.write` | MAE write | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.map.read` | MAP read | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.map.write` | MAP write | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `io.format.mmod.read` | MMOD read | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.mmod.write` | MMOD write | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.mmtf.read` | MMTF read | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.mmtf.write` | MMTF write | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.moe.read` | MOE read | P0 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `io.format.mol.read` | MOL read | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.mol.write` | MOL write | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.mol2.read` | MOL2 read | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.mol2.write` | MOL2 write | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.mrc.read` | MRC read | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.mrc.write` | MRC write | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `io.format.mtz.read` | MTZ read | P1 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `io.format.p1m.read` | P1M read | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.pdb.read` | PDB read | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.pdb.write` | PDB write | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.pdbml.read` | PDBML read | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.pdbqt.read` | PDBQT read | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.phi.read` | PHI read | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.phypo.read` | PHYPO read | P1 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `io.format.pkl.write` | PKL write | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.pkla.write` | PKLA write | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.ply.read` | PLY read | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.pml.read` | PML read | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | macos | 4 |
| `io.format.pmo.read` | PMO read | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.pqr.read` | PQR read | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.pqr.write` | PQR write | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.pwg.read` | PWG read | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.py.read` | PY read | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `io.format.r3d.read` | R3D read | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.rst.read` | RST read | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.sdf.read` | SDF read | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.sdf.write` | SDF write | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.stl.read` | STL read | P1 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `io.format.top.read` | TOP read | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.trj.read` | TRJ read | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.vdb.read` | VDB read | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.vis.read` | VIS read | P0 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `io.format.xplor.read` | XPLOR read | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.format.xyz.read` | XYZ read | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.format.xyz.write` | XYZ write | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.geometry.dump_text` | Dump surface/map geometry as text | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.auto_detect` | Load format auto-detection | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `io.load.batch_glob` | Batch glob load | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `io.load.compression` | Compressed input | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.discrete` | Discrete multi-model load | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.dynamic_molfile_plugin` | Dynamic molfile fallback | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 3 |
| `io.load.extension_alias` | Load extension alias normalization | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `io.load.file` | Load file or URL | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.load.finish_deferred` | Deferred object finalization | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.memory` | Load in-memory content | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `io.load.multiplex` | Multiplex multi-model load | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.object_api` | Load developer object | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.object_api.brick` | ChemPy brick volume object | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.object_api.callback` | Python render callback object | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.object_api.cgo` | compiled graphics object | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.object_api.chempymap` | ChemPy map object | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.object_api.model` | ChemPy model object | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.remote_url` | Remote URL input | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.state_append` | Load into object state | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `io.load.trajectory.attach` | Attach trajectory | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.load.trajectory.image_shift` | AMBER trajectory image and shift | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.load.trajectory.range` | Trajectory frame range and stride | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `io.load.trajectory.selection` | Trajectory coordinate subset | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.model.export` | Export in-memory model | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.model.import` | Import in-memory model | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.mtz.load_maps` | Load MTZ as maps | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.save.auto_detect` | Save format auto-detection | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `io.save.compression` | Compressed output | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.save.file` | Save file | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.save.multi_entry` | Append multi-entry output | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.save.multifile_template` | Object/state filename template export | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.save.selection` | Save selection | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `io.save.state.all` | Save all states | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.save.state.current` | Save current state | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `io.script.load_embedded` | Load embedded data block | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.assembly` | Setting assembly | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.atom_type_format` | Setting atom_type_format | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.auto_classify_atoms` | Setting auto_classify_atoms | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.auto_dss` | Setting auto_dss | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.auto_rename_duplicate_objects` | Setting auto_rename_duplicate_objects | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.chem_comp_cartn_use` | Setting chem_comp_cartn_use | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.cif_keepinmemory` | Setting cif_keepinmemory | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.cif_metalc_as_zero_order_bonds` | Setting cif_metalc_as_zero_order_bonds | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.cif_use_auth` | Setting cif_use_auth | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.connect_bonded` | Setting connect_bonded | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.connect_cutoff` | Setting connect_cutoff | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.connect_mode` | Setting connect_mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.default_2fofc_map_rep` | Setting default_2fofc_map_rep | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.default_buster_names` | Setting default_buster_names | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.default_fofc_map_rep` | Setting default_fofc_map_rep | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.default_phenix_names` | Setting default_phenix_names | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.default_phenix_no_fill_names` | Setting default_phenix_no_fill_names | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.default_refmac_names` | Setting default_refmac_names | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.fetch_host` | Setting fetch_host | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.fetch_path` | Setting fetch_path | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.fetch_type_default` | Setting fetch_type_default | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.ignore_pdb_segi` | Setting ignore_pdb_segi | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.load_atom_props_default` | Setting load_atom_props_default | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.load_object_props_default` | Setting load_object_props_default | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.moe_separate_chains` | Setting moe_separate_chains | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.multiplex` | Setting multiplex | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.normalize_ccp4_maps` | Setting normalize_ccp4_maps | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.normalize_grd_maps` | Setting normalize_grd_maps | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.normalize_o_maps` | Setting normalize_o_maps | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_conect_all` | Setting pdb_conect_all | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_conect_nodup` | Setting pdb_conect_nodup | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_discrete_chains` | Setting pdb_discrete_chains | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_echo_tags` | Setting pdb_echo_tags | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_formal_charges` | Setting pdb_formal_charges | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_hetatm_guess_valences` | Setting pdb_hetatm_guess_valences | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_hetatm_sort` | Setting pdb_hetatm_sort | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_honor_model_number` | Setting pdb_honor_model_number | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_ignore_conect` | Setting pdb_ignore_conect | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_insertions_go_first` | Setting pdb_insertions_go_first | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_insure_orthogonal` | Setting pdb_insure_orthogonal | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_literal_names` | Setting pdb_literal_names | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_no_end_record` | Setting pdb_no_end_record | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_reformat_names_mode` | Setting pdb_reformat_names_mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_retain_ids` | Setting pdb_retain_ids | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_standard_order` | Setting pdb_standard_order | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_truncate_residue_name` | Setting pdb_truncate_residue_name | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_unbond_cations` | Setting pdb_unbond_cations | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pdb_use_ter_records` | Setting pdb_use_ter_records | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.pqr_no_chain_id` | Setting pqr_no_chain_id | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.preserve_chempy_ids` | Setting preserve_chempy_ids | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.rank_assisted_sorts` | Setting rank_assisted_sorts | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.retain_order` | Setting retain_order | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.sdf_write_zero_order_bonds` | Setting sdf_write_zero_order_bonds | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.swap_dsn6_bytes` | Setting swap_dsn6_bytes | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.setting.validate_object_names` | Setting validate_object_names | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `io.string.read_mol` | Read MOL text | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `io.string.read_pdb` | Read PDB text | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `io.string.read_sdf` | Read SDF text | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `io.string.write_pdb` | Write PDB text | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `session.cache.optimize` | Optimize caches before session serialization | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `session.dirty_state` | Session dirty-state tracking | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.embed.data` | Session embedded-data policy | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.export.compatibility` | Export sessions for older PyMOL versions | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `session.extension.hooks` | Extension session save and restore hooks | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.file.load.pse` | Load a PSE session file | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.file.save.pse` | Save a PSE session file | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.migration` | Session version check and migration | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `session.partial` | Partial session export and restore | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.path.current` | Current session-file path tracking | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.presentation.chain` | Chain numbered presentation sessions | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.presentation.load.psw` | Load and auto-start a PSW presentation session | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.presentation.save.psw` | Save a PSW presentation session | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `session.restore.memory` | Restore application state from memory | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `session.security.movie_commands` | Lock commands restored from untrusted sessions | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `session.serialization.encoding` | Session binary and compression encoding | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `session.serialize.memory` | Serialize application state in memory | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `object.copy.exact` | Exact molecular-object copy | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.copy.merge_renamed` | Copy selection into object with identifier conflict repair | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.create.copy_properties` | Copy object properties while creating from a selection | P1 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `object.create.from_selection` | Create object or state from selection | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.delete.named` | Delete named objects or selections | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `object.edit.remove_atoms` | Remove selected atoms | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.edit.sort_atoms` | Canonical atom reorder | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.edit.uniquify_identifier` | Repair identifier collisions | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.extract.selection` | Move selected atoms into a new object | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.auto_name` | Dot-prefix automatic grouping | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.hierarchy.group.add` | Add objects to a group | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.group.auto` | Context-sensitive group add or toggle | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.hierarchy.group.close` | Collapse a group in the object panel | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.group.empty` | Empty a group without deleting members | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.group.excise` | Delete a group while retaining its members | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.group.manage` | Hierarchical group-object lifecycle | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.group.open` | Expand a group in the object panel | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.group.purge_members` | Delete all members of a group | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.group.raise` | Raise a nested group to the top level | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `object.hierarchy.group.remove` | Remove members from a group | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.group.toggle` | Toggle group expansion in the object panel | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.hierarchy.group.ungroup` | Return objects to the top level | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.matrix.copy` | Copy object transformation matrix | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.matrix.look_at` | Orient object toward target | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.matrix.query` | Query object matrix | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.matrix.reset` | Reset object transformation matrix | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.order.manage` | Object and selection panel ordering | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `object.order.move_bottom` | Move object-panel names to the bottom | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `object.order.move_top` | Move object-panel names to the top | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `object.order.natural_sort` | Natural-sort object-panel names | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.order.relative` | Reorder names at their current location | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `object.query.atom_count` | Selection atom count | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `object.query.covering_objects` | Objects covered by a selection | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `object.query.discrete_count` | Discrete-object count | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.query.names` | Object and selection name query | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `object.query.state_count` | Selection state count | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `object.query.type` | Named entity type query | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `object.rename` | Rename object or named selection | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `object.split.chains` | Split objects by chain | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.state.delete` | Delete coordinate states | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.state.join` | Join objects into one multi-state object | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.state.join.discrete` | Join heterogeneous objects as discrete states | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.state.join.identical_topology` | Join states assuming identical topology | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.state.join.identifier_intersection` | Join states by identifier intersection | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.state.join.sequence_alignment` | Join states by sequence alignment | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.state.split` | Split states into single-state objects | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.title.get` | Query state title | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.title.set` | Set state title | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `object.visibility.disable.named` | Disable names matched by a pattern | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `object.visibility.disable.selection` | Disable objects covered by an atom selection | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.visibility.enable.named` | Enable names matched by a pattern | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `object.visibility.enable.parents` | Enable an object's parent groups | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `object.visibility.enable.selection` | Enable objects covered by an atom selection | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `object.visibility.manage` | Object and named-selection enabled state | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `pbc.symmetry.copy` | Copy crystal symmetry | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `pbc.symmetry.expand` | Generate symmetry mates | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `pbc.symmetry.query` | Query crystal symmetry | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `pbc.symmetry.set` | Set crystal symmetry | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `pbc.unwrap` | Unwrap periodic jumps | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `pbc.wrap` | Wrap into periodic box | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `performance.setting.async_builds` | Setting async_builds | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.ati_bugs` | Setting ati_bugs | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.auto_defer_atom_count` | Setting auto_defer_atom_count | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.auto_defer_builds` | Setting auto_defer_builds | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.backface_cull` | Setting backface_cull | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.cache_display` | Setting cache_display | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.cache_max` | Setting cache_max | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.cache_memory` | Setting cache_memory | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `performance.setting.cache_mode` | Setting cache_mode | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.cgo_debug` | Setting cgo_debug | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.cgo_shader_ub_color` | Setting cgo_shader_ub_color | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.cgo_shader_ub_flags` | Setting cgo_shader_ub_flags | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.cgo_shader_ub_normal` | Setting cgo_shader_ub_normal | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.cgo_use_shader` | Setting cgo_use_shader | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.debug_pick` | Setting debug_pick | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.defer_builds_mode` | Setting defer_builds_mode | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.defer_updates` | Setting defer_updates | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.fast_idle` | Setting fast_idle | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.hash_max` | Setting hash_max | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.idle_delay` | Setting idle_delay | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.max_threads` | Setting max_threads | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.max_ups` | Setting max_ups | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.nb_spheres_use_shader` | Setting nb_spheres_use_shader | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.no_idle` | Setting no_idle | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.nonbonded_use_shader` | Setting nonbonded_use_shader | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.normal_workaround` | Setting normal_workaround | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.nvidia_bugs` | Setting nvidia_bugs | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.pick32bit` | Setting pick32bit | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.pick_shading` | Setting pick_shading | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.pickable` | Setting pickable | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.ray_hint_camera` | Setting ray_hint_camera | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.ray_triangle_fudge` | Setting ray_triangle_fudge | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.render_as_cylinders` | Setting render_as_cylinders | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.shaders_from_disk` | Setting shaders_from_disk | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.slow_idle` | Setting slow_idle | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.static_singletons` | Setting static_singletons | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.suspend_deferred` | Setting suspend_deferred | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.suspend_updates` | Setting suspend_updates | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.test1` | Setting test1 | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.test2` | Setting test2 | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.triangle_max_passes` | Setting triangle_max_passes | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.trilines` | Setting trilines | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.use_geometry_shaders` | Setting use_geometry_shaders | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.use_shaders` | Setting use_shaders | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `performance.setting.use_tessellation_shaders` | Setting use_tessellation_shaders | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `preset.performance_level` | Performance quality preset | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render_cache.clear` | Cache clear | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `render_cache.disable` | Cache disable | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `render_cache.enable` | Cache enable | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `render_cache.manage` | Precomputed result cache | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `render_cache.read_only` | Cache read_only | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `plugin.manager.discover_register` | Discover and register plugins | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | macos | 6 |
| `plugin.manager.install_file` | Install plugin from file | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `plugin.manager.legacy_tk` | Legacy Tk plugin host | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `plugin.manager.load` | Load plugin module | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | macos | 6 |
| `plugin.manager.menu_item` | Plugin menu item registration | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `plugin.manager.preferences` | Plugin manager preferences | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `plugin.manager.repository_fetch` | Fetch script from repository | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `plugin.manager.startup_path` | Plugin startup path | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `plugin.wizard.launch` | Launch wizard | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `plugin.wizard.refresh` | Refresh wizard | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `plugin.wizard.replace` | Replace wizard | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.background.gradient_colors` | Background top/bottom colors | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.background.gradient_mode` | Background gradient/grid mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.background.interactive_alpha` | Interactive/snapshot background alpha | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `color.background.ray_alpha` | Ray background alpha override | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `color.background.ray_alpha.mode.inherit` | Ray Opaque Background mode inherit | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.background.ray_alpha.mode.opaque` | Ray Opaque Background mode opaque | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.background.ray_alpha.mode.transparent` | Ray Opaque Background mode transparent | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.background.solid` | Solid background color | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.space.cmyk` | Color space cmyk | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.space.manage` | Display color space | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.space.pymol` | Color space pymol | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.space.rgb` | Color space rgb | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.image.dimension_units` | Physical image dimensions | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.dimension_units.mode.centimeters` | Image dimensions in centimeters | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.dimension_units.mode.inches` | Image dimensions in inches | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.dimension_units.mode.millimeters` | Image dimensions in millimeters | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.dimension_units.mode.pixels` | Image dimensions in pixels | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.filename_extension` | Image filename extension policy | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.format` | Image encoding selection | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.format.mode.guess` | Image format guess | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.format.mode.png` | Image format png | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.format.mode.ppm` | Image format ppm | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `export.image.memory_buffer` | Return encoded image buffer | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `export.image.prior_buffer` | Prior image-buffer policy | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.image.prior_buffer.mode.render_current` | Image prior-buffer mode render_current | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.image.prior_buffer.mode.require_prior` | Image prior-buffer mode require_prior | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.image.prior_buffer.mode.try_prior` | Image prior-buffer mode try_prior | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.image.source` | Image render-source selection | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `export.image.source.mode.ray` | Image source ray | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.source.mode.viewport_or_draw` | Image source viewport_or_draw | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.image.write` | Write rendered image | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `export.movie.frame_sequence` | Export movie frame sequence | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.movie.frame_sequence.dimensions` | Frame-sequence dimensions | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.frame_sequence.format` | Frame-sequence image format | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.frame_sequence.format.mode.png` | Frame-sequence format png | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.frame_sequence.format.mode.ppm` | Frame-sequence format ppm | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.frame_sequence.modal` | Frame-sequence modal execution | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.frame_sequence.preserve` | Frame-sequence preserve policy | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.frame_sequence.range` | Frame-sequence inclusive range | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.movie.frame_sequence.render_mode` | Frame-sequence render mode | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `export.movie.frame_sequence.render_mode.mode.default_setting` | Frame-sequence render mode default_setting | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.frame_sequence.render_mode.mode.draw` | Frame-sequence render mode draw | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.frame_sequence.render_mode.mode.normal` | Frame-sequence render mode normal | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.frame_sequence.render_mode.mode.ray` | Frame-sequence render mode ray | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video` | Encode movie video | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.encoder` | Movie encoder selection | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.encoder.mode.convert` | Movie encoder convert | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.encoder.mode.ffmpeg` | Movie encoder ffmpeg | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.encoder.mode.mpeg_encode` | Movie encoder mpeg_encode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.even_dimensions` | Movie even-dimension normalization | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.format` | Movie output format | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.format.mode.gif` | Movie format gif | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.format.mode.mov` | Movie format mov | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.format.mode.mp4` | Movie format mp4 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.format.mode.mpeg1` | Movie format mpeg1 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.format.mode.webm` | Movie format webm | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.frame_rate` | Movie frame rate | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.quality` | Movie encoding quality | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.movie.video.temp_frames` | Movie temporary-frame lifecycle | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.api.string` | Scene serialization string API | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.compile_feature_negotiation` | Scene exporter compile-feature negotiation | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.dispatch` | Scene export dispatch | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.external.gltf_conversion` | External COLLADA-to-glTF conversion | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.format` | Scene geometry format | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.format.mode.collada` | Scene format collada | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.format.mode.gltf` | Scene format gltf | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.format.mode.idtf` | Scene format idtf | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.format.mode.mtl` | Scene format mtl | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.format.mode.obj` | Scene format obj | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.format.mode.povray` | Scene format povray | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.format.mode.stl` | Scene format stl | P2 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `export.scene.format.mode.vrml1` | Scene format vrml1 | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `export.scene.format.mode.vrml2` | Scene format vrml2 | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `import.image.png.display` | Display PNG image | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `preset.modernize_rendering` | Modern rendering preset | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ambient_occlusion.manage` | Ambient-occlusion state | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ambient_occlusion.setting.ambient_occlusion_mode` | Ambient Occlusion Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ambient_occlusion.setting.ambient_occlusion_mode.mode.atom_map` | Ambient Occlusion Mode mode atom_map | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ambient_occlusion.setting.ambient_occlusion_mode.mode.disabled` | Ambient Occlusion Mode mode disabled | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ambient_occlusion.setting.ambient_occlusion_mode.mode.per_atom` | Ambient Occlusion Mode mode per_atom | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ambient_occlusion.setting.ambient_occlusion_mode.mode.vertex_map` | Ambient Occlusion Mode mode vertex_map | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ambient_occlusion.setting.ambient_occlusion_scale` | Ambient Occlusion Scale | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ambient_occlusion.setting.ambient_occlusion_smooth` | Ambient Occlusion Smooth | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.antialias.manage` | Antialias and color-correction state | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.antialias.setting.antialias` | Antialias | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.antialias.setting.antialias.mode.adaptive` | Antialias mode adaptive | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.antialias.setting.antialias.mode.none` | Antialias mode none | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.antialias.setting.antialias.mode.uniform_2x_adaptive` | Antialias mode uniform_2x_adaptive | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.antialias.setting.antialias.mode.uniform_3x_adaptive` | Antialias mode uniform_3x_adaptive | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.antialias.setting.antialias.mode.uniform_4x_adaptive` | Antialias mode uniform_4x_adaptive | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.antialias.setting.antialias_shader` | Antialias Shader | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.clipboard.copy` | Copy image to clipboard | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.color_correction.setting.gamma` | Gamma | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.auto_copy_images` | Auto Copy Images | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.batch_prefix` | Batch Prefix | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.bg_image_filename` | Bg Image Filename | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.bg_image_linear` | Bg Image Linear | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.bg_image_mode` | Bg Image Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.bg_image_mode.mode.center` | Bg Image Mode mode center | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.bg_image_mode.mode.fit` | Bg Image Mode mode fit | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.bg_image_mode.mode.stretch` | Bg Image Mode mode stretch | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.bg_image_mode.mode.tile` | Bg Image Mode mode tile | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.bg_image_tilesize` | Bg Image Tilesize | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.cache_frames` | Cache Frames | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.collada_background_box` | Collada Background Box | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.collada_export_lighting` | Collada Export Lighting | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.collada_geometry_mode` | Collada Geometry Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.collada_geometry_mode.mode.blender_polylist` | Collada Geometry Mode mode blender_polylist | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.collada_geometry_mode.mode.standard_141` | Collada Geometry Mode mode standard_141 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.draw_frames` | Draw Frames | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.geometry_export_mode` | Geometry Export Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.geometry_export_mode.mode.full_scene` | Geometry Export Mode mode full_scene | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.geometry_export_mode.mode.geometry_material_only` | Geometry Export Mode mode geometry_material_only | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.image_copy_always` | Image Copy Always | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.image_dots_per_inch` | Image Dots Per Inch | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.movie_fps` | Movie Fps | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.movie_quality` | Movie Quality | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.png_file_gamma` | Png File Gamma | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.png_screen_gamma` | Png Screen Gamma | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.ray_default_renderer` | Ray Default Renderer | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.ray_default_renderer.mode.builtin` | Ray Default Renderer mode builtin | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.ray_default_renderer.mode.default` | Ray Default Renderer mode default | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.ray_default_renderer.mode.povray` | Ray Default Renderer mode povray | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.ray_orthoscopic` | Ray Orthoscopic | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.ray_orthoscopic.mode.inherit` | Ray Orthoscopic mode inherit | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.ray_orthoscopic.mode.orthographic` | Ray Orthoscopic mode orthographic | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.ray_orthoscopic.mode.perspective` | Ray Orthoscopic mode perspective | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.ray_trace_frames` | Ray Trace Frames | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.show_alpha_checker` | Show Alpha Checker | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.export.setting.single_image` | Single Image | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.fog.manage` | Fog and depth-cue state | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.fog.setting.depth_cue` | Depth Cue | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.fog.setting.fog` | Fog | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.fog.setting.fog_start` | Fog Start | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.fog.setting.ray_trace_fog` | Ray Trace Fog | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.fog.setting.ray_trace_fog_start` | Ray Trace Fog Start | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.image.capture.window` | Capture complete application window | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `render.image.draw.aspect_preserving_size` | Draw aspect-preserving dimensions | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `render.image.draw.opengl` | Draw OpenGL image | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.image.draw.requires_graphics_context` | Draw graphics-context requirement | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.image.focal_blur` | Focal blur image | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.image.ray` | Ray-traced image | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.image.ray.antialias` | Ray antialias override | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.image.ray.aspect_preserving_size` | Ray aspect-preserving dimensions | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `render.image.ray.execution.async` | Asynchronous ray execution | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.image.ray.execution.synchronous` | Synchronous ray execution | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `render.image.ray.renderer` | Ray renderer selection | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.image.ray.renderer.mode.builtin` | Ray renderer builtin | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `render.image.ray.renderer.mode.default_setting` | Ray renderer default_setting | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.image.ray.renderer.mode.geometry_count` | Ray renderer geometry_count | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.image.ray.renderer.mode.povray` | Ray renderer povray | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.image.ray.stereo_pair` | Ray stereo angle and shift | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.image.ray.stops_animation` | Ray animation stop policy | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.manage` | Shared lighting state | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.lighting.panel` | Interactive lighting settings panel | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.lighting.ray_shadow_preset.black` | Ray-shadow preset black | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.ray_shadow_preset.heavy` | Ray-shadow preset heavy | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.ray_shadow_preset.light` | Ray-shadow preset light | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.ray_shadow_preset.manage` | Ray-shadow utility presets | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.ray_shadow_preset.matte` | Ray-shadow preset matte | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.ray_shadow_preset.medium` | Ray-shadow preset medium | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.ray_shadow_preset.none` | Ray-shadow preset none | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.ray_shadow_preset.occlusion` | Ray-shadow preset occlusion | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.ray_shadow_preset.occlusion2` | Ray-shadow preset occlusion2 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.ray_shadow_preset.soft` | Ray-shadow preset soft | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.ambient` | Ambient | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.lighting.setting.light_1` | Light | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.lighting.setting.light_2` | Light2 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.light_3` | Light3 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.light_4` | Light4 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.light_5` | Light5 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.light_6` | Light6 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.light_7` | Light7 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.light_8` | Light8 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.light_9` | Light9 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.light_count` | Light Count | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.lighting.setting.light_count.mode.extended_precomputed_or_ray` | Light count branch extended_precomputed_or_ray | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.light_count.mode.interactive_direct` | Light count branch interactive_direct | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.precomputed_lighting` | Precomputed Lighting | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.two_sided_lighting` | Two Sided Lighting | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.lighting.setting.two_sided_lighting.mode.auto` | Two Sided Lighting mode auto | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.two_sided_lighting.mode.both_faces` | Two Sided Lighting mode both_faces | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.lighting.setting.two_sided_lighting.mode.front_only` | Two Sided Lighting mode front_only | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.manage` | Material response state | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.material.preset.default` | Material preset default | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.preset.manage` | Material preset lifecycle | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.preset.metal` | Material preset metal | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.preset.plastic` | Material preset plastic | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.preset.rubber` | Material preset rubber | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.preset.xray` | Material preset xray | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.direct` | Direct | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.material.setting.power` | Power | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.ray_direct_shade` | Ray Direct Shade | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.ray_label_specular` | Ray Label Specular | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.ray_legacy_lighting` | Ray Legacy Lighting | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.ray_spec_local` | Ray Spec Local | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.reflect` | Reflect | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.material.setting.reflect_power` | Reflect Power | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.shininess` | Shininess | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.spec_count` | Spec Count | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.spec_direct` | Spec Direct | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.spec_direct_power` | Spec Direct Power | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.spec_power` | Spec Power | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.spec_reflect` | Spec Reflect | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.specular` | Specular | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.material.setting.specular_intensity` | Specular Intensity | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.manage` | Ray interior material state | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_color` | Ray Interior Color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_mode` | Ray Interior Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_mode.mode.ignore_triangle_interiors` | Ray Interior Mode mode ignore_triangle_interiors | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_mode.mode.normal` | Ray Interior Mode mode normal | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_reflect` | Ray Interior Reflect | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_texture` | Ray Interior Texture | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_texture.mode.inherit` | Ray Interior Texture mode inherit | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_texture.mode.texture_0` | Ray Interior Texture mode texture_0 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_texture.mode.texture_1` | Ray Interior Texture mode texture_1 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_texture.mode.texture_2` | Ray Interior Texture mode texture_2 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_texture.mode.texture_3` | Ray Interior Texture mode texture_3 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_texture.mode.texture_4` | Ray Interior Texture mode texture_4 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.interior.setting.ray_interior_texture.mode.texture_5` | Ray Interior Texture mode texture_5 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.setting.volume` | Ray Volume | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.manage` | Ray style and texture state | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_blend_blue` | Ray Blend Blue | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_blend_colors` | Ray Blend Colors | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_blend_green` | Ray Blend Green | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_blend_red` | Ray Blend Red | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_color_ramps` | Ray Color Ramps | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_oversample_cutoff` | Ray Oversample Cutoff | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_pixel_scale` | Ray Pixel Scale | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_texture` | Ray Texture | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_texture.mode.texture_0` | Ray Texture mode texture_0 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_texture.mode.texture_1` | Ray Texture mode texture_1 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_texture.mode.texture_2` | Ray Texture mode texture_2 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_texture.mode.texture_3` | Ray Texture mode texture_3 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_texture.mode.texture_4` | Ray Texture mode texture_4 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_texture.mode.texture_5` | Ray Texture mode texture_5 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_texture_settings` | Ray Texture Settings | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_color` | Ray Trace Color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_depth_factor` | Ray Trace Depth Factor | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_disco_factor` | Ray Trace Disco Factor | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_gain` | Ray Trace Gain | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_mode` | Ray Trace Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_mode.mode.color_outline` | Ray Trace Mode mode color_outline | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_mode.mode.monochrome_outline` | Ray Trace Mode mode monochrome_outline | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_mode.mode.normal` | Ray Trace Mode mode normal | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_mode.mode.quantized_outline` | Ray Trace Mode mode quantized_outline | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.ray.style.setting.ray_trace_slope_factor` | Ray Trace Slope Factor | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.shadow.manage` | Ray shadow state | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.shadow.setting.ray_clip_shadows` | Ray Clip Shadows | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.shadow.setting.ray_hint_shadow` | Ray Hint Shadow | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.shadow.setting.ray_improve_shadows` | Ray Improve Shadows | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.shadow.setting.ray_interior_shadows` | Ray Interior Shadows | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.shadow.setting.ray_shadow` | Ray Shadow | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.shadow.setting.ray_shadow_decay_factor` | Ray Shadow Decay Factor | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.shadow.setting.ray_shadow_decay_range` | Ray Shadow Decay Range | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.shadow.setting.ray_shadow_fudge` | Ray Shadow Fudge | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.manage` | Transparency compositing state | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.transparency.setting.ray_max_passes` | Ray Max Passes | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.ray_scatter` | Ray Scatter | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.ray_trace_persist_cutoff` | Ray Trace Persist Cutoff | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.ray_trace_trans_cutoff` | Ray Trace Trans Cutoff | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.ray_transparency_contrast` | Ray Transparency Contrast | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.ray_transparency_oblique` | Ray Transparency Oblique | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.ray_transparency_oblique_power` | Ray Transparency Oblique Power | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.ray_transparency_shadows` | Ray Transparency Shadows | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.ray_transparency_spec_cut` | Ray Transparency Spec Cut | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.ray_transparency_specular` | Ray Transparency Specular | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.transparency_global_sort` | Transparency Global Sort | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.transparency_mode` | Transparency Mode | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `render.transparency.setting.transparency_mode.mode.fast_sorted` | Transparency Mode mode fast_sorted | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.transparency_mode.mode.multi_layer` | Transparency Mode mode multi_layer | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.transparency_mode.mode.single_layer` | Transparency Mode mode single_layer | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.transparency_mode.mode.weighted_blended_oit` | Transparency Mode mode weighted_blended_oit | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.transparency_picking_mode` | Transparency Picking Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.transparency_picking_mode.mode.auto` | Transparency Picking Mode mode auto | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.transparency.setting.transparency_picking_mode.mode.pickable` | Transparency Picking Mode mode pickable | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `render.viewport.refresh` | Refresh interactive viewport | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `representation.surface.setting.transparency` | Transparency | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 3 |
| `setting.service.bond.get` | Get per-bond setting | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `setting.service.bond.set` | Set per-bond setting | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `setting.service.bond.unset` | Unset per-bond setting | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `setting.service.get` | Get setting value | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `setting.service.manage` | Setting service | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `setting.service.scope.atom` | Atom setting scope | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `setting.service.scope.global` | Global setting scope | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `setting.service.scope.object` | Object setting scope | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `setting.service.scope.state` | State setting scope | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `setting.service.set` | Set setting value | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `setting.service.unset` | Unset setting value | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `setting.service.unset_deep` | Deep unset of scoped settings | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `annotation.label_chains` | Label chains | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `annotation.label_segments` | Label segments | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.assignment.atom_selection` | Atom-selection color assignment | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 7 |
| `color.assignment.cache_invalidation` | Color cache invalidation | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.assignment.deep_reset` | Deep color reset and assignment | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.assignment.manage` | Color assignment | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 7 |
| `color.assignment.new_object_auto` | Automatic new-object carbon color | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 7 |
| `color.assignment.object_api` | Explicit object-color API | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.assignment.object_pattern` | Object-pattern color assignment | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 7 |
| `color.assignment.object_query` | Object-color query | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.desaturate` | Desaturate colors | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.input.hex_rgb` | Explicit hexadecimal RGB | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.input.named` | Named color lookup | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 6 |
| `color.input.numeric_index` | Numeric color-index lookup | P1 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.input.resolve` | Color token resolution | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 6 |
| `color.input.special.atomic` | Special color: atomic | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.input.special.auto` | Special color: auto | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.input.special.back` | Special color: back | P1 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `color.input.special.current` | Special color: current | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.input.special.default` | Special color: default | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.input.special.front` | Special color: front | P1 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `color.input.special.object` | Special color: object | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.preset.atomic_carbon` | Element colors with chosen carbon color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbab` | util.cbab (slate carbons) | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbac` | util.cbac (cyan carbons) | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbag` | util.cbag (carbon carbons) | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbak` | util.cbak (pink carbons) | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbam` | util.cbam (lightmagenta carbons) | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbao` | util.cbao (brightorange carbons) | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbap` | util.cbap (purple carbons) | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbas` | util.cbas (salmon carbons) | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbaw` | util.cbaw (hydrogen carbons) | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_carbon.cbay` | util.cbay (yellow carbons) | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_hydrogen` | Element colors with chosen hydrogen color | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.atomic_non_carbon` | Element colors for non-carbons | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.by_surface_area` | Color by surface area | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.chain_distinct` | Distinct color per chain | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.chainbow` | Rainbow within each chain | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.legacy_rainbow` | Legacy rainbow coloring | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.object_distinct` | Distinct color per object | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.preset.secondary_structure` | Color by secondary structure | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.ramp.automatic_range` | Automatic ramp range | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.ramp.builtin_palette` | Built-in ramp spectra | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `color.ramp.explicit_colors` | Explicit ramp colors and levels | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `color.ramp.export_samples` | Ramp sample export | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.ramp.manage` | Spatial color-ramp object | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `color.ramp.map` | Map-value color ramp | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `color.ramp.proximity` | Molecular-proximity color ramp | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.ramp.recursive_special_colors` | Recursive/special ramp colors | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.ramp.state_scope` | Ramp source-state scope | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.ramp.update` | Ramp range/color update | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `color.spectrum.arbitrary_expression` | Arbitrary expression spectrum | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 5 |
| `color.spectrum.builtin_palette` | Built-in spectrum palettes | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.spectrum.builtin_property` | Built-in numeric property spectrum | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 7 |
| `color.spectrum.by_residue` | Per-residue spectrum coloring | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.spectrum.explicit_colors` | Explicit spectrum color list | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.spectrum.interpolation` | Spectrum interpolation spaces | P1 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.spectrum.manage` | Property spectrum coloring | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.spectrum.nonnumeric_enumeration` | Nonnumeric spectrum enumeration | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 5 |
| `color.spectrum.range` | Spectrum range and clamping | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.table.builtin_catalog` | Built-in color catalog | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.table.define` | Custom color definition | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.table.define.fixed_mode` | Fixed custom color mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.table.define.replace` | Custom color replacement | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.table.define.rgb_255` | 8-bit RGB definition | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.table.define.rgb_normalized` | Normalized RGB definition | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.table.lut_clamp` | Color-table LUT clamping | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `color.table.query.index` | Color index query | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.table.query.indices` | Custom/all color index query | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.table.query.rgb` | Color RGB tuple query | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `color.table.session_persistence` | Color-table session persistence | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `display.roving.setting.roving_byres` | Setting roving_byres | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_cartoon` | Setting roving_cartoon | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_delay` | Setting roving_delay | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_detail` | Setting roving_detail | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_isomesh` | Setting roving_isomesh | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_isosurface` | Setting roving_isosurface | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_labels` | Setting roving_labels | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_lines` | Setting roving_lines | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_map1_level` | Setting roving_map1_level | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_map1_name` | Setting roving_map1_name | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_map2_level` | Setting roving_map2_level | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_map2_name` | Setting roving_map2_name | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_map3_level` | Setting roving_map3_level | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_map3_name` | Setting roving_map3_name | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_nb_spheres` | Setting roving_nb_spheres | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_nonbonded` | Setting roving_nonbonded | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_origin` | Setting roving_origin | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_origin_z` | Setting roving_origin_z | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_origin_z_cushion` | Setting roving_origin_z_cushion | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_polar_contacts` | Setting roving_polar_contacts | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_polar_cutoff` | Setting roving_polar_cutoff | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_ribbon` | Setting roving_ribbon | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_selection` | Setting roving_selection | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_spheres` | Setting roving_spheres | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `display.roving.setting.roving_sticks` | Setting roving_sticks | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.atom.dots` | PyMOL-compatible dots representation | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.atom.ellipsoid` | PyMOL-compatible ellipsoids representation | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.atom.nonbonded` | PyMOL-compatible nonbonded representation | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.atom.nonbonded_spheres` | PyMOL-compatible nb_spheres representation | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.atom.spheres` | PyMOL-compatible spheres representation | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.atom.spheres.backend.shader` | Sphere shader path | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.atom.spheres.color_override` | Sphere color override | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.atom.spheres.mode` | Sphere rendering mode | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.atom.spheres.mode.auto` | Automatic shader sphere mode | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.atom.spheres.mode.cube` | Cube sphere mode | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.point_fixed_normal` | Fixed point spheres with normals | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.point_fixed_square` | Fixed square point spheres | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.point_scaled_round` | Scaled round point spheres | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.point_scaled_round_normal` | Scaled round point spheres with normals | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.point_scaled_square` | Scaled square point spheres | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.point_scaled_square_normal` | Scaled square point spheres with normals | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.removed_4` | Removed sphere mode 4 fallback | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.removed_5` | Removed sphere mode 5 fallback | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.shader_impostor` | GLSL impostor sphere mode | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.atom.spheres.mode.tetrahedron` | Tetrahedron sphere mode | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.mode.triangles` | Triangle sphere mode | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.atom.spheres.point_max_size` | Maximum point-sphere size | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.point_size` | Fixed point-sphere size | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.quality` | Sphere tessellation quality | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.scale` | Sphere VDW radius scale | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.atom.spheres.solvent_radius` | Solvent-expanded sphere radius | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.atom.spheres.transparency` | Sphere transparency | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.backbone.cartoon` | PyMOL-compatible cartoon representation | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.backbone.cartoon.setting.all_alt` | Cartoon All Alt | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.color` | Cartoon Color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.cylindrical_helices` | Cartoon Cylindrical Helices | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.cylindrical_helices.mode.curved` | Cartoon Cylindrical Helices mode curved | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.cylindrical_helices.mode.off` | Cartoon Cylindrical Helices mode off | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.cylindrical_helices.mode.straight` | Cartoon Cylindrical Helices mode straight | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.debug` | Cartoon Debug | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.discrete_colors` | Cartoon Discrete Colors | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.dumbbell_length` | Cartoon Dumbbell Length | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.dumbbell_radius` | Cartoon Dumbbell Radius | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.dumbbell_width` | Cartoon Dumbbell Width | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.fancy_helices` | Cartoon Fancy Helices | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.fancy_sheets` | Cartoon Fancy Sheets | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.flat_cycles` | Cartoon Flat Cycles | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.flat_sheets` | Cartoon Flat Sheets | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.gap_cutoff` | Cartoon Gap Cutoff | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.backbone.cartoon.setting.helix_radius` | Cartoon Helix Radius | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.highlight_color` | Cartoon Highlight Color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ladder_color` | Cartoon Ladder Color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ladder_mode` | Cartoon Ladder Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ladder_mode.mode.off` | Cartoon Ladder Mode mode off | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ladder_mode.mode.on` | Cartoon Ladder Mode mode on | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ladder_radius` | Cartoon Ladder Radius | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.loop_cap` | Cartoon Loop Cap | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.loop_cap.mode.enum_flat` | Cartoon Loop Cap mode enum_flat | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.loop_cap.mode.enum_round` | Cartoon Loop Cap mode enum_round | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.loop_cap.mode.none` | Cartoon Loop Cap mode none | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.loop_quality` | Cartoon Loop Quality | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.loop_radius` | Cartoon Loop Radius | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_as_cylinders` | Cartoon Nucleic Acid As Cylinders | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_as_cylinders.mode.off` | Cartoon Nucleic Acid As Cylinders mode off | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_as_cylinders.mode.optimization` | Cartoon Nucleic Acid As Cylinders mode optimization | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_as_cylinders.mode.optimization_and_strands` | Cartoon Nucleic Acid As Cylinders mode optimization_and_strands | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_as_cylinders.mode.strand_geometry` | Cartoon Nucleic Acid As Cylinders mode strand_geometry | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_color` | Cartoon Nucleic Acid Color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_mode` | Cartoon Nucleic Acid Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_mode.mode.extend_3prime` | Cartoon Nucleic Acid Mode mode extend_3prime | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_mode.mode.extend_5prime` | Cartoon Nucleic Acid Mode mode extend_5prime | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_mode.mode.extend_both` | Cartoon Nucleic Acid Mode mode extend_both | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_mode.mode.phosphorus` | Cartoon Nucleic Acid Mode mode phosphorus | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.nucleic_acid_mode.mode.sugar` | Cartoon Nucleic Acid Mode mode sugar | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.oval_length` | Cartoon Oval Length | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.oval_quality` | Cartoon Oval Quality | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.oval_width` | Cartoon Oval Width | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.power` | Cartoon Power | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.power_b` | Cartoon Power B | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_quality` | Cartoon Putty Quality | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_radius` | Cartoon Putty Radius | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_range` | Cartoon Putty Range | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_scale_max` | Cartoon Putty Scale Max | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_scale_min` | Cartoon Putty Scale Min | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_scale_power` | Cartoon Putty Scale Power | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform` | Cartoon Putty Transform | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform.mode.absolute_linear` | Cartoon Putty Transform mode absolute_linear | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform.mode.absolute_nonlinear` | Cartoon Putty Transform mode absolute_nonlinear | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform.mode.implied_rms` | Cartoon Putty Transform mode implied_rms | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform.mode.normalized_linear` | Cartoon Putty Transform mode normalized_linear | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform.mode.normalized_nonlinear` | Cartoon Putty Transform mode normalized_nonlinear | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform.mode.relative_linear` | Cartoon Putty Transform mode relative_linear | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform.mode.relative_nonlinear` | Cartoon Putty Transform mode relative_nonlinear | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform.mode.scaled_linear` | Cartoon Putty Transform mode scaled_linear | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.putty_transform.mode.scaled_nonlinear` | Cartoon Putty Transform mode scaled_nonlinear | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.rect_length` | Cartoon Rect Length | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.backbone.cartoon.setting.rect_width` | Cartoon Rect Width | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.backbone.cartoon.setting.refine` | Cartoon Refine | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.refine_normals` | Cartoon Refine Normals | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.refine_tips` | Cartoon Refine Tips | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_color` | Cartoon Ring Color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_finder` | Cartoon Ring Finder | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_mode` | Cartoon Ring Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_mode.mode.center_sphere` | Cartoon Ring Mode mode center_sphere | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_mode.mode.edge_bounded` | Cartoon Ring Mode mode edge_bounded | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_mode.mode.ladder_only` | Cartoon Ring Mode mode ladder_only | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_mode.mode.round_edge` | Cartoon Ring Mode mode round_edge | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_mode.mode.size_sphere` | Cartoon Ring Mode mode size_sphere | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_mode.mode.square_edge` | Cartoon Ring Mode mode square_edge | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_radius` | Cartoon Ring Radius | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_transparency` | Cartoon Ring Transparency | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.ring_width` | Cartoon Ring Width | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.round_helices` | Cartoon Round Helices | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.sampling` | Cartoon Sampling | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.backbone.cartoon.setting.side_chain_helper` | Cartoon Side Chain Helper | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.smooth_cycles` | Cartoon Smooth Cycles | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.smooth_cylinder_cycles` | Cartoon Smooth Cylinder Cycles | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.smooth_cylinder_window` | Cartoon Smooth Cylinder Window | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.smooth_first` | Cartoon Smooth First | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.smooth_last` | Cartoon Smooth Last | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.smooth_loops` | Cartoon Smooth Loops | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.throw` | Cartoon Throw | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.trace_atoms` | Cartoon Trace Atoms | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.transparency` | Cartoon Transparency | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.tube_cap` | Cartoon Tube Cap | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.tube_cap.mode.enum_flat` | Cartoon Tube Cap mode enum_flat | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.tube_cap.mode.enum_round` | Cartoon Tube Cap mode enum_round | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.tube_cap.mode.none` | Cartoon Tube Cap mode none | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.tube_quality` | Cartoon Tube Quality | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.tube_radius` | Cartoon Tube Radius | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.setting.use_shader` | Cartoon Use Shader | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.style_assign` | Cartoon style assignment | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `representation.backbone.cartoon.type.arrow` | Cartoon type arrow | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.type.automatic` | Cartoon type automatic | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 7 |
| `representation.backbone.cartoon.type.cylinder` | Cartoon type cylinder | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.type.dash` | Cartoon type dash | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.type.dumbbell` | Cartoon type dumbbell | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.type.loop` | Cartoon type loop | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 7 |
| `representation.backbone.cartoon.type.oval` | Cartoon type oval | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.type.putty` | Cartoon type putty | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.type.rectangle` | Cartoon type rectangle | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 7 |
| `representation.backbone.cartoon.type.skip` | Cartoon type skip | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.cartoon.type.tube` | Cartoon type tube | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 7 |
| `representation.backbone.ribbon` | PyMOL-compatible ribbon representation | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.backbone.ribbon.setting.as_cylinders` | Ribbon As Cylinders | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.color` | Ribbon Color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.nucleic_acid_mode` | Ribbon Nucleic Acid Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.nucleic_acid_mode.mode.extend_3prime` | Ribbon Nucleic Acid Mode mode extend_3prime | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.nucleic_acid_mode.mode.extend_5prime` | Ribbon Nucleic Acid Mode mode extend_5prime | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.nucleic_acid_mode.mode.extend_both` | Ribbon Nucleic Acid Mode mode extend_both | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.nucleic_acid_mode.mode.phosphorus` | Ribbon Nucleic Acid Mode mode phosphorus | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.nucleic_acid_mode.mode.sugar` | Ribbon Nucleic Acid Mode mode sugar | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.power` | Ribbon Power | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.power_b` | Ribbon Power B | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.radius` | Ribbon Radius | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.sampling` | Ribbon Sampling | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.backbone.ribbon.setting.side_chain_helper` | Ribbon Side Chain Helper | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.throw` | Ribbon Throw | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.trace_atoms` | Ribbon Trace Atoms | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.transparency` | Ribbon Transparency | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.use_shader` | Ribbon Use Shader | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.backbone.ribbon.setting.width` | Ribbon Width | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.backbone.setting.trace_atoms_mode` | Trace Atoms Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.bond.lines` | PyMOL-compatible lines representation | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.lines.backend.cylinders` | Lines rendered as cylinders | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.lines.backend.shader` | Line shader path | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.lines.color_override` | Line color override | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.lines.radius_world` | Ray line radius | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.lines.smooth` | Line antialiasing | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.lines.stick_helper` | Line/stick overlap helper | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.lines.width_pixels` | Line width in pixels | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.shared.half_bonds` | Half-bond selection policy | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.shared.hide_long_bonds` | Long-bond hiding policy | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks` | PyMOL-compatible sticks representation | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.sticks.backend.cylinders` | Sticks rendered as cylinders | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.backend.debug` | Stick renderer debug mode | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.backend.shader` | Stick shader path | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.ball.color` | Stick ball color override | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.ball.enabled` | Stick ball-and-stick atoms | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.sticks.ball.ratio` | Stick ball radius ratio | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.sticks.color_override` | Stick color override | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.sticks.endpoint_nub` | Stick endpoint nub size | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.endpoint_overlap` | Stick endpoint overlap | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.endpoint_round_nub` | Rounded stick endpoint nubs | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.good_geometry` | High-quality stick geometry policy | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.hydrogen_radius_scale` | Hydrogen stick radius scale | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.sticks.quality` | Stick radial tessellation quality | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.radius` | Stick cylinder radius | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.sticks.transparency` | Stick transparency | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.sticks.valence.fixed_radius` | Fixed-radius valence sticks | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.sticks.valence.scale` | Stick valence radius scale | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.valence.enabled` | Bond-order valence display | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.valence.layout` | Line valence layout mode | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.valence.layout.fancy` | Neighbor-aware valence layout | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.valence.layout.simple` | Simple valence layout | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.valence.spacing` | Line valence spacing | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.valence.zero_order.dashed` | Dashed zero-order bonds | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.valence.zero_order.mode` | Zero-order bond appearance | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.bond.valence.zero_order.scale` | Zero-order bond radius scale | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.valence.zero_order.skip` | Skip zero-order bonds | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.bond.valence.zero_order.solid` | Solid zero-order bonds | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.cache.rebuild` | Representation geometry rebuild | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `representation.cache.rebuild.representation_scope` | Type-scoped representation rebuild | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.cache.rebuild.selection_scope` | Selection-scoped representation rebuild | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.color.recolor` | Representation color-cache refresh | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.color.recolor.representation_scope` | Type-scoped recolor | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.color.recolor.selection_scope` | Selection-scoped recolor | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.crystal.unit_cell` | PyMOL-compatible cell representation | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.custom.callback` | PyMOL-compatible callback representation | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.custom.cgo` | PyMOL-compatible cgo representation | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.defaults.auto_show_lines` | Auto-show lines on load | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.defaults.auto_show_nonbonded` | Auto-show nonbonded atoms on load | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.defaults.auto_show_spheres` | Auto-show spheres on load | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.dot.setting.as_spheres` | Dot As Spheres | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.color` | Dot Color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.density` | Dot Density | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.density.mode.density_0` | Dot Density mode density_0 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.density.mode.density_1` | Dot Density mode density_1 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.density.mode.density_2` | Dot Density mode density_2 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.density.mode.density_3` | Dot Density mode density_3 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.density.mode.density_4` | Dot Density mode density_4 | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.hydrogens` | Dot Hydrogens | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.lighting` | Dot Lighting | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.mode` | Dot Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.normals` | Dot Normals | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.radius` | Dot Radius | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.solvent` | Dot Solvent | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.trim_dots` | Trim Dots | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.use_shader` | Dot Use Shader | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.dot.setting.width` | Dot Width | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.atom` | PyMOL-compatible labels representation | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `representation.label.content.clear` | Clear selected atom labels | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `representation.label.content.expression` | Evaluated atom label expression | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `representation.label.content.literal` | Literal atom label assignment | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `representation.label.content.property_namespace` | Label expression atom-property namespace | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.drag.atom` | Drag atom label | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `representation.label.setting.anchor` | Label Anchor | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.bg_color` | Label Bg Color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.bg_outline` | Label Bg Outline | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.bg_transparency` | Label Bg Transparency | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.color` | Label Color | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `representation.label.setting.connector` | Label Connector | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.connector_color` | Label Connector Color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.connector_ext_length` | Label Connector Ext Length | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.connector_mode` | Label Connector Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.connector_mode.mode.adaptive_bent` | Label Connector Mode mode adaptive_bent | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.connector_mode.mode.bent_lower_corner` | Label Connector Mode mode bent_lower_corner | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.connector_mode.mode.center_to_box` | Label Connector Mode mode center_to_box | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.connector_mode.mode.closest_box_point` | Label Connector Mode mode closest_box_point | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.connector_mode.mode.closest_corner_or_edge` | Label Connector Mode mode closest_corner_or_edge | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.connector_width` | Label Connector Width | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.digits` | Label Digits | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `representation.label.setting.float_labels` | Float Labels | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id` | Label Font Id | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `representation.label.setting.font_id.mode.gentium_italic` | Label Font Id mode gentium_italic | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.gentium_roman` | Label Font Id mode gentium_roman | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.legacy_fallback_default` | Label Font Id mode legacy_fallback_default | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.mono` | Label Font Id mode mono | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.mono_bold` | Label Font Id mode mono_bold | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.mono_bold_oblique` | Label Font Id mode mono_bold_oblique | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.mono_oblique` | Label Font Id mode mono_oblique | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.sans` | Label Font Id mode sans | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.sans_bold` | Label Font Id mode sans_bold | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.sans_bold_oblique` | Label Font Id mode sans_bold_oblique | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.sans_oblique` | Label Font Id mode sans_oblique | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.serif` | Label Font Id mode serif | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.serif_bold` | Label Font Id mode serif_bold | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.serif_bold_oblique` | Label Font Id mode serif_bold_oblique | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.font_id.mode.serif_oblique` | Label Font Id mode serif_oblique | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.multiline_justification` | Label Multiline Justification | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.multiline_spacing` | Label Multiline Spacing | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.outline_color` | Label Outline Color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.padding` | Label Padding | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.pick_labels` | Pick Labels | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.pick_labels.mode.disabled` | Pick Labels mode disabled | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.pick_labels.mode.labels_and_geometry` | Pick Labels mode labels_and_geometry | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.pick_labels.mode.labels_only` | Pick Labels mode labels_only | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.placement_offset` | Label Placement Offset | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.position` | Label Position | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `representation.label.setting.position.mode.alignment` | Label Position mode alignment | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.position.mode.camera_displacement` | Label Position mode camera_displacement | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.position.mode.world_z_offset` | Label Position mode world_z_offset | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.ray_connector_flat` | Ray Label Connector Flat | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.relative_mode` | Label Relative Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.relative_mode.mode.normalized_screen` | Label Relative Mode mode normalized_screen | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.relative_mode.mode.pixel_screen` | Label Relative Mode mode pixel_screen | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.relative_mode.mode.world` | Label Relative Mode mode world | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.screen_point` | Label Screen Point | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.shadow_mode` | Label Shadow Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.shadow_mode.mode.cast` | Label Shadow Mode mode cast | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.shadow_mode.mode.cast_and_catch` | Label Shadow Mode mode cast_and_catch | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.shadow_mode.mode.catch` | Label Shadow Mode mode catch | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.shadow_mode.mode.none` | Label Shadow Mode mode none | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.size` | Label Size | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 4 |
| `representation.label.setting.size.mode.screen_points` | Label Size mode screen_points | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.size.mode.world_angstrom` | Label Size mode world_angstrom | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.z_target` | Label Z Target | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.z_target.mode.auto_connector` | Label Z Target mode auto_connector | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.z_target.mode.disabled` | Label Z Target mode disabled | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.label.setting.z_target.mode.enabled` | Label Z Target mode enabled | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.contour.carve` | Map contour atom carve | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `representation.map.contour.create` | Map contour object creation | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.map.contour.gradient` | Map gradient object | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.contour.isodot` | Map isodot object | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.contour.isomesh` | Map isomesh object | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `representation.map.contour.isosurface` | Map isosurface object | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `representation.map.contour.object_update_policy` | Map contour object update policy | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.map.contour.selection_extent` | Map contour selection extent | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.map.contour.source_state` | Map contour source-state semantics | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.contour.target_state` | Map contour target-state semantics | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.gradient.setting.max_length` | Gradient Max Length | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.gradient.setting.min_length` | Gradient Min Length | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.gradient.setting.min_slope` | Gradient Min Slope | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.gradient.setting.normal_min_dot` | Gradient Normal Min Dot | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.gradient.setting.spacing` | Gradient Spacing | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.gradient.setting.step_size` | Gradient Step Size | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.gradient.setting.symmetry` | Gradient Symmetry | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.isolevel.manage` | Contour isolevel management | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.map.isolevel.query` | Contour isolevel query | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.map.isolevel.update` | Contour isolevel update | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.map.isosurface.mode.dots` | Isosurface mode dots | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.isosurface.mode.lines` | Isosurface mode lines | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.isosurface.mode.triangles_gradient_normals` | Isosurface mode triangles_gradient_normals | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.map.isosurface.mode.triangles_triangle_normals` | Isosurface mode triangles_triangle_normals | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.isosurface.setting.algorithm` | Isosurface Algorithm | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 3 |
| `representation.map.isosurface.setting.algorithm.mode.marching_cubes_basic` | Isosurface Algorithm mode marching_cubes_basic | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.isosurface.setting.algorithm.mode.marching_cubes_vtkm` | Isosurface Algorithm mode marching_cubes_vtkm | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.isosurface.setting.algorithm.mode.marching_tetrahedra` | Isosurface Algorithm mode marching_tetrahedra | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.map.isosurface.side.back` | Isosurface side back | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.isosurface.side.front` | Isosurface side front | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.map.mesh` | PyMOL-compatible mesh representation | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.measurement.angle` | PyMOL-compatible angles representation | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `representation.measurement.angle.setting.color` | Angle Color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.angle.setting.label_digits` | Label Angle Digits | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.angle.setting.label_position` | Angle Label Position | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.angle.setting.size` | Angle Size | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.dihedral` | PyMOL-compatible dihedrals representation | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `representation.measurement.dihedral.setting.color` | Dihedral Color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.dihedral.setting.label_digits` | Label Dihedral Digits | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.dihedral.setting.label_position` | Dihedral Label Position | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.dihedral.setting.size` | Dihedral Size | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes` | PyMOL-compatible dashes representation | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `representation.measurement.distance_dashes.backend_negotiation` | Distance-dash line/cylinder backend | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes.backend_negotiation.mode.cylinders` | Distance-dash backend cylinders | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes.backend_negotiation.mode.lines` | Distance-dash backend lines | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes.setting.as_cylinders` | Dash As Cylinders | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes.setting.color` | Dash Color | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.measurement.distance_dashes.setting.gap` | Dash Gap | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `representation.measurement.distance_dashes.setting.label_digits` | Label Distance Digits | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes.setting.length` | Dash Length | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `representation.measurement.distance_dashes.setting.radius` | Dash Radius | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes.setting.round_ends` | Dash Round Ends | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes.setting.transparency` | Dash Transparency | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes.setting.use_shader` | Dash Use Shader | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.distance_dashes.setting.width` | Dash Width | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 5 |
| `representation.measurement.label.drag` | Drag measurement label | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.measurement.label.numeric_format` | Measurement numeric-label formatting | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 3 |
| `representation.mesh.setting.as_cylinders` | Mesh As Cylinders | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.carve_cutoff` | Mesh Carve Cutoff | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.carve_selection` | Mesh Carve Selection | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.carve_state` | Mesh Carve State | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.clear_cutoff` | Mesh Clear Cutoff | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.clear_selection` | Mesh Clear Selection | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.clear_state` | Mesh Clear State | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.color` | Mesh Color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.cutoff` | Mesh Cutoff | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.grid_max` | Mesh Grid Max | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.lighting` | Mesh Lighting | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.mode` | Mesh Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.mode.mode.all` | Mesh Mode mode all | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.mode.mode.flags` | Mesh Mode mode flags | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.mode.mode.heavy_atoms` | Mesh Mode mode heavy_atoms | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.negative_color` | Mesh Negative Color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.negative_visible` | Mesh Negative Visible | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.normals` | Mesh Normals | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.quality` | Mesh Quality | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.radius` | Mesh Radius | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.skip` | Mesh Skip | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.solvent` | Mesh Solvent | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.type` | Mesh Type | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.type.mode.points` | Mesh Type mode points | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.type.mode.wire` | Mesh Type mode wire | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.use_shader` | Mesh Use Shader | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.mesh.setting.width` | Mesh Width | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.alignment_as_cylinders` | Setting alignment_as_cylinders | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.auto_color_next` | Setting auto_color_next | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.auto_show_classified` | Setting auto_show_classified | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cell_centered` | Setting cell_centered | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cell_color` | Setting cell_color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cgo_dot_radius` | Setting cgo_dot_radius | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cgo_dot_width` | Setting cgo_dot_width | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cgo_ellipsoid_quality` | Setting cgo_ellipsoid_quality | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cgo_lighting` | Setting cgo_lighting | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cgo_line_radius` | Setting cgo_line_radius | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cgo_line_width` | Setting cgo_line_width | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cgo_ray_width_scale` | Setting cgo_ray_width_scale | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cgo_sphere_quality` | Setting cgo_sphere_quality | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cgo_transparency` | Setting cgo_transparency | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.chromadepth` | Setting chromadepth | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.cone_quality` | Setting cone_quality | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.dist_counter` | Setting dist_counter | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.draw_mode` | Setting draw_mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.dynamic_width` | Setting dynamic_width | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.dynamic_width_factor` | Setting dynamic_width_factor | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.dynamic_width_max` | Setting dynamic_width_max | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.dynamic_width_min` | Setting dynamic_width_min | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.ellipsoid_color` | Setting ellipsoid_color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.ellipsoid_probability` | Setting ellipsoid_probability | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.ellipsoid_quality` | Setting ellipsoid_quality | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.ellipsoid_scale` | Setting ellipsoid_scale | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.ellipsoid_transparency` | Setting ellipsoid_transparency | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.isomesh_auto_state` | Setting isomesh_auto_state | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.map_auto_expand_sym` | Setting map_auto_expand_sym | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.nb_spheres_quality` | Setting nb_spheres_quality | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.nb_spheres_size` | Setting nb_spheres_size | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.nonbonded_as_cylinders` | Setting nonbonded_as_cylinders | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.nonbonded_size` | Setting nonbonded_size | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.nonbonded_transparency` | Setting nonbonded_transparency | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.pymol_space_max_blue` | Setting pymol_space_max_blue | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.pymol_space_max_green` | Setting pymol_space_max_green | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.pymol_space_max_red` | Setting pymol_space_max_red | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.pymol_space_min_factor` | Setting pymol_space_min_factor | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.ramp_blend_nearby_colors` | Setting ramp_blend_nearby_colors | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.smooth_color_triangle` | Setting smooth_color_triangle | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.smooth_half_bonds` | Setting smooth_half_bonds | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.spheroid_fill` | Setting spheroid_fill | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.spheroid_scale` | Setting spheroid_scale | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.misc.setting.spheroid_smooth` | Setting spheroid_smooth | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.object.extent` | PyMOL-compatible extent representation | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.preset.licorice` | PyMOL-compatible licorice representation | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.preset.wire` | PyMOL-compatible wire representation | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `unwritten` | — | 0 |
| `representation.slice.setting.dynamic_grid` | Slice Dynamic Grid | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.slice.setting.dynamic_grid_resolution` | Slice Dynamic Grid Resolution | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.slice.setting.grid` | Slice Grid | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.slice.setting.height_map` | Slice Height Map | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.slice.setting.height_scale` | Slice Height Scale | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.slice.setting.track_camera` | Slice Track Camera | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.molecular` | PyMOL-compatible surface representation | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `unwritten` | — | 1 |
| `representation.surface.setting.best` | Surface Best | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.carve_cutoff` | Surface Carve Cutoff | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.carve_normal_cutoff` | Surface Carve Normal Cutoff | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.carve_selection` | Surface Carve Selection | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.carve_state` | Surface Carve State | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.cavity_cutoff` | Surface Cavity Cutoff | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.cavity_mode` | Surface Cavity Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.cavity_radius` | Surface Cavity Radius | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.circumscribe` | Surface Circumscribe | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.clear_cutoff` | Surface Clear Cutoff | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.clear_selection` | Surface Clear Selection | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.clear_state` | Surface Clear State | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.color` | Surface Color | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 3 |
| `representation.surface.setting.color_smoothing` | Surface Color Smoothing | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.color_smoothing_threshold` | Surface Color Smoothing Threshold | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.debug` | Surface Debug | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.miserable` | Surface Miserable | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.mode` | Surface Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.mode.mode.all` | Surface Mode mode all | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.mode.mode.flags` | Surface Mode mode flags | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.mode.mode.heavy_atoms` | Surface Mode mode heavy_atoms | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.mode.mode.visible` | Surface Mode mode visible | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.mode.mode.visible_heavy_atoms` | Surface Mode mode visible_heavy_atoms | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.negative_color` | Surface Negative Color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.negative_visible` | Surface Negative Visible | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.normal` | Surface Normal | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.optimize_subsets` | Surface Optimize Subsets | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.pick_surface` | Pick Surface | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.poor` | Surface Poor | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.proximity` | Surface Proximity | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.quality` | Surface Quality | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.ramp_above_mode` | Surface Ramp Above Mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.residue_cutoff` | Surface Residue Cutoff | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.smooth_edges` | Surface Smooth Edges | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.solvent` | Surface Solvent | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.trim_cutoff` | Surface Trim Cutoff | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.trim_factor` | Surface Trim Factor | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.type` | Surface Type | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.type.mode.completely_scribed` | Surface Type mode completely_scribed | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.type.mode.deterministic_alt_weighting` | Surface Type mode deterministic_alt_weighting | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.type.mode.deterministic_solid` | Surface Type mode deterministic_solid | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.type.mode.dots_conventional` | Surface Type mode dots_conventional | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.type.mode.pairwise_scribed` | Surface Type mode pairwise_scribed | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.type.mode.solid_conventional` | Surface Type mode solid_conventional | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.type.mode.triangle_mesh_conventional` | Surface Type mode triangle_mesh_conventional | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.setting.use_shader` | Surface Use Shader | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.shared.setting.cavity_cull` | Cavity Cull | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.shared.setting.min_mesh_spacing` | Min Mesh Spacing | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.surface.shared.setting.solvent_radius` | Solvent Radius | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.visibility.argument.selection_first` | Legacy selection-first disambiguation | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.visibility.as_exclusive` | Exclusive representation replacement | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.composite.licorice` | Licorice composite visibility | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.composite.wire` | Wire composite visibility | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.default.as_wire` | No-argument as default | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.visibility.default.hide_everything` | No-argument hide default | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.visibility.default.show_wire` | No-argument show default | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.visibility.hide` | Representation hide | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.independent_from_object` | Representation/object visibility independence | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.manage` | Per-selection representation visibility model | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.mask.everything` | All-representations aggregate mask | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.mask.multiple` | Multiple-representation mask | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `representation.visibility.nonmolecular_object` | Non-molecular object representation visibility | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.visibility.scope.name_pattern` | Object/name-pattern visibility scope | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.visibility.scope.selection` | Atom-selection visibility scope | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.show_additive` | Additive representation show | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.toggle_all_or_none` | All-or-none representation toggle | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `representation.visibility.toggle_object` | Object visibility through toggle special case | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.carve` | Direct volume atom carve | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.create` | Direct volume object creation | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.volume.direct` | PyMOL-compatible volume representation | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `unwritten` | — | 2 |
| `representation.volume.legacy_numeric_ramp` | Legacy numeric volume ramp argument | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.object_overwrite` | Direct volume overwrite policy | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `representation.volume.ramp.builtin_presets` | Built-in volume ramp presets | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.volume.ramp.define` | Volume ramp definition | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `representation.volume.ramp.get` | Volume ramp query | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `representation.volume.ramp.panel` | Interactive volume ramp editor | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.ramp.set` | Volume ramp assignment | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.volume.selection_extent` | Direct volume selection extent | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.setting.bit_depth` | Volume Bit Depth | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.setting.data_range` | Volume Data Range | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.setting.layers` | Volume Layers | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.setting.mode` | Volume Mode | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.volume.setting.mode.mode.post_classified` | Volume Mode mode post_classified | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.volume.setting.mode.mode.pre_integrated` | Volume Mode mode pre_integrated | P1 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `representation.volume.slice` | PyMOL-compatible slice representation | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `unwritten` | — | 2 |
| `representation.volume.slice.create` | Map slice object creation | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `representation.volume.slice.source_state` | Slice source-state semantics | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.slice.target_state` | Slice target-state semantics | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.source_state` | Direct volume source-state semantics | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation.volume.target_state` | Direct volume target-state semantics | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.b_factor_putty` | Preset b_factor_putty | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.ball_and_stick` | Preset ball_and_stick | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.classified` | Preset classified | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.default` | Preset default | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.interface` | Preset interface | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.ligand_cartoon` | Preset ligand_cartoon | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.ligand_sites` | Preset ligand_sites | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.ligand_sites_dots` | Preset ligand_sites_dots | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.ligand_sites_hq` | Preset ligand_sites_hq | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.ligand_sites_mesh` | Preset ligand_sites_mesh | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.ligand_sites_trans` | Preset ligand_sites_trans | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.ligand_sites_trans_hq` | Preset ligand_sites_trans_hq | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.ligands` | Preset ligands | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.manage` | Representation presets | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.pretty` | Preset pretty | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.pretty_solv` | Preset pretty_solv | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.pub_solv` | Preset pub_solv | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.publication` | Preset publication | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.simple` | Preset simple | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.simple_no_solv` | Preset simple_no_solv | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `representation_preset.technical` | Preset technical | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `annotation.callout` | Screen-stabilized callout | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `annotation.curve.create` | Create curve object | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.clip.far_absolute` | Set far clip plane absolutely | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.clip.far_relative` | Move far clip plane relatively | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.clip.fit_atoms` | Fit clipping planes to selected atoms | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.clip.manage` | Camera clipping-slab management | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.clip.move_slab` | Move both clipping planes | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.clip.near_absolute` | Set near clip plane absolutely | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.clip.near_relative` | Move near clip plane relatively | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.clip.query` | Query clipping-plane positions | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `camera.clip.slab_thickness` | Set clipping-slab thickness | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.frame.center` | Center camera on a selection | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.frame.center.animate` | Animated center transition | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.frame.center.move_origin` | Center rotation-origin policy | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.frame.center.state_scope` | Center coordinate-state scope | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `camera.frame.orient` | Principal-axis camera orientation | P1 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.frame.orient.animate` | Animated orientation transition | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.frame.orient.state_scope` | Orient coordinate-state scope | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.frame.zoom` | Frame a selection by zooming | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.frame.zoom.animate` | Animated zoom transition | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.frame.zoom.buffer` | Zoom framing buffer | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.frame.zoom.complete` | Complete no-center-clipping zoom | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.frame.zoom.state_scope` | Zoom coordinate-state scope | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 3 |
| `camera.named_view.clear_all` | Clear or list all named camera views | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `camera.named_view.delete` | Delete a named camera view | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `camera.named_view.manage` | Named camera-view lifecycle | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `camera.named_view.recall` | Recall a named camera view | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `camera.named_view.store` | Store a named camera view | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `camera.navigation.move_axis` | Translate camera along a primary axis | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.navigation.rock` | Continuous Y-axis camera rocking | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.navigation.turn_axis` | Rotate camera about a primary axis | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.origin.from_selection` | Rotation origin from selection | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.origin.manage` | Rotation-origin management | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.origin.object_scope` | Per-object rotation origin | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.origin.position` | Explicit rotation-origin position | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.origin.state_scope` | Rotation-origin coordinate-state scope | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.projection.field_of_view` | Vertical field-of-view control | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.projection.orthographic` | Perspective or orthographic projection | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.reset.global` | Reset global camera and framing | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.reset.object_transform` | Reset per-object transform | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `camera.state.camera_origin` | Camera-space rotation-origin state | P0 | 검증됨 (`compatible`) | 예 | — | — | `compatible` | macos | 4 |
| `camera.state.clip_planes` | Front and rear clip-plane state | P0 | 검증됨 (`compatible`) | 예 | — | — | `compatible` | macos | 4 |
| `camera.state.manage` | Complete camera-state model | P0 | 검증됨 (`compatible`) | 예 | — | — | `compatible` | macos | 4 |
| `camera.state.model_origin` | Model-space rotation-origin state | P0 | 검증됨 (`compatible`) | 예 | — | — | `compatible` | macos | 4 |
| `camera.state.projection_encoding` | Projection and field-of-view view encoding | P1 | 검증됨 (`compatible`) | 예 | — | — | `compatible` | macos | 4 |
| `camera.state.restore_18` | Restore the 18-value PyMOL view | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.state.rotation` | Model-to-camera rotation state | P0 | 검증됨 (`compatible`) | 예 | — | — | `compatible` | macos | 4 |
| `camera.state.serialize_18` | Serialize the 18-value PyMOL view | P0 | 검증됨 (`compatible`) | 예 | 예 | 예 | `compatible` | macos | 4 |
| `camera.stereo.anaglyph` | Anaglyph stereo mode | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.anaglyph_mode` | Anaglyph color-combination policy | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.angle` | Stereo angular-separation scale | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.checkerboard` | Checkerboard stereo mode | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.chromadepth` | Chromadepth stereo mode | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.stereo.clone_dynamic` | Cloned dynamic stereo mode | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.stereo.column_interleaved` | Column-interleaved stereo mode | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.crosseye` | Cross-eye stereo mode | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.custom` | Custom stereo mode | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.stereo.double_pump_mono` | Stereo-context monoscopic double pumping | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.stereo.dynamic` | Dynamic stereo mode | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.stereo.dynamic_strength` | Dynamic stereo strength | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.stereo.enabled` | Stereo enable/disable state | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.geowall` | GeoWall stereo mode | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.stereo.manage` | Stereoscopic display-mode management | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.openvr` | OpenVR stereo mode | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.stereo.quadbuffer` | Quad-buffered stereo mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `camera.stereo.row_interleaved` | Row-interleaved stereo mode | P2 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.shift` | Stereo camera separation | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.side_by_side` | Side-by-side stereo mode | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.swap` | Swap stereo eyes | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `camera.stereo.walleye` | Wall-eye stereo mode | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 1 |
| `camera.viewport.query` | Query graphics viewport dimensions | P1 | 진행 중(미검증) (`partial`) | 예 | — | — | `specified` | — | 2 |
| `camera.viewport.set` | Set graphics viewport dimensions | P1 | 진행 중(미검증) (`partial`) | 예 | — | — | `specified` | — | 2 |
| `scene.message.get` | Query a scene message | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `scene.message.set` | Set a scene message | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `scene.navigate.first` | Recall the first ordered scene | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.navigate.next` | Recall the next ordered scene | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.navigate.previous` | Recall the previous ordered scene | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.order.manage` | Ordered scene collection management | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.order.move_top` | Move scenes to the top | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.order.natural_sort` | Natural-sort scene names | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.order.relative` | Move scenes at their current relative location | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.query.list` | Query ordered scene names | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `scene.query.thumbnail` | Query a scene thumbnail | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `scene.snapshot.component.activity` | Scene object-activity channel | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `scene.snapshot.component.color` | Scene color channel | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.snapshot.component.frame` | Scene global-frame channel | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `scene.snapshot.component.message` | Scene playback-message channel | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `scene.snapshot.component.representation` | Scene representation-visibility channel | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `scene.snapshot.component.selection_scope` | Scene atom-channel selection scope | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `scene.snapshot.component.thumbnail` | Scene thumbnail channel | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `scene.snapshot.component.view` | Scene camera-view channel | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `scene.snapshot.delete` | Delete one or all scene snapshots | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `scene.snapshot.insert_after` | Store current state after the current scene | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.snapshot.insert_before` | Store current state before the current scene | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.snapshot.manage` | Named scene snapshot lifecycle | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `scene.snapshot.recall` | Recall a named scene snapshot | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `scene.snapshot.rename` | Rename a scene snapshot | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `scene.snapshot.store` | Store a named scene snapshot | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `scene.snapshot.update` | Update the current scene while preserving its message | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `view.query.position` | Query view center position | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.all_states` | Setting all_states | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.animation` | Setting animation | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.animation_duration` | Setting animation_duration | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.auto_zoom` | Setting auto_zoom | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.motion_bias` | Setting motion_bias | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.motion_hand` | Setting motion_hand | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.motion_linear` | Setting motion_linear | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.motion_power` | Setting motion_power | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.motion_simple` | Setting motion_simple | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.rock` | Setting rock | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.rock_delay` | Setting rock_delay | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.scene_animation` | Setting scene_animation | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.scene_animation_duration` | Setting scene_animation_duration | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.scene_current_name` | Setting scene_current_name | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.scene_frame_mode` | Setting scene_frame_mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.scene_loop` | Setting scene_loop | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.scenes_changed` | Setting scenes_changed | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.state` | Setting state | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.state_counter_mode` | Setting state_counter_mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.sweep_angle` | Setting sweep_angle | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.sweep_mode` | Setting sweep_mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.sweep_phase` | Setting sweep_phase | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `view.setting.sweep_speed` | Setting sweep_speed | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.appearance.cartoon_color` | Cartoon-color selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.appearance.color` | Atom-color selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.appearance.enabled` | Enabled-object atom selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.appearance.representation` | Representation-visibility selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.appearance.ribbon_color` | Ribbon-color selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.appearance.visible` | Visible-atom selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.acceptor` | Hydrogen-bond acceptor selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.backbone` | Polymer backbone selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.donor` | Hydrogen-bond donor selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.guide` | Polymer guide-atom selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.hbond_acceptor_short` | Hydrogen-bond acceptor shorthand | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.hbond_donor_short` | Hydrogen-bond donor shorthand | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.hetatm` | HETATM-origin selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.hydrogen` | Hydrogen selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.inorganic` | Non-polymer inorganic selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.metal` | Metal selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.nucleic` | Nucleic-polymer selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.organic` | Non-polymer organic selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.polymer` | Polymer selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.protein` | Protein-polymer selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.sidechain` | Polymer sidechain selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.chemical.solvent` | Solvent selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.comparison.equal` | Numeric equality comparison | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.comparison.greater_than` | Greater-than numeric comparison | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.comparison.less_than` | Less-than numeric comparison | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.comparison.range` | Numeric range/list comparison | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.constant.all` | All atoms | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.constant.none` | Empty atom set | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.coordinate.present` | Current-state coordinate presence | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.coordinate.state` | Coordinate-state selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.bond_steps` | Expand by bond steps | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.bound_to` | Direct bonded atoms including overlap | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.calpha` | Reduce expanded residues to C-alpha atoms | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.chain` | Expand to chains | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.fragment` | Expand to fragments | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.molecule` | Expand to bonded molecules | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.neighbor` | Direct bonded neighbors excluding source | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.object` | Expand to objects | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.residue` | Expand to residues | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.ring` | Expand to small rings | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.segment` | Expand to segments | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.expansion.unit_cell` | Expand to crystallographic unit cell | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.flag.action.clear` | Atom flag clear | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.flag.action.reset` | Atom flag reset | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.flag.action.set` | Atom flag set | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.flag.assign` | Atom flag assignment | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.identifier.altloc` | Alternate-location selector | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.atom_name` | Atom-name selector | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.chain` | Chain selector | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.element` | Element selector | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.index` | Current object atom-index selector | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.label` | Atom-label selector | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.object` | Object/model selector | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.peptide_sequence` | Peptide-sequence selector | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.rank` | Load-order atom-rank selector | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.residue_id` | Residue-identifier selector | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.residue_name` | Residue-name selector | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.segment` | Segment selector | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.identifier.source_id` | Source atom-ID selector | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.logical.and` | Logical conjunction | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.logical.not` | Logical negation | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.logical.or` | Logical disjunction | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.logical.subtract` | Selection subtraction | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.macro.slash` | Slash selection macro | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.macro.slash.absolute` | Leading-slash absolute macro | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.macro.slash.all_empty` | All-empty slash macro | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.macro.slash.name_alt_backtick` | Name-backtick altloc qualifier | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.macro.slash.numeric_resn_as_resi` | Numeric residue-name field as residue identifier | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.macro.slash.object_index_backtick` | Object-backtick atom index | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.macro.slash.relative` | Right-aligned relative macro | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.macro.slash.resn_resi_backtick` | Resn-backtick residue identifier | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.macro.slash.skip_empty_star` | Empty and wildcard macro fields | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.macro.slash.slash_limit` | Slash-count limit | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.match.identifiers` | Identifier-set matching | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.match.name_residue` | Name/residue matching | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.named.create_replace` | Create or replace a named selection | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.named.default_name` | Default selection-name shortcut | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.named.delete` | Delete named selections | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.named.deselect_all` | Disable every visible selection indicator | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.named.domain_scope` | Domain-scoped named selection | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.named.indicate` | Transient indicated selection | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.named.indicator_policy` | Creation-time selection indicator policy | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.named.list_query` | List named selections | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.named.manage` | Named-selection lifecycle | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.named.merge` | Merge into an existing named selection | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.named.pop` | Destructive atom-by-atom selection iteration | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.named.reference` | Explicit named-selection/object reference | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.named.select_list` | Direct identifier-list selection API | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.named.select_list.id` | Select-list by source atom ID | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.named.select_list.index` | Select-list by current atom index | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.named.select_list.rank` | Select-list by load-order atom rank | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.named.select_list.state_scope` | State-scoped select-list API | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.named.state_scope` | State-scoped named selection | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.numeric.b_factor` | B-factor selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.numeric.coordinate_x` | Model-space X selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.numeric.coordinate_y` | Model-space Y selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.numeric.coordinate_z` | Model-space Z selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.numeric.formal_charge` | Formal-charge selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.numeric.occupancy` | Occupancy selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.numeric.partial_charge` | Partial-charge selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.numeric.user_property` | User numeric-property selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.pickability.mask` | Mask atoms from mouse picking | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.pickability.unmask` | Restore mouse pickability | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.position.first` | First selected atom | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.position.last` | Last selected atom | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.property.custom` | Custom atom-property selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.property.flag` | Atom-flag selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.property.numeric_type` | Numeric-type selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.property.secondary_structure` | Secondary-structure selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.property.stereochemistry` | Stereochemistry selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.property.text_type` | Text-type selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.pseudo.rotation_origin` | Rotation-origin pseudo-atom | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.pseudo.scene_center` | Scene-center pseudo-atom | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.setting.active_selections` | Setting active_selections | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.auto_hide_selections` | Setting auto_hide_selections | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.auto_indicate_flags` | Setting auto_indicate_flags | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.auto_number_selections` | Setting auto_number_selections | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.auto_show_selections` | Setting auto_show_selections | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.hide_underscore_names` | Setting hide_underscore_names | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.mouse_selection_mode` | Setting mouse_selection_mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.sel_counter` | Setting sel_counter | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.selection_overlay` | Setting selection_overlay | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.selection_round_points` | Setting selection_round_points | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.selection_visible_only` | Setting selection_visible_only | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.selection_width` | Setting selection_width | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.selection_width_max` | Setting selection_width_max | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.selection_width_scale` | Setting selection_width_scale | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.setting.suppress_hidden` | Setting suppress_hidden | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.spatial.around` | Atoms around a selection | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.spatial.beyond` | Beyond-distance selection | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.spatial.expand` | Distance expansion including source | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.spatial.near_to` | Within-distance selection excluding target | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.spatial.vdw_gap` | VDW-surface gap selection | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.spatial.within` | Within-distance selection | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.status.bonded` | Bonded-atom selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.status.fixed` | Fixed-atom selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.status.masked` | Masked-atom selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.status.protected` | Protected-atom selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.status.restrained` | Restrained-atom selector | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.surface_atoms.find` | Find surface atoms | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.value.atom_name_wildcard` | Atom-name wildcard override | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.value.case_chain_segment` | Chain and segment case policy | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.value.case_general` | General selection case policy | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `selection.value.escape` | Selection value escape handling | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `selection.value.list.alpha` | Alphabetic value lists | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `selection.value.list.numeric` | Numeric value lists | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `selection.value.manage` | Selection value matching policy | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.value.mixed_residue` | Mixed residue identifier matching | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `selection.value.quote` | Quoted selection values | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.value.range.alpha` | Alphabetic ranges | P1 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `selection.value.range.numeric` | Numeric and open-ended ranges | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `selection.value.wildcard` | Configurable alphabetic wildcard matching | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 2 |
| `trajectory.coordinates.smooth` | Window-average coordinate states | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.coordinates.spheroid` | Spheroid motion average | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.fit.intra_states` | Fit all states to a reference state | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.morph.linear` | Linear morph | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.morph.manage` | Conformational morph trajectory | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.morph.rigimol` | RigiMOL morph | P2 | 사용 불가/보류 (`blocked_access`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.base_matrix.check` | Base matrix check | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.base_matrix.clear` | Base matrix clear | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.base_matrix.manage` | Movie base camera matrix | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.base_matrix.recall` | Base matrix recall | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.base_matrix.store` | Base matrix store | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.frame_command.append` | Append frame command | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.frame_command.dump` | Dump frame commands | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.frame_command.manage` | Per-frame command lifecycle | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.frame_command.replace` | Replace frame command | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.frame_command.security_lock` | Lock restored frame commands | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.image_cache.clear` | Clear rendered-frame cache | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.camera_scope` | Camera keyframe scope | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.clear` | Keyframe clear | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.interpolate` | Keyframe interpolate | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.interpolation_parameters` | Keyframe interpolation parameters | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.manage` | Camera and object keyframes | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.object_scope` | Object keyframe scope | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.purge` | Keyframe purge | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.reinterpolate` | Keyframe reinterpolate | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.reset` | Keyframe reset | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.scene_state_channels` | Keyframe scene/state channels | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.smooth` | Keyframe smooth | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.store` | Keyframe store | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.toggle` | Keyframe toggle | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.toggle_interp` | Keyframe toggle_interp | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.keyframe.uninterpolate` | Keyframe uninterpolate | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.manage` | Movie timeline lifecycle | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.navigation` | Movie frame navigation | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.navigation.first` | Seek first movie frame | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.navigation.frame_query` | Query movie frame | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.navigation.last` | Seek last movie frame | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.navigation.middle` | Seek middle movie frame | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.navigation.seek` | Seek explicit movie frame | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.navigation.state_query` | Query mapped molecular state | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.navigation.step_backward` | Step movie backward | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.navigation.step_forward` | Step movie forward | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.play` | Start movie playback | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.playing_query` | Query movie playback state | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.session.persistence` | Persist movie timeline | P1 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | macos | 2 |
| `trajectory.movie.setting.animate_by_frame` | Animate-by-frame policy | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.setting.auto_interpolate` | Automatic interpolation | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.setting.auto_store` | Automatic keyframe store | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.setting.delay` | Movie frame delay | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.setting.frame` | Current movie frame setting | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.setting.loop` | Movie loop policy | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 1 |
| `trajectory.movie.setting.mouse_restart_delay` | Mouse delay restart | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.setting.panel` | Movie panel mode | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.setting.panel_row_height` | Movie panel row height | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.setting.rock` | Movie rock override | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.setting.scene_restart_delay` | Scene delay restart | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.state_mapping.append` | Append state mapping | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.state_mapping.manage` | Frame-to-state mapping | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.state_mapping.range_syntax` | State-range mapping syntax | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.state_mapping.repeat_syntax` | Repeat-state mapping syntax | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.state_mapping.replace` | Replace state mapping | P0 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.stop` | Stop movie playback | P0 | 진행 중(미검증) (`partial`) | 예 | 예 | 예 | `specified` | — | 2 |
| `trajectory.movie.timeline_edit.copy` | Copy movie frames | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.timeline_edit.delete` | Delete movie frames | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.timeline_edit.freeze` | Movie-edit interpolation freeze | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.timeline_edit.insert` | Insert movie frames | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.timeline_edit.manage` | Movie timeline structural editing | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.timeline_edit.move` | Move movie frames | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.timeline_edit.negative_index` | Movie-edit relative indexing | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.timeline_edit.object_scope` | Movie-edit object scope | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie.toggle` | Toggle movie playback | P0 | 진행 중(미검증) (`partial`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie_generator.load` | Load numbered files as frames | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie_generator.nutate` | Nutate movie | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie_generator.pause` | Pause movie | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie_generator.rock` | Rock movie | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie_generator.roll` | Roll movie | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie_generator.screw` | Screw movie | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie_generator.sweep` | Sweep movie | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie_generator.tdroll` | Three-axis roll movie | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.movie_generator.zoom` | Zoom movie | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `trajectory.rms.current_states` | Per-state current-coordinate RMS | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `trajectory.rms.fitted_states` | Per-state fitted RMS | P0 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 2 |
| `trajectory.timeline.frame_count_query` | Query movie frame count | P1 | 진행 중(미검증) (`partial`) | — | 예 | 예 | `specified` | — | 1 |
| `ui.application.quit` | Quit application | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.application.reinitialize` | Reinitialize application state | P1 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.application.splash` | Show splash information | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.diagnostics.frame_rate_reset` | Reset frame-rate meter | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.diagnostics.memory_dump` | Dump memory state | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.diagnostics.renderer` | Query renderer information | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.diagnostics.report` | System diagnostics report | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.help.command` | Command help | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.help.python_keyword` | Python keyword help redirect | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.help.setting` | Setting help | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.keyboard.binding` | Key binding | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.mouse.button_binding` | Mouse button binding | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.mouse.config_ring` | Mouse configuration ring | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.mouse.edit_mode` | Toggle editing mouse mode | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.mouse.mode_cycle` | Cycle mouse mode | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.object_tree.group_arrow_prefix` | Show parent-navigation arrow prefixes for group members | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.object_tree.group_full_member_names` | Show full or prefix-stripped group member names | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_cut_laser` | Setting openvr_cut_laser | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_disable_clipping` | Setting openvr_disable_clipping | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_far_plane` | Setting openvr_far_plane | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_alpha` | Setting openvr_gui_alpha | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_back_alpha` | Setting openvr_gui_back_alpha | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_back_color` | Setting openvr_gui_back_color | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_distance` | Setting openvr_gui_distance | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_fov` | Setting openvr_gui_fov | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_overlay` | Setting openvr_gui_overlay | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_scene_alpha` | Setting openvr_gui_scene_alpha | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_scene_color` | Setting openvr_gui_scene_color | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_text` | Setting openvr_gui_text | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_use_alpha` | Setting openvr_gui_use_alpha | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_gui_use_backdrop` | Setting openvr_gui_use_backdrop | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_laser_width` | Setting openvr_laser_width | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.openvr.setting.openvr_near_plane` | Setting openvr_near_plane | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.output.clear` | Clear output buffer | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view` | Setting seq_view | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_alignment` | Setting seq_view_alignment | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_color` | Setting seq_view_color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_discrete_by_state` | Setting seq_view_discrete_by_state | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_fill_char` | Setting seq_view_fill_char | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_fill_color` | Setting seq_view_fill_color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_format` | Setting seq_view_format | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_gap_mode` | Setting seq_view_gap_mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_label_color` | Setting seq_view_label_color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_label_mode` | Setting seq_view_label_mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_label_spacing` | Setting seq_view_label_spacing | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_label_start` | Setting seq_view_label_start | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_location` | Setting seq_view_location | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_overlay` | Setting seq_view_overlay | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_unaligned_color` | Setting seq_view_unaligned_color | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.sequence_viewer.setting.seq_view_unaligned_mode` | Setting seq_view_unaligned_mode | P1 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.setting.auto_overlay` | Setting auto_overlay | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.autoclose_dialogs` | Setting autoclose_dialogs | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.setting.button_mode` | Setting button_mode | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.button_mode_name` | Setting button_mode_name | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.colored_feedback` | Setting colored_feedback | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.display_scale_factor` | Setting display_scale_factor | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.grid_max` | Setting grid_max | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.grid_mode` | Setting grid_mode | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.grid_slot` | Setting grid_slot | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.internal_feedback` | Setting internal_feedback | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.internal_gui` | Setting internal_gui | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.internal_gui_control_size` | Setting internal_gui_control_size | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.internal_gui_mode` | Setting internal_gui_mode | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.setting.internal_gui_name_color_mode` | Setting internal_gui_name_color_mode | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.internal_gui_width` | Setting internal_gui_width | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.internal_prompt` | Setting internal_prompt | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.keep_alive` | Setting keep_alive | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.legacy_mouse_zoom` | Setting legacy_mouse_zoom | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.mouse_grid` | Setting mouse_grid | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.mouse_limit` | Setting mouse_limit | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.mouse_scale` | Setting mouse_scale | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.mouse_wheel_scale` | Setting mouse_wheel_scale | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.mouse_z_scale` | Setting mouse_z_scale | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.overlay` | Setting overlay | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.overlay_lines` | Setting overlay_lines | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.presentation` | Setting presentation | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.presentation_auto_quit` | Setting presentation_auto_quit | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.presentation_auto_start` | Setting presentation_auto_start | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.presentation_mode` | Setting presentation_mode | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.setting.scene_buttons` | Setting scene_buttons | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.sdof_drag_scale` | Setting sdof_drag_scale | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.show_frame_rate` | Setting show_frame_rate | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.show_progress` | Setting show_progress | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.text` | Setting text | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.virtual_trackball` | Setting virtual_trackball | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.wizard_prompt_mode` | Setting wizard_prompt_mode | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.setting.wrap_output` | Setting wrap_output | P2 | 사용 불가/보류 (`deferred`) | — | — | — | `specified` | — | 0 |
| `ui.window.control` | Window control | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
| `ui.window.full_screen` | Full-screen mode | P2 | 사용 불가/보류 (`excluded`) | — | — | — | `specified` | — | 0 |
