# Command grammar v1

첫 molecular vertical slice의 GUI·Console·Python parity와 session replay 검증을 기준으로 foundation
command grammar를 version 1로 pin한다. Canonical history/session에는 생략된 기본값과 alias가 모두
정규화된 invocation을 저장한다.

## Native CLI

```text
molshredder load --path PATH [--name NAME] [--file-format FORMAT]
molshredder format list [--family all|structure|trajectory|volume]
                           [--direction all|read|write]
molshredder volume load --path PATH [--name NAME]
                           [--file-format auto|dx|opendx|mrc|map|ccp4|mrcs]
                           [--coordinate-unit angstrom|nanometer]
molshredder volume list
molshredder volume save --path PATH [--file-format auto|dx|opendx|mrc|map|ccp4|mrcs]
                           [--overwrite false|true]
molshredder volume isosurface --level NUMBER
                           [--color blue|cyan|green|magenta|orange|red|white|yellow]
                           [--opacity 0..1] [--replace false|true]
molshredder save --path PATH [--file-format auto|pdb|mmcif|cif|mol|mol2|psf|pqr|sdf|gro|g96|xyz]
                 [--frames current|all] [--precision 0..15]
                 [--comment TEXT] [--overwrite false|true]
molshredder select --name NAME --expression EXPR [--update false|true]
molshredder show --representation lines|sticks|spheres|ribbon|cartoon [--selection EXPR]
                   [--replace false|true]
molshredder analyze center [--selection EXPR] [--mode centroid|com]
                               [--precision 0..15] [--unit angstrom|nanometer]
molshredder measure distance --from EXPR --to EXPR
                               [--mode atom|centroid|com|minimum|maximum|mean|closest]
                               [--pbc raw|minimum-image]
                               [--precision 0..15] [--unit angstrom|nanometer]
molshredder script run --path SCRIPT.py --trust true
                         [--arguments-json '["arg1","arg2"]']
                         [--working-directory DIRECTORY]
```

각 leaf command는 `--format text|json|csv`를 받는다. CSV 성공 출력은 typed table command에 한정한다.
`molshredder COMMAND --help`가 registry의
required/default/choice metadata에서 생성되는 authoritative user help다.

MD vertical slice의 additive trajectory playback 및 time-series 문법은
[Trajectory commands](TRAJECTORY_COMMANDS.md)에 둔다. 이는 foundation 다섯 command의 grammar v1을
변경하지 않는다.
다중 object의 additive `object list/activate/visibility` 문법과 failure-atomic scene semantics는
[Molecular objects and visibility](OBJECTS.md)에 둔다.
Additive `format list`와 `save` 문법, atomic output 및 semantic loss table은
[Structure writing](STRUCTURE_WRITING.md)에 둔다.
외부 Python script 실행의 trust, provenance와 failure semantics는 [Automation](AUTOMATION.md)에 둔다.

## 의미

- `load`: molecular structure를 새 object로 읽는다. Multi-record SDF, multi-molecule MOL2와 multi-block mmCIF는 record/block마다
  object를 만들고 전체 batch를 atomic commit한다. Explicit name은 여러 structure에서 숫자 suffix로 확장된다.
  Concatenated GRO는 stable identity의 frame들로 한 object에 유지된다.
  G96 ordered POSITION block도 stable identity의 frame들로 한 object에 유지된다. PSF와 Amber PRMTOP은
  정상적인 zero-frame topology object로 load되며 `traj load` 전에는 coordinate operation이 명시적으로
  실패한다. PRMTOP에는 `--file-format prmtop`을 사용하며 `.prmtop`, `.parm7`, `.top`은 자동 판별된다.
  VTF는 `--file-format vtf` 또는 `.vtf` suffix로 topology, bond, atom property, unit cell과 ordered/indexed
  sparse frame을 한 object에 load한다. Multi-frame structure는 즉시 `traj frame/play`에 연결된다.
  BinaryCIF 0.3.x는 `.bcif` 또는 `--file-format bcif`로 읽으며 multi-block/model semantics는 mmCIF와 같다.
