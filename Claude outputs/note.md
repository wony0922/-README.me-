# AI 작업노트 — Ore Deal (TeamProject)

> 새 대화에서 Claude에게: "Docs/AI_작업노트.md 읽고 이어서 하자" 라고 하면 됩니다.
> 마지막 정리: 2026-11-15 (주제별로 다시 정리. 날짜별 원본 기록은 `Docs/AI_작업노트_이전기록.md`)
> 사용자 선호: "푸쉬할거야" 하면 GitHub Desktop 커밋/푸쉬를 **직접 하지 말고 단계별로 안내**.

---

## 1. 지금 상태 한눈에
- 2~4인 LAN 멀티 보드/카드게임(Listen Server). 메인메뉴 → 방 만들기/참가 → `Lvl_Table`(살롱 맵)에서 플레이.
- 턴은 4단계: **1/4 매립 → 2/4 경매 거래 → 3/4 가져온 자원 배치 → 4/4 보급(드로우)**. 경매 거래는 C++(`OreDealAuctionBroker`)로 구현됨.
- 상점(F 키, C++): 판매 탭(마차 통째로 판매)·구매 탭(나무 구매) 동작 확인.
- UI 전체가 크림색 픽셀아트(Everyday Life Dialogue UI) + Mona 픽셀 폰트로 통일됨(메인메뉴, HUD, 대화창, 경매 안내, 상점, 결과 화면).
- 버그 수정 담당 구분: 팀원 C++ 규칙 코드의 버그(자원 5종/8종 불일치, 덱 소진 시 2단계 정지, 1장 판매 조건 등)는 **팀원 담당**, 손대지 않음.

## 2. 용어 (2026-11-14 사용자 정의, 설명할 때 이 이름만 사용)
| 용어 | 뜻 | 코드상 저장 위치 |
|---|---|---|
| **손패** | 플레이어가 들고 있는 카드 | 폰 `BP_SeatedPlayer.HandCards` |
| **가져온 카드** | 거래 단계에서 가져와 마차에 반드시 배치해야 하는 카드(가져온 순서대로) | `AOreDealAuctionBroker` 좌석의 `PendingCards` |
| **덱 위 카드** | 덱 위에 펼쳐진 카드 전부(처음 펼친 2장 중 안 가져간 카드 + 턴 플레이어가 손패에서 올린 카드) | `BP_CardDeck.RevealedCards` 배열 하나(빈 칸 -1 무시) |
| **거래 카드** | 덱 위 카드 중 턴 플레이어가 이번에 거래하려고 고른 카드(강조 표시) | `AOreDealAuctionBroker.LotIndices` |
| **제안 카드** | 다른 플레이어가 거래 단계에서 손패에서 골라 낸 카드 | 좌석의 `OfferCards` / `OfferHandIndices` |
| **마차 1 카드 / 마차 2 카드** | 마차에 배치된 카드. 플레이어 기준 왼쪽 = 마차 1 | `BP_CardBoard.WagonTypes/WagonCounts` 인덱스 0 / 1 |

