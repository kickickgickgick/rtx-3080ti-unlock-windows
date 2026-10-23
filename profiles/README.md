# Local machine profile

이 디렉터리의 `local-profile.json`은 `tools/make_profile.ps1`가 읽기 전용 probe의 `PROFILE.CFG`와 `PROM.BIN`을 검증한 뒤 생성합니다.

subsystem ID, PCI BDF, BAR0 주소나 PCIe 경로를 직접 입력하거나 예제 숫자를 복사하지 마세요. 로컬 프로필과 빌드 중 생성되는 `unlock/build/gu_profile_generated.h`는 Git에서 제외되며, 생성된 EFI도 해당 시스템 전용입니다.

전체 절차는 [`docs/USB-BOOT.md`](../docs/USB-BOOT.md)를 따르세요.
