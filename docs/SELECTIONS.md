# Atom selection foundation

MolShredder의 foundation selection은 source string을 immutable expression tree로 parse한 뒤
topology 크기의 0/1 atom mask로 평가한다. Parser와 evaluator는 GUI, CLI, Python 및 renderer가
공유하는 C++ core다. 현재 grammar는 pinned PyMOL 3.1.0 selection-language inventory의 P0
16개 semantic group을 MolShredder data model에 맞춰 구현하지만, reference black-box capture가
완료되기 전에는 PyMOL 또는 VMD 문법 호환을 주장하지 않는다.

## Grammar

```text
expression := or-expression
or-expression := and-expression ("or" and-expression)*
and-expression := unary-expression (("and" | implicit-and) unary-expression)*
unary-expression := ("not" | "first" | "last" | expansion) unary-expression | primary
primary := "all" | "none" | status | chemical | pseudo | predicate
         | ("@" | "%") name | "(" expression ")"
predicate := field value | numeric-expression comparison numeric-expression
comparison := "=" | "==" | "!=" | "<" | "<=" | ">" | ">="
numeric-expression := numeric-product (("+" | "-") numeric-product)*
numeric-product := numeric-primary (("*" | "/") numeric-primary)*
numeric-primary := number | numeric-field | "(" numeric-expression ")"
range-comparison := numeric-expression "in" range-list
range-list := number [":" number] ("," number [":" number])*
unary-expansion := expansion-operator unary-expression
postfix-expansion := primary (("around" | "expand") distance | "extend" steps)*
spatial-relation := unary-expression (relation distance "of" unary-expression)*
match-relation := unary-expression ("in" | "like") unary-expression
```

`not` > `and`/implicit-and/`-` > `or` 순서로 결합하며 `!`, `&`, `|`/`+`를 각각 normalize한다.
논리 keyword와 field 이름은 ASCII case-insensitive다. Single/double
quoted value는 공백과 `and/or/not` 같은 단어를 data로 보존하며 backslash로 다음 문자를
escape한다.

Unquoted string value는 `+`로 list를 만들고 `*`/`?` wildcard를 사용할 수 있다. Quoted value의
wildcard 문자는 literal이다. 일반 identifier는 `A-C` alpha range를, residue ID는 `5-10`
inclusive numeric range와 `5-10+42B` list를
지원한다. Identifier matcher는 case-sensitive이며 element symbol만 ASCII case-insensitive다.

| field | alias | value와 의미 |
|---|---|---|
| `name` | `n.`, `n;` | atom name list/wildcard match |
| `element` | `elem`, `symbol`, `e.`, `e;` | 원소 기호, ASCII case-insensitive |
| `altloc` | `alt` | alternate-location identifier |
| `resname` | `resn` | residue name exact match |
| `resid` | `resi` | sequence number와 insertion code 결합값, 예: `10A` |
| `chain` | - | chain ID exact match |
| `segment` | `segi` | segment ID exact match |
| `object` | `model`, `o.`, `m.`, `m;` | explicit evaluation context의 object name |
| `rank` | - | zero-based stable topology order |
| `pepseq` | `ps.` | chain 안의 contiguous one-letter peptide sequence; `?`/`X` wildcard |
| `index` | - | one-based internal atom index 또는 inclusive `first:last` |
| `id` | - | source atom serial 또는 inclusive `first:last` |

Pinned identifier alias `residue/resident/i./i;`, `r./r;`, `c./c;`,
`segment/segid/s./s;`, `idx.`도 해당 canonical field로 normalize한다.

`label`, `flag`/`f.`/`f;`, `numeric_type`/`nt.`, `text_type`/`tt.`, `stereo`, `custom`과
`p.<name> value`는 topology/current-frame atom property를 match한다. Text는 list/wildcard,
boolean은 `true/false/1/0`, numeric은 exact value를 사용하며 numeric range/arithmetic는
`p.<name> <comparison> ...` 문법을 사용한다. `ss`는 `secondary_structure` property와 연결된다.