- `volume load`: ASCII OpenDX regular scalar grid 하나를 molecular object와 독립된 typed volume scene
  object로 읽는다. `coordinate-unit` 기본값은 APBS convention인 `angstrom`이며 source format 자체가
  단위를 기록하지 않으므로 override와 provenance를 결과에 보존한다. 성공 결과는 dimensions, origin,
  세 delta vector, z-fastest value count, precision과 scalar range를 반환한다. MRC/CCP4는 header geometry가
  Angstrom이므로 `nanometer` 선택 시 geometry를 변환한다. MRC axis permutation과 origin/start policy는
  [Volumetric data](VOLUMETRIC_DATA.md)에 고정한다.
- `volume list`: 현재 Workspace의 volume object ID/name, scene node, dimensions, value count, precision,
  scalar range, coordinate unit, representation count와 active/visibility를 typed table로 반환한다.
- `volume save`: active volume을 OpenDX 또는 canonical MRC2014 mode 2로 failure-atomic 저장한다. Format은
  명시하거나 suffix로 선택하며 GUI action, CLI와 Python이 같은 typed operation 및 loss report를 사용한다.
- `volume isosurface`: active volume의 contour mesh를 생성한다. 기본 cyan/불투명 style을 사용하며
  `--replace true`가 기본이다. GUI, CLI와 Python은 같은 failure-atomic Workspace operation을 호출한다.
- `select`: atom selection expression을 named selection으로 생성 또는 교체한다. `--update true`는
  frame/state 변화에 따라 다시 평가되는 selection을 뜻한다.
- `show`: selection에 representation을 보이게 한다. 현재 choice는 lines/sticks/spheres와
  protein backbone ribbon/cartoon이다. 기본은 기존 representation에 append하며 `--replace true`는
  새 packet 생성이 성공한 뒤 기존 representation을 atomic하게 교체한다. Cartoon은 독립
  STRIDE-method v0 assignment를 사용한다.
- `analyze center`: 재사용 가능한 scalar/vector analysis result를 계산한다. `centroid`와 `com`을
  구분하며 기본 selection은 `all`이다. COM은 topology의 explicit `mass` property를 우선하고 없을
  때 versioned estimated element-mass table을 사용하며 provenance를 결과에 포함한다.
- `measure distance`: scene/session에 남는 measurement를 만든다. 두 endpoint는 atom 또는 selection
  expression이고 mode가 reduction semantics를 지정한다. 현재 실행되는 첫 slice는 endpoint마다
  정확히 한 atom을 선택하는 `mode=atom`이며 `pbc=raw|minimum-image`를 지원한다. Minimum-image는
  active frame의 orthorhombic/triclinic unit cell을 요구한다.

## Shorthand

| shorthand | canonical command | 고정 argument |
|---|---|---|
| `open` | `load` | 없음 |
| `center` | `analyze center` | 없음 |
| `centroid` | `analyze center` | `mode=centroid` |
| `com` | `analyze center` | `mode=com` |
| `dist`, `distance` | `measure distance` | 없음 |

명시한 option은 shorthand의 고정 argument도 override한다. 예를 들어 `com --mode centroid`는
canonical `analyze center --mode centroid`가 된다. Alias 사용 여부는 history에 남기지 않는다.

## 현재 실행 상태

다섯 foundation command는 stateful Workspace와 실제 reader/selection/representation/analysis
kernel에 연결됐다. 연속 native terminal workflow는 한 process의 `molshredder console`에서
실행한다. One-shot command를 각각 새 process로 실행하면 state가 유지되지 않는다.
`analyze center`는 pure result이고 `measure distance`는 Workspace에 boundary mode를 포함한
measurement record를 남긴다. Distance의 selection reduction mode는 schema v1 choice로 예약돼 있지만
kernel이 구현될 때까지 명시적 `unsupported` error를 반환한다. `minimum-image`는 shared exact
triclinic PBC kernel을 실행하며 결과 data의 `pbc` field에 provenance를 보존한다.

## 호환성 정책

- Grammar v1 canonical leaf name은 `load`, `select`, `show`, `analyze center`, `measure distance`다.
- `open`, `center`, `centroid`, `com`, `dist`, `distance`는 convenience alias이며 session/history에는
  저장하지 않는다.
- 기존 invocation의 의미를 바꾸는 command/parameter rename, default 변경 또는 choice 제거는
  grammar version과 session migration이 필요하다.
- 새 optional parameter나 representation/format choice를 추가하는 것은 기존 normalized invocation을
  바꾸지 않는 범위에서 additive change로 처리할 수 있다.
- `--format`은 presentation option이므로 domain invocation과 session provenance에 포함하지 않는다.
