# USB 준비 및 부팅

준비 과정은 읽기 전용 **프로필 수집 단계**와, 그 결과로 빌드한 **unlock 실행 단계**로 나뉩니다. 사용자가 subsystem ID, BDF, BAR0 주소 또는 PCIe 포트 번호를 직접 입력할 필요는 없습니다.

## 준비

- x64 UEFI 환경, Secure Boot 해제. 펌웨어 화면 출력이 대상 GPU를 통과하면 RTX GOP를 잠시 분리하는 동안 화면이 멈추거나 꺼질 수 있습니다.
- 비어 있는 FAT32 USB. 포맷이 필요하면 USB의 기존 파일을 먼저 백업하고 대상 장치를 확인합니다.
- LLVM/clang, lld-link와 Python 3가 설치된 Windows 빌드 환경.
- `unlock/assets.json`과 이름·크기·SHA256이 일치하는 자산 5개. 자산 준비 방식은 [상위 프로젝트의 firmware 문서](https://github.com/Zagitta/gpu-unlock/tree/0ebdb1c6308b6a2072be5bf04513472942541956/firmware)를 참고합니다.

Windows 장치 암호화/BitLocker를 사용하는 PC에서는 펌웨어 설정 변경 전에 복구 키를 확인해 두세요.

## 1단계: 읽기 전용 프로필 수집

저장소 루트에서 probe를 빌드합니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build-probe.ps1
```

`build/probe/usb-root` **안의 내용**을 비어 있는 FAT32 USB 최상위에 복사합니다. 이 USB로 부팅하면 probe는 다음 순서로 동작합니다.

1. PCI ID가 `10de:2208`이고 display class인 RTX 3080 Ti 후보만 표시합니다.
2. 후보가 하나면 자동 선택하고, 여러 개면 30초 안에 화면의 번호를 누르게 합니다. Esc는 취소입니다.
3. 선택한 GPU의 subsystem ID, BDF, 두 PCI device-path 노드, BAR0 리소스, 칩·VRAM·콜드 selector와 PROM을 읽습니다.
4. GPU MMIO/PCI 상태를 변경하거나 reset하지 않고 `RTX3080TI-PROBE/RUNxx/`에 `REPORT.TXT`, `PROM.BIN`, `PROFILE.CFG`를 저장합니다.

`PROFILE.CFG`가 없다면 수집값이 완전하지 않은 것입니다. unlock 단계로 진행하지 말고 `REPORT.TXT`를 확인하세요. PCIe switch 등으로 PCI 노드가 두 개보다 많은 경로는 현재 의도적으로 거부됩니다.

Windows로 돌아온 뒤 수집 파일에서 비공개 로컬 프로필을 만듭니다. `X:`는 실제 USB 드라이브 문자와 run 번호로 바꾸세요.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\make_profile.ps1 `
  -ProbeConfig X:\RTX3080TI-PROBE\RUN00\PROFILE.CFG `
  -Prom X:\RTX3080TI-PROBE\RUN00\PROM.BIN
```

이 스크립트는 PROM의 FWSEC 구조와 probe의 읽기 전용 증명·GA102 정책을 검사한 뒤 `profiles/local-profile.json`을 생성합니다. 숫자를 수동 편집해 검사를 우회하지 마세요. 이 파일과 probe 출력은 시스템 식별값을 포함하므로 공개 저장소에 올리지 않습니다.

## 2단계: unlock USB 만들기

프로필 생성 후 EFI를 빌드하고, 자산 5개를 로컬 `assets` 디렉터리에 준비합니다. 아래 명령은 새 로컬 폴더를 만들며 USB를 포맷하거나 자동으로 선택하지 않습니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\build.ps1 `
  -Profile .\profiles\local-profile.json
python .\tools\package_usb.py --assets .\analysis\pinned-assets-595.58.03 --output .\usb-package
```

`--assets`를 생략하면 실행 자산을 뺀 템플릿이 만들어집니다. 템플릿의 `ASSETS/REQUIRED-ASSETS.json`은 필요한 파일 목록입니다. 자산 파일 자체가 아니므로, 5개 파일을 별도로 준비해야 합니다.

완성된 `usb-package` **안의 내용**을 USB 최상위에 복사합니다. `usb-package` 폴더 자체를 한 단계 아래에 넣지 않습니다.

```text
USB:/
  EFI/BOOT/BOOTX64.EFI
  RTX3080TI-UNLOCK/
    ASSETS/
      booter_load_ga102.bin
      v67-ga102-595.58.03.bin
      gsp_ga10x.bin
      gsp_boot_ga102.bin
      gsp_boot_desc_ga102.bin
    LICENSES/
    SOURCE.zip
  USB-README.md
  PACKAGE.json
  SHA256SUMS.txt
```

`RTX3080TI-UNLOCK` 폴더는 로그를 쓸 수 있어야 합니다. `BOOTX64.EFI`만 복사한 상태에서는 실행할 수 없습니다. 이 앱은 파일 내용과 보드 조건을 다시 검사합니다.

## unlock 실행

1. Windows 작업을 저장하고 PC를 완전히 종료한 뒤 전원을 켭니다. 이미 드라이버가 사용한 GPU의 warm 상태에서는 검사가 거부될 수 있습니다.
2. 메인보드의 부팅 메뉴에서 **UEFI USB**를 선택합니다.
3. 앱이 현재 GPU를 빌드에 사용한 로컬 프로필과 다시 대조합니다. `READY`까지 사전 검사가 끝나면 **U를 한 번** 누릅니다. Esc 또는 U 없이 30초 대기는 취소입니다.
4. 실행 중에는 USB를 뽑거나 재부팅하지 않습니다.
5. 앱은 GOP 해제 직전의 전체 `EFI_PCI_IO_PROTOCOL` 속성을 저장합니다. 해제 직후와 FLR 이후에는 BME만 제외한 동일 속성을 복원·검증하고, GOP를 재연결하지 않은 채 미리 읽어 둔 내부 ESP의 **Windows Boot Manager를 직접 실행**합니다.

화면은 GOP 해제 시점부터 멈추거나 꺼지는 것이 정상입니다. 검증된 실행에서는 Windows NVIDIA 드라이버가 초기화되면서 화면이 다시 출력됐습니다. Windows 부팅음이 들린 뒤에도 화면이 돌아오지 않으면 충분히 기다린 다음 전원 버튼으로 종료하고 USB를 제거해 USB의 실행 로그를 회수합니다. 이 저장소는 Windows 자동 진단 예약 작업을 설치하지 않습니다.

`STOP / POWER OFF`가 표시되면 전원 버튼으로 PC를 끕니다. 그 화면에서 Windows로 계속 진행하지 않습니다. 다시 시도하기 전에 `RTX3080TI-UNLOCK/RUNxx/REPORT.TXT`를 확인합니다.

## 적용 범위

UEFI 성공은 Windows에서의 장시간 안정성을 보장하지 않습니다. 먼저 장치 상태를 확인하고 자신의 작업에서 성능과 전력·온도를 비교하세요. 언락은 휘발성이며 일반 재부팅 후에는 다시 적용해야 합니다.

GPU를 다른 PCIe 슬롯으로 옮기거나 VBIOS·메인보드·PCI 리소스 배치가 바뀌면 probe부터 다시 실행해 새 프로필로 빌드하세요. 한 PC에서 만든 `local-profile.json`이나 `BOOTX64.EFI`를 다른 PC에 재사용하지 마세요.

기존 Windows 디스크의 부트로더를 바꿀 필요는 없습니다. USB 사용을 중단하려면 USB를 제거하고 Windows Boot Manager로 부팅합니다.