## 3. 환경
- UE **5.8**, 블루프린트 + C++ 모듈 `Source/TeamProject`(팀원 작성 + 내가 추가한 파일). Visual Studio 빌드 필요.
- 작업 폴더: **`C:\Project\-README.me-`** (TeamProject.uproject). 예전 경로 `C:\프로젝트\게임가상현실\...`는 더 이상 안 씀.
- Blender 원본: `GameVR.blend`, 내보내기 폴더 `...\Export\` (캐릭터·손·살롱).
- 기준 규칙 문서: 루트 `README.md`(Ore Deal 규칙). 기존 Furnace/연료 명칭은 Wagon/보급품으로 읽는다.
- 언리얼 MCP 자동 시작: 에디터 설정 `ModelContextProtocolSettings.bAutoStartServer = true`.

## 4. 빌드 방법과 주의
- **cpp만 바뀜** → 에디터 Cmd에 `LiveCoding.Compile`(약 40초). 성공/실패는 출력 로그 `LogLiveCoding`.
  - 실패해도 오류 문장은 Live Coding 콘솔 창에만 나옴 → 아래 전체 빌드로 로그를 받아 확인.
  - 익명 네임스페이스의 상수 배열 같은 변경은 Live Coding으로 반영 안 된 적 있음(11/15 `OreDealPreGameComponent`) → 전체 빌드.
- **헤더 변경 / 확실히 반영해야 할 때** → 에디터 종료 후 전체 빌드:
  `"C:\Program Files\Epic Games\UE_5.8\Engine\Build\BatchFiles\Build.bat" TeamProjectEditor Win64 Development -Project="C:\Project\-README.me-\TeamProject.uproject" -WaitMutex` (약 10~80초) → 에디터 재실행.
  - Blender MCP의 Python `subprocess`로 실행 가능. 로그는 `Saved/Logs/DialogueUIBuild.log` 등에 저장해서 확인.
  - 에디터 종료/재실행도 Blender Python에서: 엔진 `Engine/Plugins/Experimental/PythonScriptPlugin/Content/Python/remote_execution.py` import → 미저장 확인(`EditorLoadingAndSavingUtils.get_dirty_*_packages()`) → `unreal.SystemLibrary.quit_editor()` → 빌드 → `UnrealEditor.exe TeamProject.uproject`. 재실행 후 언리얼 MCP는 자동으로 다시 붙음.
- **unity 빌드 이름 충돌 주의**: cpp를 고치면 Adaptive Build가 그 파일을 unity 묶음에서 빼면서 다른 파일끼리 새로 묶임 → 익명 네임스페이스에 **같은 이름**이 있으면 C2374/C2084로 깨짐.
  - 그래서 `OreDealTablePlayerController.cpp`의 `ResourceNames` → `ControllerResourceNames`로 변경함.
  - 새 헬퍼 함수/상수는 파일마다 다른 이름으로(예: `InteractionPixelFont` / `AuctionPixelFont` / `StorePixelFont`, `MakeDuiBrush` / `MakeAuctionDuiBrush`).
- **파일 올리기 주의(device_commit_files)**: 같은 stagedPath로 다시 올리면 이전 내용이 들어갈 수 있음 → 수정본은 새 파일 이름으로 올리고 디스크 md5로 확인.

## 5. 작업 주의사항 (시행착오)
- **PIE 실행 중 블루프린트 컴파일 금지** → 에디터 크래시로 미저장 작업 손실된 적 있음. 툴 작업 전 `IsPIERunning` 확인. 순서: PIE 정지 → 컴파일 → 저장 → PIE.
- **Git LFS `lockable`** 때문에 .uasset/.umap이 읽기 전용 → 저장 실패, 지운 액터가 레벨 재로드 시 부활. 해결: `attrib -r "C:\Project\-README.me-\Content\*" /s` (폴더 속성창의 "읽기 전용" 체크가 다시 보이는 건 정상).
- 레벨에 **이미 배치된 액터는 BP 템플릿 컴포넌트 변경이 반영 안 됨** → 액터 제거 후 같은 트랜스폼/라벨/SeatIndex/폴더로 재배치.
- **RPC(서버/클라 실행) 설정·커스텀 이벤트 파라미터 타입은 툴로 불가** → 에디터 UI(디테일 패널 "리플리케이트")로만. 사용자가 직접 클릭하는 게 가장 저렴.
- 툴 `set_actor_transform`에 location만 넘기면 **회전이 0으로 리셋** → 항상 location/rotation/scale 전부 넘길 것.
- 리플리케이트된 Rotator는 0~360으로 올 수 있음 → `Delta(Rotator)`로 정규화 후 사용.
- 리슨 서버 호스트는 서버 RPC가 **즉시 실행**됨 → 응답 대기 플래그는 RPC **호출 전**에 세팅.
- 툴 `import_file`은 기존 메시 에셋을 덮어쓰지 못함 → 새 이름으로 임포트 후 참조 교체. 슬롯 머티리얼은 import 후 set_material.
- bpy: `mesh.materials.clear()`는 폴리곤 material_index를 0으로 리셋함.
- 컴포넌트 스케일을 툴로 바꾸면 컨스트럭션 재실행으로 되돌아갈 수 있음 → 액터 스케일로 처리.

## 6. 테스트(PIE) 방법
- 에디터 PIE 설정: 넷 모드 **Standalone**, 플레이어 2(또는 3). 어느 레벨에서 Play해도 메인메뉴부터 시작.
- **가장 확실한 방법 — 원격 Python으로 세션 묶기**(Blender MCP Python → `remote_execution`):
  `unreal.EditorLevelLibrary.get_pie_worlds(False)` → 0번 월드에 콘솔 `open /Game/MaterialTraders/Maps/Lvl_Table?listen`, 나머지에 `open 127.0.0.1:7777` → 서버 월드(게임모드 있는 쪽)에 `ke * StartGame`.
  - 골드는 PlayerState `AddGold` 호출, 마차 카드는 보드 `AddCard(종류, 마차, PS)` 호출로 채움(BP 변수는 `set_editor_property` 불가).
  - 위젯 상태 확인: `unreal.ObjectIterator(unreal.Widget)`로 경로에 `GameInstance` 들어간 위젯을 찾아 `get_visibility()` 등 확인.
- 메뉴 버튼으로 하는 방법: 작은 PIE 창의 "방 만들기"(SlateInspector `Click`이 먹힘) → 에디터 Cmd에 `open 127.0.0.1:7777`(Cmd 명령은 뷰포트 쪽 월드에 실행됨). `open ?listen`으로 연 방은 방 목록에 안 뜸(IP 접속은 됨).
- 키 입력: SlateInspector `Click`(조준점 "•" 텍스트 ref) 후 `PressKey`. 키가 안 먹히면 원격 Python으로 대체.
- 콘솔 팁: `ke * 함수명`(서버 월드 실행), `getall 클래스 프로퍼티`(예: `getall OreDealAuctionBroker Stage`, `Seats`), `slomo 0.15`, `ke * ServerStartDraw`(손패 채우기).
- PIE 창끼리 액터 이름이 다를 수 있음(서버 C_0 ≠ 클라 C_0). HandCount 등으로 대응 확인.
- 멀티 창에서는 마우스 캡처한 창이 키를 가져감(Shift+F1로 풀기).

## 7. MCP(언리얼) 툴 사용 팁 — 한글 에디터
- write_graph_dsl은 실행(exec) 핀을 인식 못함 → 노드는 `create_node`, 연결은 `connect_pins`로 직접.
- 노드 type_id 예시: `유틸리티|연산자|추가/빼기/곱하기/같음(==)/작음(<)/크거나같음(>=)`, `수학|부울|ANDBoolean`, `Variables|디폴트|GetX/SetX`(bool은 b 접두사 빠짐), `함수호출|FuncName`, `Class|BPPSTable|AddGold`, `Utilities|FlowControl|Branch`(ForEach 입력핀 'Exec', ForLoop 'execute'), `폰|GetControlRotation`(declaring_class=/Script/Engine.Pawn), `수학|로테이터|Delta(Rotator)`, `위젯|SetText(Text)`, `유저인터페이스|위젯생성`, `사용자인터페이스|뷰포트|AddtoViewport`.
- UMG 위젯 수정: `UMGToolSet`(GetWidgetDescription → WrapWidgets/RenameWidget …) + `ObjectTools.get/set_properties`(브러시·폰트는 get으로 받은 JSON을 고쳐서 set) → `CompileWidgetBlueprint` → `save_assets`. 여러 단계는 `ProgrammaticToolset.execute_tool_script`로 한 번에.
- 텍스처 가져오기: `TextureTools.import_file`. 폰트(.ttf) 가져오기·Font 에셋 구성은 원격 Python(`AssetImportTask`, `CompositeFont.import_text`).
- 엔진 헤더 확인이 필요하면 Blender Python으로 `C:\Program Files\Epic Games\UE_5.8\Engine\Source\...` 를 읽으면 됨.

## 8. 게임 흐름 / 조작 (현재)
### 8-1. 시작 흐름
- 메인메뉴(`Lvl_MainMenu`) → 방 만들기 / 방 참가(LAN 방 목록) → `Lvl_Table`.
- 대기 중: `BP_CardDeck.bGameStarted=false`. 하단 시작 패널(방장 "▶ 게임 시작 [SPACE]", 나머지 "방장이 게임을 시작하기를 기다리는 중...", "접속 인원 n / 4 (최소 2명)").
  - 대기 중 숨김(`OreDealPreGameComponent`): 덱·마차판·상인, 자원바(ResBar), 단계 글자, 키 가이드(TurnFrame), **상점 버튼**. 왼손은 테이블 위에 쉬는 자세.
- 방장 Space → GM.StartGame(2명 이상) → 모두 카드 5장(5장 묶음 드로우 애니). 턴은 `BP_CardDeck.TurnPS`, 방장부터 PlayerArray 순.

### 8-2. 턴 4단계 (`BP_CardDeck.TurnPhaseIndex`, `OreDealTurnBridge`)
1. **1/4 매립**: 손패 맨 앞 1장 필수 배치, 이후 추가 자유. A/D 마차 · Space 배치 · E 확정.
2. **2/4 경매 거래**(아래 8-3).
3. **3/4 가져온 자원 배치**: 전원이 가져온 카드를 마차에 배치하면 자동 진행. 전원 완료 전 E → "(이름)의 자원 배치가 끝나지 않았습니다".
4. **4/4 보급**: W 또는 덱 클릭으로 3장 드로우 · E 턴 종료.
- 마차 정산: 같은 종류 놓으면 +1, 다른 종류면 기존 청산(조건·가격은 팀원 C++ 정산표).

### 8-3. 경매 거래 규칙 (사용자 확정, C++ `OreDealAuctionBroker`)
1. 덱에서 2장 펼침 → 턴 플레이어가 C로 1장/2장 골라 Space로 가져감 → **가져온 카드**(덱 순서대로), 나머지는 **덱 위 카드**. 2장 다 가져가도 2번은 반드시 거침.
2. 턴 플레이어가 손패에서 원하는 카드를 덱 위에 올림(0장 가능).
3. 턴 플레이어가 덱 위 카드 중 **거래 카드** 여러 장 선택(강조).
4. 다른 플레이어는 손패에서 **제안 카드** 선택(머리 위 표시). 아무것도 안 고르고 Space = 거래 안 함.
5. 3인 이상: 턴 플레이어가 A/D로 상대 지목(카메라 회전) 후 Space. 2인은 지목 없음.
6. 성립 시 상대는 거래 카드 전부, 턴 플레이어는 제안 카드 전부 → 둘 다 **가져온 카드**로. 아무도 제안 안 하면 거래 카드는 턴 플레이어 몫.
7. 덱 위 카드가 없어질 때까지 3~6 반복 → 3/4 배치 단계(Consume)에서만 가져온 카드 배치 가능.
- Stage: Idle → PickReveal → ActiveOffer → SelectLot → Bidding → (ChoosePartner) → Consume.
- 덱 위 카드 배치 형태(`DeckSlotLocal`): 1장 중앙, 2장 Y ±9, 3장 이상 한 줄 3장·간격 15·넘치면 위로(+14z).

### 8-4. 조작키
| 키 | 기능 |
|---|---|
| 마우스 | 시야(좌우 ±35°) |
| 마우스 가운데(누르기) | 이모트 휠: 위 가리키기 / 오른쪽 비하하기 / 아래 엄지척 (왼쪽 비어 있음) |
| 마우스 왼쪽 | 조준점 대상 상호작용(덱 → 4단계 드로우) |
| A / D | 포커스 이동(마차 1/2, 덱 위 카드, 지목할 상대) |
| C | 여러 장 선택 토글 |
| Space | 게임 시작(방장) / 결정·배치 |
| Z / X | 내 화면 카드 이동(손패 선택 창, 손패 보기) |
| Q | 내 손패 보기 열기/닫기 |
| E | 단계 진행 / 4단계 턴 종료 |
| W | 드로우(4단계) |
| F | 상점 열기/닫기 |
| Enter | 채팅 |
| F9 | 디버그 즉시 종료(나중에 제거) |

### 8-5. 상점 (F, C++)
- 게임 시작 후에만 열림, 열려 있는 동안 키보드 독점(모달), 채팅 입력 중 F 무시, 닫힐 때 구매 확인 취소.
- 탭: 판매 / 구매 / 빈 칸(미정). 4x2 칸, 하단 골드판(증감 시 초록/빨강 강조). 마우스·방향키·Space/Enter 모두 지원. 화면이 좁으면 패널 자동 축소(최대 1배).
- 판매: 마차 통째로 판매(서버 `ServerSellWagon`, 내 턴 1~3단계). 구매: 확인 상자 → `ServerBuyItem`.
- 아이템 추가: `Private/OreDealStoreCatalog.cpp`의 `GetItems()`에 한 줄 = 한 칸 `MakePlayerStateItem(고유이름, 표시이름, 골드가격, 세트 개수, PlayerState 변수이름, 아이콘 경로)`. 현재 나무 2개 = 1골드(임시). 다른 지급 방식은 `EOreDealStoreGrant` + `UOreDealStoreComponent::GrantItem`.

## 9. 코드 / 에셋 현황
### 9-1. C++ (`Source/TeamProject`)
| 파일 | 역할 |
|---|---|
| `OreDealTablePlayerController` | 입력 총괄(`InputKey`에서 상점 → 경매 순으로 우선 처리), 경매·상점 컴포넌트 생성, 지목 카메라, 브로커 스폰 |
| `OreDealAuctionBroker` | 서버 권한 경매 상태(레벨에 자동 스폰, Replicated). 덱/마차판 찾기 월드별 캐시 |
| `OreDealAuctionComponent` | PC에 붙음. 경매 서버 RPC·키 처리·HUD 갱신 |
| `OreDealAuctionHudWidget` | 경매 상단 안내, 경고, 손패 선택 창, 가져온 카드 표시 |
| `OreDealAuctionDisplay` | 덱 위 카드·제안 카드·가져온 카드를 3D 평면으로 표시(로컬 전용) |
| `OreDealStoreCatalog / Component / Widget` | 상점 아이템 목록 / F 입력·구매 RPC / 화면 |
| `OreDealInteractionWidget` | 좌상단 대화창·채팅, 상점 버튼(`StoreButton`), 판매 토스트, 조준점 |
| `OreDealHandViewWidget` | Q 손패 보기(좌하단, Z/X 이동) |
| `OreDealPreGameComponent` | 게임 시작 전 숨김 처리(액터·HUD·손 자세) |
| `OreDealTurnBridge` | 턴 단계 진행·배치 허용 판정·단계 문구(키 가이드 2줄 요약) |
| `OreDealMarketBroker`, `OreDealTradeManager` | 예전 거래창(더 이상 스폰 안 함) / 팀원 규칙 코드 |

### 9-2. 블루프린트 (`/Game/MaterialTraders/...`)
- `BP_GM_Table`(게임모드: 시작·턴·종료·재시작, Standalone이면 메인메뉴로), `BP_PS_Table`(Gold, Wood, bReadyRematch), `BP_PC_Table`(C++ 컨트롤러 상속, HUD 생성), `BP_SeatedPlayer`(폰: 머리 회전, 손패, 드로우 애니, 이모트), `BP_CardBoard`(마차 2칸·정산), `BP_CardDeck`(펼친 카드, bGameStarted, TurnPS, TurnPhaseIndex), `BP_RoundTable`, `BP_Chair`, `BP_GM_Menu`/`BP_PC_Menu`.
- UI: `WBP_MainMenu`, `WBP_RoomEntry`, `WBP_HUD`(ResBar 나무/골드, StartPanel, TurnFrame>TxtTurn 키 가이드, TxtPhase는 숨김), `WBP_Result`, `WBP_EmoteWheel`.
- 레벨 `Lvl_Table`: 원형 테이블(반경 92.2), 의자 4, 마차판 `CardBoard_0~3`(SeatIndex 0~3, 폴더 Table/Boards, ±54.816), 덱 1개(폴더 Table). 살롱 메시 6개(폴더 Saloon, 위치 (40,-60,0)), 조명 11개(Saloon/Lights). 상인 액터는 삭제됨.

### 9-3. 동작 메모
- 머리 회전: 로컬 LookRot → `ServerSetLook` → 복제. 이모트는 별도 RPC 없이 **LookRot.Roll**에 실어 보냄(가리키기 90, 비하하기 160, 엄지척 -160).
- 드로우 애니: 서버가 `DrawSeq`만 복제, 각 컴퓨터가 같은 애니 재생(StartDrawAnim/TickDrawAnim/EvalTrack). 여러 장은 `DrawTypes` 배열.
- 손·손패는 2배 크기. `CardFan`, `RightHandRoot`, `FlyCard`는 애니가 트랜스폼을 덮어쓰므로 **자식**(`LeftHand`, `CardSlots`, `RightHand`, `FlyCardScale`)만 스케일.
- 카드 텍스처 순서: Stone, Coal, Gold, Iron, Diamond (`BP_CardBoard.FloatMats`와 같음). 부유 카드 회전 (P0,Y-90,R90).

## 10. UI 스타일 가이드
- 에셋 팩: Everyday Life Pixel Dialogue UI, **Cream** 테마, 720p(2배) PNG. 원본 `SourceArt/UI/Dialogue/T_DUI_*.png`(+README, ASSET_CATALOG.csv) → `/Game/MaterialTraders/UI/Dialogue/T_DUI_*`. 팩에 LICENSE.txt가 없었음 → 사용 조건은 구매처에서 확인 필요.
  - 텍스처: MenuPanel, Notification, Modal, Tooltip, Nameplate, Button/SmallButton Idle·Hover·Pressed, ScrollThumb/Track, SliderThumb/Track. 설정: Nearest, 밉맵 없음, UI 그룹, TC_EditorIcon, NeverStream.
- 브러시 규칙: Box, `Margin = 카탈로그 inset / 원본 크기`, `ImageSize = 원본 크기 x 2`(작은 버튼·스크롤바·입력창은 x1).
  - 패널 inset 12/8/16/8 (원본 MenuPanel 288x220, Notification 224x32, Modal 320x144, Tooltip 160x32), 버튼 20/14/16/10 (Button 144x32, SmallButton 64x32), 스크롤 Thumb 12x32(4/8/4/8), Track 8x64(0/4/0/4).
- 쓰임: 큰 패널 = MenuPanel(대화창, 방 목록, 결과), 한 줄 알림 = Notification(자원바, 키 가이드, 경매 안내, 토스트), 확인/경고 = Modal(시작 패널, 경매 경고), 작은 힌트 = Tooltip(손패 선택 힌트, 채팅 입력).
- 글자색(크림 위): 진갈색 **#613D3B**, 강조 **#B56C4C**, 경고 **#A83228**. 외곽선·그림자 끔.
- 폰트: **Mona12 KR**(Monad ABXY, SIL OFL 1.1). 원본 `SourceArt/Fonts/Mona/`(라이선스 포함) → `/Game/MaterialTraders/UI/Fonts/F_Mona12KR`(Regular/Bold, FontFace 힌팅 Monochrome·로딩 Inline). 12px 격자라 크기는 **9pt 배수**(9/18/27/36/45), 27 이상 Bold.
- C++에서는 각 위젯 cpp의 `MakeLabel`/`AddText`/`MakeButton`이 폰트·브러시를 적용. 상점 창(`OreDealStoreWidget`)은 자체 그림(STORE 시안) 그대로, 폰트만 Mona.

## 11. 아트 / 모델 / 최적화
- 캐릭터(현재): `/Game/Asset/Char2/SM_Char2_{Head,Body}_{Base,Cowboy,Ranger}`. `BP_SeatedPlayer`의 `CharacterMeshes`/`HeadMeshes` = [Cowboy, Ranger, Base]. 머리 원점 = 목, 머리 위치 z39. Blender 컬렉션 `MT_Character2`. (예전 `Player1/2`의 `SM_Char_*_R`은 미사용)
- 손(현재): `SK_Hand2b_L/R` + 애니 `SK_Hand2b_*_Anim_Open/Grip/Relax/Fist/ThumbsUp/Point`. 가리키기 손가락 문제(쿼터니언 키 vs 오일러 본) · 엄지척 손날 문제(웨이트) 해결됨. (예전 `Hands/`, `Hand2/`는 미사용)
- 살롱: Blender `MT_Saloon` → `/Game/Asset/Saloon/SM_Saloon_*`. 기존 `Floor`는 숨김+충돌 끔.
- 저사양 최적화: Lumen GI 끔, 반사 화면 공간, 일반 섀도 맵, 레이트레이싱·메시 거리 필드 끔. 캐릭터/살롱 LOD 추가, `chair1`·`bar_table` Nanite 끔. (월드 파티션이라 라이트 베이크 불가 → 조명 실시간)
- 입력: `IMC_Default`에서 IA_Move/IA_Jump 삭제(이동 없음).

## 12. 남은 일 / 확인 필요
**결정 필요**
- 1장 마차 판매 조건: 서버 코드는 "마차 2대 모두 1장"일 때만 허용, README 문구대로면 한쪽이 비고 한쪽이 1장일 때도 가능해야 함 → 팀원 규칙 코드라 그대로 둠. README대로 바꿀지 사용자에게 확인.
- 상대 턴 문구("다른 플레이어의 턴 · 시야 회전 / 이모트(휠 클릭)만 가능")는 BP 그래프 글자 → 짧게 바꿀지.
- 메인메뉴에 게임 제목 간판 넣을지.

**실기 확인 안 한 것**
- 상점: 마우스 조작(탭·칸·버튼 클릭, 호버, 확인 상자), 게임 종료/재시작 시 상점 닫힘, 채팅 중 F 무시, 경매 중 상점 열고 닫은 뒤 경매 입력 복귀.
- 경매: 가져온 카드 배치가 덱 위 카드 소진 후에만 되는지(2인 이상), 호스트 연속 배치.
- UI: 결과 화면, 판매 토스트, 경매 경고 문구, 채팅 입력창, 방 목록 한 줄(`WBP_RoomEntry`)을 화면으로 확인.
- 클라 뷰포트에 대화창·상점 버튼이 2벌 겹쳐 생성되는 현상(`OreDealInteractionWidget` 2개) — 원인 미확인.

**나중에**
- 나무(Wood) 획득 규칙, 세 번째 마차 구매, 덱 소진 종료, 동점 처리, 플레이어 이름 표시, F9 디버그 키 제거.
- 상점 세 번째 탭 용도(`UOreDealStoreWidget`의 `Tab == 2`).
- 덱 Y축 방향 자리에서 덱 위 카드 3장 이상일 때 비스듬히 보일 수 있음.
- 팀원용 작업노트(`Docs/팀원용_작업노트.md`, 10/16 기준) 갱신: 조작키 표(M → F, 상점), 용어 표, 깨진 표 정리. 팀원에게 컨트롤러·InteractionWidget·PreGame·TurnBridge 수정 사실 공유.
- 커밋/푸쉬는 사용자가 직접(요청 시 단계별 안내). 새 폴더: `Content/MaterialTraders/UI/Dialogue/`, `Content/MaterialTraders/UI/Fonts/`, `SourceArt/UI/Dialogue/`, `SourceArt/Fonts/Mona/`, `SourceArt/UI/Store/`, `Content/MaterialTraders/UI/Store/`, 새 cpp/h `OreDealStore*`, 삭제된 `__ExternalActors__`(상인 8개).

## 13. 변경 이력 (요약, 자세한 내용은 `AI_작업노트_이전기록.md`)
- 10/13~10/16: 블루프린트로 기본 게임(메인메뉴·방, 덱·마차·정산, 턴, 드로우 애니, 이모트, 결과·재시작), 캐릭터 머리 분리, 손·덱 크기 조정.
- 10/25: Ore Deal 규칙 기준 확정(README).
- 11/10~11: 작업 폴더 이동, 살롱·조명 적용, 이동 입력 삭제, 카드 랙 삭제, Q 손패 보기(C++).
- 11/14: 경매 거래 시스템(C++), 마차판 이미지, 저사양 최적화, 새 손(Hand2/2b), 새 캐릭터(Char2) 3종.
- 11/15: 상점(F), 브로커 캐시 최적화, Dialogue UI 크림 테마 + Mona 픽셀 폰트 전체 적용(메인메뉴·HUD·결과 포함), 상점 버튼 대기 중 숨김, 키 가이드를 재화 UI 아래로 이동·단계 글자 숨김, unity 충돌 수정(`ControllerResourceNames`).
