# Molecular objects and visibility

Workspace는 여러 molecular object를 load 순서대로 보존하며 각 object에 monotonic `object_id`, immutable
scene node, `MolecularSystem`, named selection, representation과 optional trajectory state를 둔다. Object를
전환해도 다른 object의 representation, selection과 trajectory frame은 사라지지 않는다.

## Commands

```text
molshredder object list [--format text|json|csv]
molshredder object activate --id OBJECT_ID
molshredder object visibility --id OBJECT_ID --visible false|true
```

`object list`는 object ID/name, active/direct/effective visibility, atom/frame/representation 수,
trajectory 존재 여부와 scene node ID를 typed table로 반환한다. `object activate`는 active object와 scene
node selection을 같은 immutable scene commit에서 바꾼다. `object visibility`는 object의 direct visibility를
바꾸며 ancestor visibility를 포함한 결과는 `effectively_visible`로 별도 보고한다. 존재하지 않는 ID나
scene build 실패는 기존 active index와 scene snapshot을 보존한다.

이 command family는 foundation grammar v1의 기존 다섯 command 의미를 변경하지 않는 additive API다.
GUI, interactive CLI와 Python은 같은 registry handler와 Workspace operation을 사용한다. 예:

```python
molshredder.invoke("object list")
molshredder.invoke("object activate", {"id": "1"})
molshredder.invoke("object visibility", {"id": "2", "visible": "false"})
```

## Desktop panel and rendering

Desktop Objects panel은 모든 object의 name, atom 수, representation, active state와 visibility toggle을
표시한다. Row click은 canonical `object activate`, visibility button은 canonical `object visibility`를
호출한다. Renderer는 active object 하나만 교체하는 대신 모든 effectively-visible object의 모든
representation packet을 하나의 frame snapshot으로 합성한다. Packet pick IDs는 합성 중 다시 mapping되지만
각 target의 scene node identity는 보존된다. 비활성 object를 click-pick하면 해당 object를 먼저 canonical
operation으로 활성화한 뒤 `picked` named selection을 그 object에 만든다.

현재 panel에는 rename/delete/group/reorder, per-object transform, solo/lock/fixed, representation subtree와
drag-and-drop이 없다. Active object를 숨길 수 있으며 이때 analysis command는 계속 active object를 대상으로
하지만 renderer에는 나타나지 않는다. Group ancestor visibility UI와 multi-object analysis selection은 후속
capability다.
