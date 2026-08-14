# Atom selection foundation

MolShredder의 foundation selection은 source string을 immutable expression tree로 parse한 뒤
topology 크기의 0/1 atom mask로 평가한다. Parser와 evaluator는 GUI, CLI, Python 및 renderer가
공유하는 C++ core다. 현재 grammar는 첫 vertical slice를 위한 provisional subset이며 아직
PyMOL 또는 VMD 문법 호환을 약속하지 않는다.

## Grammar

```text
expression := or-expression
or-expression := and-expression ("or" and-expression)*
and-expression := unary-expression ("and" unary-expression)*
unary-expression := "not" unary-expression | primary
primary := "all" | "none" | predicate | "@" name | "(" expression ")"
predicate := field value
```

`not` > `and` > `or` 순서로 결합한다. 암시적 `and`는 허용하지 않아 session replay가 공백이나
추측에 의존하지 않는다. 논리 keyword와 field 이름은 ASCII case-insensitive다. Single/double
quoted value는 공백과 `and/or/not` 같은 단어를 data로 보존하며 backslash로 다음 문자를
escape한다.

| field | alias | value와 의미 |
|---|---|---|
| `name` | - | atom name exact match |
| `element` | `elem` | 원소 기호, ASCII case-insensitive |
| `resname` | `resn` | residue name exact match |
| `resid` | `resi` | sequence number와 insertion code 결합값, 예: `10A` |
| `chain` | - | chain ID exact match |
| `segment` | `segi` | segment ID exact match |
| `index` | - | one-based internal atom index 또는 inclusive `first:last` |
| `id` | - | source atom serial 또는 inclusive `first:last` |

예:

```text
all
chain A and not resname HOH
(name CA or name N) and resi 10A
element O and index 1:100
@active_site or @ligand
```

Named selection은 `@name`으로만 참조한다. Bare identifier를 named selection으로 추측하지 않아
field/value typo가 조용히 다른 결과로 바뀌는 것을 막는다.

## Named selection lifecycle

`NamedSelections::set()`은 생성과 교체를 atomic operation으로 처리한다. Unknown reference,
cycle 또는 평가 오류가 있으면 기존 entry를 복원한다. 이름은 letter/underscore로 시작하고 이후
letter/digit/`_-.`를 사용할 수 있으며 `all`, `none`은 reserved다. List는 이름순이다.

- Static selection은 정의 시 mask를 계산하고 정확히 같은 immutable topology snapshot에서만
  반환한다.
- Dynamic selection은 호출 시 expression과 dependency를 다시 평가한다.
- 참조 중인 entry는 dependent selection을 교체하거나 삭제하기 전에는 삭제할 수 없다.

현재 predicate는 topology field만 사용하므로 dynamic은 topology snapshot 변화에 대한 재평가를
뜻한다. Coordinate/frame-dependent property와 spatial predicate가 추가되면 같은 flag가 frame
변화에 따른 재평가를 제어한다. Static entry는 topology를 소유하지 않으므로 registry보다 topology
snapshot lifetime이 길어야 한다. 저장된 pointer는 snapshot identity 비교에만 사용한다.

## Validation과 limitation

Parse error는 source byte offset과 `invalid_selection` error를 반환한다. Evaluator는 atom 수와 같은
0/1 mask만 허용한다. `index`는 one-based이며 `0`, malformed/reversed range와 resolver가 없는
`@name`은 실패한다.

아직 지원하지 않는 범위는 wildcard/regex, implicit boolean, property comparison/arithmetic,
polymer/protein/solvent keyword, bond/residue/chain expansion, within/around/same-property/sequence,
coordinates와 volume predicate, per-frame property, PBC spatial query 및 selection cache/index다. 이
기능은 capability matrix에 따라 AST node와 evaluator를 확장하며 기존 명시적 grammar의 의미를
변경하지 않는다.
