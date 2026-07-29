# Pokemon Ball

제스처 인식 기반 포켓몬 포획 게임 — ESP32-S3 + 원형 LCD + BNO055 IMU

## 하드웨어

| 부품 | 모델 | 인터페이스 |
|------|------|-----------|
| MCU | Seeed XIAO ESP32S3 | — |
| 디스플레이 | Seeed Round Display (GC9A01 240×240) | SPI |
| IMU | GY-BNO055 | I2C (0x29, SDA=GPIO5, SCL=GPIO6) |
| 터치 | ESP32-S3 내장 정전식 터치 | T1 = GPIO1 |

## 게임 흐름

```
흔들기 → BNO055 데이터 수집 → Edge Impulse 제스처 추론
  → 제스처 임시 저장 (포켓볼 "TOUCH!" 표시)
    → 터치 → 제스처 확정 → 포켓몬 등장
      → 2% 확률: 히든 캐릭터 "산지니" 등장
        → MQTT publish → 웹 대시보드 실시간 갱신
```

## 프로젝트 구조

```
pokemon_ball/
├── pokemon_ball.ino     # 메인 — setup/loop
├── config.h             # 설정 + 제스처→포켓몬 매핑 테이블
├── display.{h,cpp}      # LCD 모듈 (포켓볼 드로잉 + JPEG 렌더링)
├── gesture.{h,cpp}      # AI 모듈 (BNO055 + Edge Impulse)
├── touch.{h,cpp}        # 터치 모듈 (디바운스)
├── mqtt_client.{h,cpp}  # MQTT 모듈 (publish)
├── game.{h,cpp}         # 상태머신 (게임 로직)
└── web/
    └── index.html       # 웹 대시보드 (MQTT WebSocket subscriber)
```

## 빌드

```powershell
arduino-cli compile --fqbn esp32:esp32:XIAO_ESP32S3 `
  --library "C:\Users\<user>\Documents\Arduino\libraries\TFT_eSPI" `
  --library "C:\Users\<user>\Documents\Arduino\libraries\JPEGDecoder" `
  pokemon_ball

arduino-cli upload -p COMx --fqbn esp32:esp32:XIAO_ESP32S3 pokemon_ball
```

### 필요 라이브러리

- TFT_eSPI (User_Setup: Setup66_Seeed_XIAO_Round.h)
- JPEGDecoder
- Adafruit BNO055
- PubSubClient

## Edge Impulse 연동

`config.h`에서 `#define USE_EDGE_IMPULSE` 주석 해제 후 `gesture_inferencing.h` 설치.
주석 상태에서는 stub 모드로 동작 (BNO055 데이터 기반 가상 제스처).

## 설정 변경 (`config.h`)

| 항목 | 설명 |
|------|------|
| `WIFI_SSID` / `WIFI_PASS` | WiFi STA 접속 정보 |
| `MQTT_HOST` / `MQTT_PORT` | MQTT 브로커 주소 |
| `POKEMON_TABLE[]` | 제스처→포켓몬 매핑 (행 추가/수정으로 확장) |
| `HIDDEN_NAME` / `HIDDEN_PROBABILITY` | 히든 캐릭터 이름 / 등장 확률(%) |
| `TOUCH_PIN` / `TOUCH_THRESHOLD` | 터치 핀 / 임계값 |
| `SHAKE_THRESHOLD` | 흔들기 감지 임계값 (m/s²) |

## 웹 대시보드

`web/index.html`을 브라우저에서 열고 MQTT 브로커의 WebSocket 주소 입력.

기능:
1. 현재 획득 포켓몬 표시
2. 현재 AI 인식 제스처 표시
3. 포켓몬 도감 (획득/미획득/히든)
4. 제스처별 등장 확률 표시
