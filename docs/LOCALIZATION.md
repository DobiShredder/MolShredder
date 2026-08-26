# Localization

MolShredder Desktop은 영어(`en`)와 한국어(`ko`)를 기본 지원한다. 첫 실행은 OS locale을 사용하고,
지원하지 않는 locale은 영어로 fallback한다. Toolbar의 언어 control은 실행 중 언어를 바꾸고 선택을
application setting에 저장한다. 개발 및 smoke test에서는 다음처럼 명시할 수 있다.

```bash
molshredder_desktop --language=en
molshredder_desktop --language=ko
molshredder_desktop --language=system
```

Desktop presentation 문자열은 Qt Linguist를 사용한다. QML의 사용자 표시 문자열은 `qsTr()`로 표시하고
한국어 catalog는 `translations/molshredder_ko.ts`에 둔다. 새 언어를 추가하려면 해당 `.ts` file을
`qt_add_translations()`의 `TS_FILES`에 추가하고 다음 명령으로 catalog를 갱신·검증한다.

```bash
cmake --preset desktop
cmake --build --preset desktop --target update_translations
cmake --build --preset desktop --target release_translations
ctest --preset desktop -R localization --output-on-failure
```

번역은 UI presentation에만 적용한다. Operation ID, command noun/option, JSON field, file format token,
unit symbol과 scientific provenance는 locale과 무관한 stable contract를 유지한다. Core error는 stable
error code와 structured context를 제공하고 frontend가 사용자 설명을 번역하는 방향으로 확장한다.
번역 문자열의 `%1`, `%2` placeholder는 원문과 동일하게 유지해야 하며 catalog test가 누락, 빈 번역,
중복과 placeholder drift를 거부한다.

현재 catalog는 QML UI chrome 150개를 포함한다. Typed operation에서 전달되는 동적 status/error detail은 stable
code/context의 presentation mapping이 아직 없어 일부 영어로 표시될 수 있다. 이를 한국어 지원 완료로 과장하지 않으며,
workflow menu/action metadata를 정리하는 다음 UX 단계에서 code별 번역과 remediation catalog를 연결한다.
