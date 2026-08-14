# Analysis result presentation

`gui::make_analysis_presentation()`은 Qt/QML에 종속되지 않은 Analyze panel view-model factory다.
GUI가 별도 계산이나 결과 formatting을 구현하지 않도록 shared `DispatchOutcome` 하나에서 title,
summary, structured fields, text/JSON rendering, optional typed table/CSV와 marker를 만든다.

## Marker model

- `analyze center` 성공 결과는 `PointMarker`가 된다. Object ID, 계산 결과 position과 length unit,
  precision이 적용된 label을 가진다. Marker는 analysis result view가 소유하는 transient artifact이며
  Workspace나 scene을 변경하지 않는다.
- `measure distance` 성공 결과는 `AtomDistanceMarker`가 된다. Persistent Workspace measurement ID,
  object ID, 두 stable zero-based atom anchor, distance/unit과 label을 가진다. 실제 viewport는 현재
  frame에서 anchor coordinate를 해석하므로 trajectory에서 endpoint를 갱신할 여지를 보존한다.
- 실패한 analysis/measurement도 text와 JSON error presentation을 만들지만 marker는 만들지 않는다.
- `analyze trajectory center|distance|rmsd|rmsf` 성공 결과는 marker 대신 typed table과 CSV view를 가진다.
- Registry normalization에 실패한 action과 analysis가 아닌 command는 generic error/result view가
  담당한다. Presenter는 normalized scalar center/distance 및 네 trajectory analysis command를 받는다.

Marker position/distance는 command에서 요청한 unit을 함께 보존한다. Renderer가 특정 scene unit을
가정하거나 label 값을 다시 계산해서는 안 된다. Point marker의 label은 예를 들어
`centroid (0.150, 0.250, 0.350) nm`, distance label은 `0.173205 nm` 형식이다. 현재 Angstrom label은
ASCII `A`를 사용하며 향후 localization/style layer가 화면 표기를 바꿔도 stored numerical unit은
변하지 않는다.

## Result rendering

Presentation의 `fields`는 성공 `Response`의 typed data를 그대로 복사한다. `text`와 `json`은 각각
기존 `command::render()`를 호출한 결과이므로 CLI/GUI가 별도 serialization 규칙을 갖지 않는다.
Table response는 같은 `Table`을 보존하고 공통 serializer에서 `csv`를 만든다.
성공 response가 필요한 marker field를 누락하거나 잘못된 type/unit을 제공하면 presenter는
`internal` error로 실패해 contract drift를 드러낸다.

## 현재 한계

이 모델은 실제 Qt Analyze panel, marker geometry packet, label layout/occlusion, picking, style,
visibility toggle과 session restore를 구현하지 않는다. Center marker는 transient이고 distance marker는
measurement identity만 보존한다. Qt/QML 연결 이후에도 numerical computation과 serialization은 이
경계 밖으로 복제하지 않는다.
