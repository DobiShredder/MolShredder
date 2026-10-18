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

### Slash macro

PyMOL slash macro는 selection이 올 수 있는 어디에서나 쓸 수 있고 `object`, `segment`, `chain`, `resi`, `name`의
다섯 field를 `/`로 구분한 conjunction으로 확장된다. 비어 있는 field는 제한하지 않는다.

- 절대형(앞에 `/`): field가 왼쪽부터 대응한다. `/ubq//A/10/CA`는 object `ubq`, chain `A`, resi `10`, name `CA`이고
  `/ubq////CA`는 object와 name만 제한한다.
- 상대형: field가 오른쪽(name)부터 대응한다. `A/10/CA`는 chain·resi·name, `10/CA`는 resi·name이다. 두 field
  `A/10`은 resi `A`와 name `10`이므로 chain과 resi를 함께 지정하려면 `A/10/`처럼 끝에 `/`를 둔다.
- field 값은 일반 selection과 같은 list(`10+12`), range(`1-10`), wildcard(`C*`)를 쓰고 insertion code는 residue id에
  포함된다(`10A`). Field가 다섯 개를 넘거나 값이 하나도 없으면 오류다.
- backtick 한정자: object field `obj`5`는 object와 atom index, residue field `ALA`10`는 residue 이름과 번호, name field
  `CA`A`는 atom 이름과 altloc을 함께 지정한다. 다른 field의 backtick, 값 없는 backtick, backtick 두 개는 오류다.
- 숫자 field 이름 뒤의 `/`(`b/2 > 5`, `x /2`)와 괄호 뒤의 `/`는 계속 나눗셈이다. 숫자로 시작하는 상대형은
  문자가 하나라도 있어야 macro로 읽는다(`10/CA`는 macro, `10/2`는 나눗셈).

### VMD residue class와 numeric field

VMD 공개 User's Guide의 정의를 따르는 residue class keyword를 지원한다(residue 이름 목록 기준, 대문자 이름):
`acidic`(ASP GLU), `basic`(ARG HIS LYS), `neutral`(VAL PHE GLN TYR HIS CYS), `charged`(basic or acidic),
`aliphatic`(ALA GLY ILE LEU VAL), `aromatic`(HIS PHE TRP TYR), `cyclic`(HIS PHE PRO TRP TYR),
`acyclic`(polymer.protein and not cyclic), `small`(ALA GLY SER), `medium`(VAL THR ASP ASN PRO CYS),
`large`(polymer.protein and not (small or medium)), `buried`(ALA LEU VAL ILE PHE CYS MET TRP),
`surface`(polymer.protein and not buried), `polar`(polymer.protein and not ALA LEU VAL ILE PRO PHE MET TRP),
`at`, `cg`, `purine`, `pyrimidine`(핵산 residue 이름), `alpha`(`ss H`), `amino`(N·CA·C·O atom을 모두 가진 residue).
이 keyword들은 PyMOL의 같은 이름과 의미가 다를 수 있으므로 VMD 정의임을 문서화한다. 단백질 판별은 이 문법의
`polymer.protein`(PyMOL 정의)을 쓴다.

Numeric field로 `atomicnumber`, `numbonds`(topology bond 수), `radius`(van der Waals, Bondi 표 또는 PQR radius property),
`mass`(topology `mass` property; 없으면 오류이며 element 추정 질량은 `analyze center --mode com`이 제공)를 쓸 수 있다.
`bonded`는 bond가 하나라도 있는 atom이다.

### Numeric 함수와 word 비교

Numeric 식은 한 인자 함수를 쓸 수 있다: `abs`, `acos`, `asin`, `atan`, `ceil`, `cos`, `cosh`, `exp`, `floor`, `log`
(자연로그), `log10`, `sin`, `sinh`, `sqr`(x*x), `sqrt`, `tan`, `tanh`. 각도는 radian이다. 함수는 field와 사칙연산을 인자로
받는다(`abs(b - 25) < 6`, `sqrt(sqr(b)) = b`). 함수가 정의역을 벗어나거나 overflow하면(`sqrt(partial_charge)`가 음수
atom에 적용되는 경우 등) 해당 atom을 조용히 제외하지 않고 selection 평가 전체를 오류로 처리한다(0으로 나누기와 같은 정책).
비교 연산자는 기호(`= != < <= > >=`) 외에 PyMOL word 형태 `eq ne lt le gt ge`도 받는다(`b ge 30`, `(b + 10) ge 40`).

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

`select --merge true`는 같은 update mode의 기존 selection에 새 membership을 합집합으로 더한다(PyMOL `merge=1`).
이름이 없으면 일반 생성과 같고, update mode가 다르거나 새 expression이 실패하면 기존 entry를 그대로 둔다.
Static merge는 얼려 둔 기존 membership을 보존하고, dynamic merge는 `(기존) or (새)` expression이 된다.
`select`의 `--name`을 생략하면 PyMOL처럼 `sele`가 된다. `select list --by index|id|rank --values 1,5,9-12`는 쉼표로 구분한
atom 식별자 목록(범위 포함)을 static selection으로 만들며 `--merge`와 `--state`를 같은 규칙으로 받는다. 빈 항목이나 알 수 없는
식별자 종류는 오류다. 문자 범위는 `chain A-C`와 `chain A:C`가 같고, `name C\*`처럼 이스케이프한 `*`는 literal이다.
`select --state N|all`은 static selection의 state-sensitive 연산(`x/y/z`, 공간 관계, frame property)을 현재 frame 대신
1-based state N에서, 또는 모든 state의 합집합으로 평가한다. Dynamic selection은 항상 현재 state를 따르므로
`--state`와 함께 쓸 수 없고, merge와도 함께 쓸 수 없다.

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

## VMD 값 keyword와 구조 단어 (SEQ-318)

- `residuetype protein|nucleic|waters|nothing [...]`: 값 목록은 합집합이다. protein·nucleic은 `polymer.protein`·`polymer.nucleic`, waters는 `solvent`, nothing은 그 밖의 원자다.
- `backbonetype proteinback|nucleicback|hydrogen|normal [...]`: 단백질·핵산 backbone, 수소, 나머지로 원자를 나눈다.
- `structure H|E|C [...]`, `helix`·`alpha_helix`, `sheet`·`betasheet`·`beta_sheet`·`extended_beta`, `coil`: 파일이 제공한 secondary structure(`ss H`/`ss S`)를 쓴다. `3_10_helix`·`pi_helix`·`bridge_beta`·`turn`과
  `structure G|I|B|T`는 계산된 할당이 필요해 오류를 낸다(조용히 빈 결과를 주지 않는다).
- `vx`·`vy`·`vz`: 현재 frame의 속도 비교. 속도를 저장한 frame이 아니면 오류다.
- `select keywords`가 전체 keyword·별칭·예시를 나열하며 test가 모든 예시의 parse를 확인한다.
