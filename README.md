# RTX 3080 Ti UEFI Unlock Port

이 저장소는 `Zagitta/gpu-unlock` 계열의 GA102 연산 unlock 절차를 RTX 3080 Ti에 맞춘 실험 포트입니다. UEFI에서 대상 GPU의 펌웨어 드라이버를 분리하고 unlock 트랜잭션을 실행한 뒤, 펌웨어 GOP를 재연결하지 않고 내부 Windows Boot Manager를 직접 chainload합니다.

PCI BDF, subsystem ID, BAR0 주소와 PCIe 경로를 소스에 고정하지 않습니다. 별도의 읽기 전용 probe가 현재 시스템에서 값을 수집하고, 사용자는 여러 RTX 3080 Ti가 발견된 경우 대상 GPU 번호만 선택합니다. 생성된 로컬 프로필과 실행 시 다시 읽은 상태가 정확히 일치하지 않으면 unlock 앱은 실행을 거부합니다.

이 구조가 모든 RTX 3080 Ti를 지원한다는 뜻은 아닙니다. 프로필 생성기는 현재 검증 정책과 다른 칩·VRAM·BAR 구조·PROM·콜드 selector·복잡한 PCIe 스위치 경로를 거부하며, 프로필이 생성되더라도 다른 보드와 펌웨어의 안전성은 보장되지 않습니다.

## 검증 범위

- PCI ID `10de:2208` RTX 3080 Ti 후보의 런타임 선택
- probe가 수집한 로컬 보드·PCI·PROM 프로필을 사용하는 fail-closed 빌드
- 검증된 Windows NVIDIA 드라이버: `596.02`
- 고정 GSP 자산: embedded `595.58.03` 이미지
- 대상 GPU가 펌웨어 화면 출력을 담당하는 headless 전환 경로
- Secure Boot 해제 환경

v0.14의 한 로컬 검증 환경에서는 UEFI selector 유지와 FLR 성공, Windows PnP 및 `nvidia-smi` 정상 상태, 화면 복구와 예상된 Tensor 처리량 변화를 확인했습니다. 이는 다른 RTX 3080 Ti나 다른 펌웨어에서의 안전성·호환성을 보장하지 않습니다.

GPU VBIOS나 Windows 부트 파일에는 쓰지 않지만, 실행 중 GPU MMIO·PCI 상태·펌웨어 실행과 FLR을 수행합니다. 실패하면 화면 정지, 강제 전원 종료, 장치 오류 또는 부팅 실패가 발생할 수 있습니다. 변경은 휘발성이며 USB 없이 다시 부팅하면 원래 제한 상태로 돌아옵니다.

## headless 부팅 흐름

1. 내부 GPT ESP의 `\EFI\Microsoft\Boot\bootmgfw.efi`가 정확히 하나인지 확인하고 미리 로드합니다.
2. USB 로그와 다섯 자산, 보드·PROM·콜드 상태를 검증한 뒤 U 입력을 기다립니다.
3. RTX GOP 드라이버를 분리하고 GOP가 사라진 것을 확인합니다. 이때 화면이 멈추거나 꺼질 수 있습니다.
4. unlock, FWSEC/SEC2, FLR과 사후 상태 검사를 수행하고 모든 DMA 자원을 정리합니다.
5. 모든 DMA 자산을 해제하고 BME가 꺼진 것을 확인합니다. GOP 해제 전에 저장한 PCI 속성에서 BME만 제외한 값을 다시 적용해 VGA/legacy routing과 IO/MEM decode를 보존한 뒤 exclusive PCI 소유권을 반환합니다.
6. GOP가 없는 상태임을 확인한 뒤 Windows Boot Manager를 직접 시작합니다. Windows NVIDIA 드라이버가 화면을 복구하지 못하면 CLI 또는 검은 화면이 유지될 수 있습니다.

USB 구성과 실제 실행 절차는 [USB 준비 및 부팅](docs/USB-BOOT.md)을 따르세요. CLI 화면이 고정된 동안 사용할 복구 명령은 없습니다.

## 측정 결과

동일한 드라이버와 실행 파일로 4096×4096 GEMM을 실행하고, 각 precision마다 2초 warmup과 CUDA event 30개를 측정했습니다. 아래 값은 프로세스 3회 median의 median입니다. 전력과 온도는 `nvidia-smi`를 약 100ms 간격으로 기록했으며 외부 벽전력 측정값이 아닙니다.

| 상태 | FP32 | FP16 Tensor | BF16 Tensor | GPU 부하 평균 전력 | 최대 전력 | 최대 온도 | GPU 부하 평균 SM 클럭 |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| 제한 상태 | 24.571 TFLOPS | 70.367 TFLOPS | 70.437 TFLOPS | 309.05 W | 331.70 W | 74 °C | 1923.2 MHz |
| 언락 상태 | 24.620 TFLOPS | 129.930 TFLOPS | 131.994 TFLOPS | 344.89 W | 398.02 W | 78 °C | 1909.4 MHz |
| 변화 | +0.20% | +84.65% | +87.39% | +11.60% | +19.99% | +4 °C | -0.72% |

원시 벤치마크 JSON, 각 trial과 100ms 전력·온도 telemetry CSV는 [`results/`](results/)에 포함되어 있습니다. CSV 열 순서는 `timestamp, pstate, temperature.gpu, power.draw, power.limit, clocks.sm, clocks.mem, utilization.gpu, utilization.memory`입니다. 별도의 초기 언락 전·후 측정 JSON도 함께 보존했습니다.

이는 짧은 단일 GEMM 측정 결과이며 장시간 안정성, 게임 성능, GPU 수명 또는 모든 AI 작업의 향상률을 의미하지 않습니다.

