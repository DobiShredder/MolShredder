# Result envelope

MolShredder command handler는 frontend와 무관한 typed `Response`를 반환한다. CLI와 interactive
console은 이 응답을 동일한 serializer로 text, JSON 또는 CSV로 표현한다.

## 형식 선택

Native CLI에서는 command 전후에 `--format text|json|csv`를 지정할 수 있다.

```bash
molshredder --format json system version
molshredder system version --format json
```

Interactive console에서는 `format text`, `format json` 또는 `format csv`로 이후 command의 형식을 바꾼다.
기본값은 text다.

## JSON schema v1

한 command invocation은 newline으로 끝나는 JSON object 하나를 생성한다. 성공 object는 stdout,
실패 object는 stderr로 출력되며 실패 시 process exit code는 0이 아니다. `command`에는 alias를
확장한 deterministic canonical invocation이 들어간다.

성공 예:

```json
{"schema_version":1,"status":"ok","command":"invoke \"system version\"","summary":"MolShredder 0.1.0","data":{}}
```

실패 예:

```json
{"schema_version":1,"status":"error","command":"invoke \"analyze center\"","error":{"code":"invalid_argument","message":"missing required parameter: selection","suggestion":"provide the required parameter"}}
```

`data` value는 null, boolean, signed/unsigned 64-bit integer, finite double, UTF-8 string, array 또는
재귀 object다. Field와 object key는 정렬되어 같은 결과의 직렬화 순서가 결정적이다. Typed
`Table`이 있는 응답은 `data.table={columns,rows}`를 추가한다. Column 이름은 비어 있지 않고
유일하며, 모든 row는 column 수와 같은 개수의 scalar cell만 가진다. NaN,
infinity와 invalid UTF-8은 invalid JSON으로 내보내지 않고 stable serialization error를 반환한다.

`Number{value,decimal_places}`는 JSON schema에서는 일반 number이고 Python에서는 float지만 text/JSON/CSV
serializer가 0..15의 요청 decimal precision을 보존한다. Fixed decimal rendering 후 불필요한 trailing
zero를 제거하므로 반올림된 double의 binary artifact가 다시 노출되지 않는다. 일반 `double`은 기존
round-trip representation을 유지한다.

정식 machine-readable contract는 [result-envelope-v1.schema.json](schemas/result-envelope-v1.schema.json)에
있다. Schema version 변경에는 새 schema 파일, migration/compatibility 검토와 golden fixture 갱신이
필요하다.

## CSV table contract

CSV는 typed `Table`이 있는 성공 응답에만 제공한다. 첫 record는 column header이고 이후 record는
동일한 순서의 row다. Separator는 comma, record terminator는 CRLF이며 comma, quote, CR/LF가 든
cell은 double quote로 감싸고 내부 quote를 두 번 쓴다. Null은 빈 cell, boolean은 `true|false`, number는
locale과 무관한 round-trip text로 기록한다. UTF-8은 JSON과 동일하게 검증한다. Scalar 성공 결과에
CSV를 요청하면 `unsupported`를 반환한다.

실패 envelope는 원래 operation error를 가리지 않도록
`status,error_code,message,suggestion` header와 한 error row로 출력한다. CSV에는 summary와 global
field를 별도 preamble로 섞지 않는다. 따라서 time-series table은 selection, unit, frame, PBC 등
행을 독립적으로 해석하는 데 필요한 provenance column을 포함해야 한다.
