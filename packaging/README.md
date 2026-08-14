# Packaging

이 디렉토리는 공개 release artifact와 installer 설정만 관리한다. 현재는 설계 placeholder이며
실제 installer target은 아직 없다.

- `macos/`: `.app` bundle, installer, signing과 notarization
- `linux/`: portable/package artifact와 desktop integration
- `windows/`: installer, Start Menu, signing과 uninstall integration

모든 installer는 clean environment에서 install, launch, upgrade와 uninstall smoke test를
통과해야 한다. Certificate와 credential은 파일로 commit하지 않고 CI secret store를 쓴다.