Appearance selector `color`, `cartoon_color`, `ribbon_color`, `rep`은 같은 이름의 typed atom
property를 match하고 `enabled`, `visible`/`v.`/`v;`는 boolean property를 사용한다. `fixed`/`fxd.`,
`masked`/`msk.`, `protected`, `restrained`/`rst.`도 boolean status property다. Property가 없으면
해당 unary status membership은 empty다.

## Expansion과 spatial relation

Prefix `byresidue`, `bychain`, `bysegment`, `byobject`, `bymolecule`/`byfragment`, `bycalpha`,
`neighbor`, `bound_to`와 pinned alias를 지원한다. `neighbor`는 입력 membership을 제외한 direct
bond neighbor이고 `bound_to`는 선택 내부 bond endpoint를 포함한다. Postfix `extend N`은 입력을
포함해 N bond step까지 확장한다. `byring`은 선택 atom이 속한 최대 7-member small ring의
shortest cycle을, `bycell`은 current frame에 valid unit cell이 있을 때 현재 single-object cell의
전체 atom을 반환한다.

```text
byresidue (name CA or name N)
neighbor index 5
index 5 extend 2
index 1 around 4.0
chain A within 5.0 of resname LIG
all near_to 3.5 of @ligand
all beyond 10.0 of @active_site
index 1 gap -0.5
all within 5.0 of center
origin around 2.0
```

`around`은 기준 selection을 제외하고 주변 atom을 반환하며 `expand`는 원 selection을 포함한다.
`within`은 left operand 중 right operand cutoff 안의 atom, `near_to`는 그 결과에서 right membership을
제외한 atom, `beyond`는 right operand의 모든 present atom보다 먼 left atom을 반환한다. Distance는
current frame의 present coordinate에 raw angstrom으로 적용하며 정확히 cutoff인 atom을 포함한다.
`gap`은 기준 atom을 제외하고 `center_distance <= radius(candidate) + radius(reference) + gap`인
atom을 선택한다. Radius는 `pqr.radius`/`vdw_radius` typed property와 unit을 우선하며 없으면
`molshredder-selection-vdw-radius-v1` element fallback을 쓴다. Negative gap도 허용한다. `center`는 명시된 scene center 또는
present-coordinate centroid, `origin`은 명시된 rotation origin 또는 local `(0,0,0)`인 virtual point다.
Plain pseudo selector는 topology atom mask가 empty이며 spatial operand로 사용한다. Spatial query는
현재 raw/non-PBC 계약이고 PBC가 필요한 분석은 별도 typed PBC operation을 사용한다.

## Coordinate state와 chemical class

`present`/`pr.`은 current frame presence mask를, `state N`은 one-based coordinate source state의
presence mask를 사용한다. `first`/`last`는 operand의 topology order에서 한 atom만 반환하며 `bonded`는
한 개 이상의 topology bond endpoint인 atom을 반환한다. Symbolic `!`, `&`, `|`, `+`와 binary `-`는
각각 `not`, `and`, `or`, `or`, set subtraction으로 normalize한다. `*`는 `all` alias다.

Chemical selector의 현재 method ID는 `molshredder-selection-chemical-v1`이다. `hydrogens`, `metals`,
`hetatm`, `polymer[.protein|.nucleic]`, `solvent`, `organic`, `inorganic`, `backbone`, `sidechain`,
`guide`, `donors`와 `acceptors` 및 pinned alias를 제공한다. Polymer/solvent는 versioned residue-name
catalog, organic/inorganic은 non-polymer/non-solvent residue의 carbon presence, donor/acceptor는 explicit
topology boolean property 또는 `element-bond-v1` fallback을 사용한다. 이는 pinned PyMOL black-box
chemical perception과 compatible하다는 주장이 아니며 SEQ-212 chemistry normalization에서 method와
provenance를 공통 contract로 승격하기 전까지 `partial`이다.

예:

```text
all
chain A and not resname HOH
(name CA or name N) and resi 10A
element O and index 1:100
@active_site or @ligand
```

