# 빌드 및 검증

## 기본 검사

Windows에서 README의 컴파일러와 Python을 설치한 뒤 저장소 루트에서 실행합니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build-probe.ps1
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\build.ps1 `
  -Profile .\profiles\local-profile.json
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\test-all.ps1
```

기본 검사는 probe, 3080 Ti PROM 정책, port/Bus Master, runtime, PE 거부 검사, USB 패키지와 프로필 파서의 정상·거부 경로를 실행합니다. GPU와 UEFI 서비스는 모의 환경입니다.

Python 패키지 검사만 실행하려면 다음 명령을 사용합니다.

```text
python -m unittest discover -s tools -p "test_*.py" -v
```

## 로컬 fixture를 사용하는 추가 검사

다음 파일을 준비한 뒤 `-WithLocalFixtures`를 지정합니다.

- `analysis/pinned-assets-595.58.03/`: `unlock/assets.json`의 5개 자산. GSP 입력은 Windows 596.02 패키지의 embedded 595.58.03 파일입니다.
- `evidence/rtx3080ti-run00/PROM.BIN`: 비공개 1MiB PROM fixture. `evidence/`는 공개 소스에서 제외됩니다.
- `profiles/local-profile.json`: `tools/make_profile.ps1`로 probe 출력에서 생성한 비공개 프로필.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\test-all.ps1 -WithLocalFixtures
```

58개 저수준 transaction, SHA256, EFI transaction, 현재 PCI-attribute-preserving headless 앱 경로와 FWSEC 순서 비교 검사가 추가됩니다. 앱 검사는 GOP 해제 전 속성 저장, BME를 제외한 속성 복원, selector 유지, 직접 Boot Manager chainload와 Boot Manager가 반환할 때의 안전 정지를 확인합니다. PROM fixture는 기록된 보드의 회귀 검사 입력입니다.

## 소스와 패키지 해시

`SOURCE-MANIFEST.json`에는 공개 소스 파일 목록과 SHA256이 있습니다. USB 패키지 도구는 이 목록에 있는 파일만 `SOURCE.zip`에 넣고 빌드 입력·EFI·실행 자산을 검사합니다. 개인 작업 파일과 빌드 캐시는 패키지에 넣지 않습니다.

소스를 수정한 개발자는 새 파일을 Git에 추가하고 소스 manifest를 갱신한 뒤 로컬 프로필을 명시하여 다시 빌드·검사합니다.

```powershell
git add .
python .\tools\source_manifest.py
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\build.ps1 `
  -Profile .\profiles\local-profile.json
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\test-all.ps1
```

`tools/source_manifest.py`는 Git의 추적 파일을 기준으로 동작합니다. ZIP으로 받은 소스는 제공된 manifest를 사용해 그대로 빌드하고 패키징할 수 있습니다.

검사 결과는 `unlock/build/test-results.json`에 기록합니다. host/mock 통과와 실제 USB 부팅 결과는 별개입니다.