## 클론 후 빠른 시작

Windows에서 Git, Python 3, LLVM/clang 및 `lld-link`를 사용합니다. x64 UEFI와 FAT32 USB가 필요하며 Secure Boot는 꺼야 합니다. 아래의 `X:`는 사용자가 확인한 USB 드라이브 문자로 바꾸세요. 어떤 스크립트도 USB를 자동 선택하거나 포맷하지 않습니다.

### 1. 읽기 전용 probe

```powershell
git clone <repository-url> rtx3080ti-unlock
cd .\rtx3080ti-unlock

powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\build-probe.ps1
Copy-Item -Path .\build\probe\usb-root\* -Destination X:\ -Recurse -Force
```

이 USB로 완전 종료 상태에서 부팅합니다. RTX 3080 Ti가 하나면 자동 선택되고, 여러 개면 사용자가 화면의 번호만 선택합니다. `U`는 누르지 않습니다. 성공하면 `X:\RTX3080TI-PROBE\RUNxx\`에 `REPORT.TXT`, `PROFILE.CFG`, `PROM.BIN`이 생성됩니다.

### 2. 로컬 프로필

`RUNxx`를 실제 생성된 디렉터리명으로 바꿉니다. subsystem ID, BDF, BAR0, PCIe 포트, ACPI root 및 PROM 해시는 직접 입력하지 않습니다.

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\tools\make_profile.ps1 `
  -ProbeConfig X:\RTX3080TI-PROBE\RUNxx\PROFILE.CFG `
  -Prom X:\RTX3080TI-PROBE\RUNxx\PROM.BIN
```

검증을 통과하면 Git에서 제외되는 `profiles/local-profile.json`이 생성됩니다. 실패한 입력을 통과시키기 위해 `PROFILE.CFG`, 프로필 생성기 또는 검증 상수를 수정하지 마세요.

### 3. 고정 펌웨어 자산

라이선스 때문에 다섯 자산 자체는 저장소에 포함하지 않습니다. 정확히 일치하는 NVIDIA `595.58.03` 소스 트리와 Windows `596.02` 드라이버 패키지를 사용합니다. 아래 경로는 각자 압축을 푼 위치로 바꿉니다.

```powershell
$assets = ".\assets\595.58.03"

python .\tools\extract_boot_assets_595.py `
  "C:\path\to\NVIDIA-open-gpu-kernel-modules-595.58.03" $assets

python .\tools\generate_v67_595.py `
  "$assets\booter_load_ga102.bin" `
  "$assets\v67-ga102-595.58.03.bin"

Copy-Item `
  "C:\path\to\extracted-596.02\Display.Driver\gsp_ga10x.bin" `
  "$assets\gsp_ga10x.bin"
```

추출기와 패키지 도구는 파일 크기 및 고정 SHA-256을 검사합니다. 다른 버전을 비슷해 보인다는 이유로 이름만 바꾸어 사용하지 마세요. 요구되는 이름과 해시는 `unlock/assets.json`에 있습니다.

### 4. 빌드, 검사 및 USB 패키지

```powershell
powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\build.ps1 `
  -Profile .\profiles\local-profile.json

powershell.exe -NoProfile -ExecutionPolicy Bypass -File .\unlock\test-all.ps1

python .\tools\package_usb.py `
  --assets .\assets\595.58.03 `
  --output .\usb-package
```

`usb-package` 안의 내용을 FAT32 USB 최상위에 복사합니다. 패키지의 `ready_for_boot: false`와 `files-complete-hardware-preflight-required`는 오류가 아니라, 새 시스템의 실제 부팅 검증이 아직 수행되지 않았다는 표시입니다. 최종 실행과 복구 절차는 [USB 준비 및 부팅](docs/USB-BOOT.md)을 따르세요.

`profiles/local-profile.json`, probe 출력, 펌웨어 자산, 빌드 결과와 생성된 C 헤더는 Git에서 제외됩니다. 생성된 EFI에는 로컬 프로필 값이 포함되므로 다른 PC나 다른 PCIe 슬롯에서 재사용하거나 범용 바이너리로 배포하지 마세요.

### AI 에이전트에 맡길 때

AI 에이전트는 위 명령의 경로 확인, 빌드, 로그 해석과 패키징을 도울 수 있습니다. 다만 다음 원칙을 지키도록 요청하세요.

- 사용자가 확인하지 않은 드라이브를 포맷하거나 USB 대상으로 선택하지 않습니다.
- probe가 만든 숫자를 수동 입력하거나 다른 사용자의 프로필·PROM·EFI를 재사용하지 않습니다.
- 해시, PROM/FWSEC, cold selector, PCI/GOP 경로 검사를 수정하거나 우회하지 않습니다.
- `make_profile.ps1` 또는 빌드가 입력을 거부하면 unlock 단계로 진행하지 않습니다.
- 로컬 프로필, PROM, 펌웨어 자산과 생성된 EFI를 공개 커밋에 포함하지 않습니다.

공개 소스만으로 실행되는 검사와 개발자용 로컬 fixture 검사의 차이는 [TESTING.md](TESTING.md)에 있습니다.

## 출처와 라이선스

포트와 재사용 코드는 GPL-2.0-only입니다. 핵심 코드는 [Zagitta/gpu-unlock](https://github.com/Zagitta/gpu-unlock/tree/0ebdb1c6308b6a2072be5bf04513472942541956) 및 3080 포트 계열에서 유래했습니다. EDK2 및 NVIDIA 관련 파일은 [외부 코드 고지](THIRD_PARTY_NOTICES.md)와 각 라이선스를 따릅니다.
