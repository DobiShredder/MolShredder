# Structure format support

상태: foundation reader contract  
검증 기준일: 2026-08-13

MolShredder core는 dependency 없는 PDB 및 PDBx/mmCIF structure reader를 제공한다. Public API는
`molshredder/io/structure_reader.hpp`이며 memory content 또는 file path를 읽을 수 있다. Format을
명시하거나 첫 meaningful record로 자동 판별한다. Reader는 application object ID를 만들지 않고
`StructureDocument` 안에 topology와 coordinate source를 반환하므로 application layer가 object
tree identity를 할당한다.

## Capability matrix

| Channel | PDB 3.3 | PDBx/mmCIF |
|---|---|---|
| Atom/residue identity | `ATOM`, `HETATM`, altLoc, chain, resSeq, iCode, segID, serial | `_atom_site` label 및 author identifier, alt ID, atom-site ID |
| Element/charge | element column, blank element의 aligned atom-name inference, optional charge | `type_symbol`, nullable `pdbx_formal_charge` |
| Coordinate | fixed-column Cartesian Å, float64 preservation | `Cartn_x/y/z` Å, float64 preservation |
| Per-frame property | nullable occupancy와 temperature factor | nullable occupancy와 `B_iso_or_equiv` |
| Multiple model | `MODEL`/`ENDMDL` | `pdbx_PDB_model_num` |
| Missing model atom | explicit presence mask와 finite placeholder | explicit presence mask와 finite placeholder |
| Unit cell | `CRYST1` lengths/angles, space group와 Z metadata | `_cell` lengths/angles, scalar metadata |
| Explicit bond | deduplicated `CONECT`, unknown order | `_struct_conn` label/auth partners와 sing/doub/trip/arom order |
| Source metadata | entry ID, title, space group, format/source | data-block name와 모든 scalar data item |

Static source values는 typed topology property에, model별 occupancy/B-factor와 missingness는 frame
property에 보존한다. `.`와 `?`인 numeric mmCIF value나 blank PDB value를 임의의 과학 값으로
대체하지 않고 finite placeholder와 별도의 `*_present` boolean column으로 표현한다. Later model은
model 1의 immutable atom identity에 맞춰 배치하며 누락은 허용하지만 새 atom이나 duplicate
identity는 오류다.

mmCIF lexer는 CIF 1.1의 data block, case-insensitive data name, key/value item, single-level loop,
single/double quote, line-start semicolon text field, comment 및 `.`/`?` missing token을 처리한다.
Loop는 한 category의 column만 가져야 하며 value 수가 column 수의 양의 배수인지 검증한다.
여러 `data_` block은 document 안의 여러 structure로 반환한다.

## Validation과 오류

Reader는 다음을 입력 경계에서 거부한다.

- 잘못되거나 누락된 required numeric/identity field
- unknown element, invalid formal charge와 non-finite coordinate
- duplicate first-model identity/serial 및 later-model topology mutation
- partial 또는 degenerate unit cell
- unknown explicit connectivity endpoint
- nested/unclosed PDB model, malformed CIF quote/text field/loop/data item

Parse error message에는 source name과 1-based line을 포함한다. File open failure는 stable
`not_found` error로 반환하고 parse/semantic failure는 `invalid_argument`로 반환한다.

## 현재 limitation

- Reader는 아직 writer나 source-text round-trip을 제공하지 않는다.
- File API는 전체 structure text를 memory에 읽는다. Out-of-core trajectory reader와 별개이며,
  대형 mmCIF incremental tokenizer는 performance profiling 후 추가한다.
- PDB hybrid-36 serial, `ANISOU`, `LINK`/`SSBOND`, secondary-structure/assembly/transform record와
  dictionary-implied polymer bond는 아직 해석하지 않는다.
- mmCIF의 non-atom loop는 syntax validation만 하고 retained table로 노출하지 않는다.
  `_chem_comp_bond`, assembly, anisotropic displacement와 BCIF는 아직 지원하지 않는다.
- Explicit connectivity가 없을 때 distance로 bond를 추정하지 않는다. Chemical Component
  Dictionary 기반 bond 생성은 별도 topology-enrichment 단계다.

따라서 현재 `PDB/mmCIF 지원`은 위 matrix의 read channel만 의미하며 완전한 format round-trip이나
wwPDB dictionary 전체 지원을 뜻하지 않는다.

## Normative reference

- [wwPDB PDB format 3.3 introduction](https://www.wwpdb.org/documentation/file-format-content/format33/sect1.html)
- [wwPDB PDB coordinate records](https://www.wwpdb.org/documentation/file-format-content/format33/sect9.html)
- [wwPDB PDB crystallographic records](https://www.wwpdb.org/documentation/file-format-content/format33/sect8.html)
- [wwPDB PDB format 3.3 full index](https://www.wwpdb.org/documentation/file-format-content/format33/v3.3.html)
- [wwPDB PDBx/mmCIF syntax](https://mmcif.wwpdb.org/docs/tutorials/mechanics/pdbx-mmcif-syntax.html)
- [wwPDB PDBx/mmCIF coordinate section](https://mmcif.wwpdb.org/docs/user-guide/resources/coordinate_section.html)
- [IUCr CIF 1.1 syntax](https://www.iucr.org/resources/cif/spec/version1.1/cifsyntax)

Synthetic fixtures는 MolShredder용으로 직접 작성했으며 외부 structure data를 복사하지 않았다.