Named selection은 `@name` 또는 pinned `%name`으로 참조한다. Bare identifier를 named selection으로 추측하지 않아
field/value typo가 조용히 다른 결과로 바뀌는 것을 막는다.

숫자 비교는 pinned PyMOL 3.1.0의 `=`, `<`, `>`, `in` 네 semantic을 포함하고 MolShredder의
명시적 `==`, `!=`, `<=`, `>=`를 제공한다. Parentheses가 우선하며 `*`와 `/`가 `+`와 `-`보다 먼저 결합한다. 숫자
field는 `index`, `id`, `formal_charge`(`fc`, `fc.`, `fc;`), `b`, `q`,
`partial_charge`(`pc`, `pc.`, `pc;`), current-frame coordinate `x/y/z`와 임의의 numeric
property를 가리키는 `p.<name>`이다. `b`는 `b_factor`를 우선하고 없으면 `b_iso_or_equiv`를
사용한다. 같은 property가 topology와 current frame에 모두 있으면 frame channel이 우선한다.

```text
b > 20
q = 1 and partial_charge < -0.25
b / 10 + q * 2 >= 3
p.score in 1:3,8
(b + q * 10) / 2 >= 10
```

Property에 대응하는 `<name>_present` boolean column이 있으면 missing row는 선택하지 않는다.
없는 property, text property, non-finite value와 zero division은 `invalid_selection`으로 실패하며
부분 mask를 반환하지 않는다. Numeric equality와 range boundary는 exact membership contract다.

## Named selection lifecycle

`NamedSelections::set()`은 생성과 교체를 atomic operation으로 처리한다. Unknown reference,
cycle 또는 평가 오류가 있으면 기존 entry를 복원한다. 이름은 letter/underscore로 시작하고 이후
letter/digit/`_-.`를 사용할 수 있으며 `all`, `none`은 reserved다. List는 이름순이다.

- Static selection은 정의 시 mask를 계산한다. Versioned topology transaction에서는 stable atom
  ID로 target snapshot에 remap하고 삭제된 atom을 제거한다. 임의의 별도 snapshot에 직접
  적용하는 것은 거부한다.
- Dynamic selection은 호출 시 expression과 dependency를 다시 평가한다.
- 참조 중인 entry는 dependent selection을 교체하거나 삭제하기 전에는 삭제할 수 없다.

Dynamic entry는 `EvaluationContext`의 current immutable coordinate frame에서 `x/y/z`와 frame atom
property를 매번 다시 평가한다. Static entry는 정의 당시 frame에서 계산한 mask를 보존하므로 seek 뒤에도
membership이 변하지 않는다. Static entry는 topology를 소유하지 않으므로 registry보다 topology
snapshot lifetime이 길어야 한다. 저장된 pointer와 version은 snapshot/remap identity 검증에만 사용한다.

## Validation과 limitation

Parse error는 source byte offset과 `invalid_selection` error를 반환한다. Evaluator는 atom 수와 같은
0/1 mask만 허용한다. `index`는 one-based이며 `0`, malformed/reversed range와 resolver가 없는
`@name`은 실패한다.

`in`은 left atom의 name/residue number+insertion/residue name/chain/segment identifier가 right set의
atom과 같은 membership을, `like`/`l.`/`l;`는 name과 residue number+insertion만 같은 membership을
반환한다.

Desktop의 Select > Select by Expression은 name, expression과 static/dynamic policy를 받아 CLI와
Python이 사용하는 canonical `select` operation을 호출한다. Dynamic 선택은 trajectory seek 뒤
current frame에서 재평가된다.

아직 지원하지 않는 범위는 regex predicate, volume-grid predicate, PBC-aware spatial selection,
multi-object cross-context pseudo point, persistent spatial cache/index와 pinned PyMOL black-box의
동작 호환 판정이다. 이 범위는 capability matrix의 후속 evidence와 data-model 작업에서 추가하며
현재 명시적 raw-coordinate semantics를 조용히 바꾸지 않는다.
