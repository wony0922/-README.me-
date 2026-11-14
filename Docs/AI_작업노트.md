# AI 작업노트 — Ore Deal (TeamProject)

> 새 대화에서 Claude에게: "Docs/AI_작업노트.md 읽고 이어서 하자" 라고 하면 됩니다.
> 마지막 갱신: 2026-11-14 (8장 경매 거래 추가)

## 용어 (2026-11-14 사용자 정의, 구현·수정 설명에 이 이름만 사용)
| 용어 | 뜻 | 코드상 저장 위치 |
|---|---|---|
| **손패** | 플레이어가 들고 있는 카드 | 폰 `BP_SeatedPlayer.HandCards` |
| **가져온 카드** | 거래 단계에서 가져와 마차에 반드시 배치해야 하는 카드(가져온 순서대로) | `AOreDealAuctionBroker` 좌석의 `PendingCards` |
| **덱 위 카드** | 덱 위에 펼쳐진 카드 전부(처음 펼친 2장 중 안 가져간 카드 + 턴 플레이어가 손패에서 올린 카드) | `BP_CardDeck.RevealedCards` **배열 하나**(빈 칸 -1은 무시). 턴 플레이어가 낸 손패는 이 배열에 push |
| **거래 카드** | 덱 위 카드 중 턴 플레이어가 이번에 거래하려고 고른 카드(강조 표시) | `AOreDealAuctionBroker.LotIndices` (`RevealedCards`의 인덱스) |
| **제안 카드** | 다른 플레이어가 거래 단계에서 손패에서 골라 낸 카드 | 좌석의 `OfferCards` / `OfferHandIndices` |
| **마차 1 카드 / 마차 2 카드** | 마차에 배치된 카드. 플레이어 기준 왼쪽 = 마차 1, 오른쪽 = 마차 2 | `BP_CardBoard.WagonTypes/WagonCounts` 인덱스 0 / 1 (WagonPivot0 = 왼쪽) |

## 0. 환경
- UE 5.8, 블루프린트 전용 프로젝트. 2~4인 LAN 멀티 보드/카드게임 (Listen Server).
- 작업 폴더(git 클론): `C:\프로젝트\게임가상현실\-README.me-` (TeamProject.uproject)
- Blender 원본: `C:\프로젝트\Blender\TeamProject\GameVR.blend`, 내보내기 폴더 `...\Export\`
- PIE 설정: 클라이언트 2, Play As Listen Server.
- 사용자 선호: "푸쉬할거야" 하면 GitHub Desktop 커밋/푸쉬를 **직접 하지 말고 단계별로 안내**.

## 1. 주의사항 (시행착오에서 배운 것)
- **PIE 실행 중 블루프린트 컴파일 금지** → 에디터 크래시로 미저장 작업 손실된 적 있음. 항상: PIE 정지 → 컴파일 → 저장 → PIE.
- **Git LFS `lockable`** (`.gitattributes`) 때문에 체크아웃된 .uasset/.umap이 읽기 전용 → 저장 실패, 지운 액터가 레벨 재로드 시 부활.
  - 해결: `attrib -r "C:\프로젝트\게임가상현실\-README.me-\Content\*" /s` (팀 합의 시 lockable 제거 고려)
  - 폴더 속성창의 "읽기 전용" 체크가 다시 보이는 건 윈도우 정상 동작.
- 레벨에 **이미 배치된 액터는 BP 템플릿 컴포넌트 변경이 반영 안 됨** → 액터 제거 후 같은 트랜스폼/라벨/SeatIndex/폴더로 재배치.
- **RPC(서버/클라 실행) 설정·커스텀 이벤트 파라미터 타입은 툴로 불가** → 에디터 UI(디테일 패널 "리플리케이트")로만. 사용자가 직접 클릭하는 게 가장 저렴.
- 툴 `set_actor_transform`에 location만 넘기면 **회전이 0으로 리셋됨** → 항상 location/rotation/scale 전부 넘길 것. (의자·Seat·마차판 yaw: _0=180/0, _1=0/180, _2=-90/90, _3=90/-90 — Chair·Seat/CardBoard 순)
- 리플리케이트된 Rotator는 0~360으로 올 수 있음 → `Delta(Rotator)`로 정규화 후 사용.

## 2. MCP(언리얼) 툴 사용 팁 — 한글 에디터
- write_graph_dsl은 실행(exec) 핀을 인식 못함("실행") → 노드는 `create_node`, 연결은 `connect_pins`로 직접.
- 노드 type_id 예시:
  - 연산자: `유틸리티|연산자|추가/빼기/곱하기/같음(==)/작음(<)/크거나같음(>=)`, `수학|부울|ANDBoolean`, `수학|플로트|Clamp(Float)`
  - 변수: `Variables|디폴트|GetX/SetX` (bool은 b 접두사 빠짐), 셀프 `Variables|셀프레퍼런스가져오기`
  - 함수 호출 `함수호출|FuncName`, 다른 클래스 `Class|BPPSTable|AddGold`, `Class|컨트롤러|GetPlayerState`
  - 흐름 `Utilities|FlowControl|Branch`, ForEach 입력핀 'Exec', ForLoop 입력핀 'execute'
  - 기타 `폰|IsLocallyControlled`, `폰|GetControlRotation`(declaring_class=/Script/Engine.Pawn 필요), `수학|로테이터|Delta(Rotator)/BreakRotator/MakeRotator`, `트랜스폼메이션|SetRelativeRotation`, `위젯|SetText(Text)`, `유틸리티|텍스트|ToText(Integer)`, `유저인터페이스|위젯생성`, `사용자인터페이스|뷰포트|AddtoViewport`
- 콘솔: 에디터 Cmd에 `ke * 함수명` (PIE 서버 월드 실행), `getall 클래스 프로퍼티` 로 값 확인 → 스크린샷보다 훨씬 저렴.
- PIE 월드끼리 액터 이름이 다를 수 있음(서버 C_0 ≠ 클라 C_0). HandCount 등으로 대응 확인.

## 3. 블루프린트 현황 (/Game/MaterialTraders/...)
- **BP_GM_Table**: PlayerController=BP_PC_Table, PlayerState=BP_PS_Table. BeginPlay: IsStandalone이면 OpenLevel(Lvl_MainMenu) (10/16, Lvl_Table 열린 채 Play해도 메인메뉴부터). EndGame(bGameOver 가드)→모든 PC ClientShowResult. DebugEndGame(F9, 랜덤 골드). CheckRematch(전원 준비 시 servertravel Lvl_Table).
- **BP_PS_Table**: Gold, Wood(int, Replicated), bReadyRematch. AddGold(Amount).
- **BP_PC_Table**: BeginPlay(로컬이면 GameOnly 입력 + WBP_HUD 생성). 키 A/D=MoveFocus(-1/+1), Space=PressSpace, F9=디버그 종료.
  - Focus 0,1=펼친카드, 2,3=마차. PressSpace: 펼친카드 선택/해제, 마차에서 선택카드 있으면 ServerPlace(true,…) 없으면 손패 맨왼쪽 ServerPlace(false,…).
  - RPC: ClientShowResult(소유클라), ServerReadyRematch/ServerDebugEndGame/ServerPlace(서버, reliable; bool 파라미터 NewParam=FromReveal, NewParam2=Reveal1, NewParam3=Wagon1).
  - DoPlace: 내 보드 찾기(FindMyBoard: 폰과 가장 가까운 보드) → 펼친카드 or 손패[0] → board.AddCard → 덱 RevealIfEmpty.
- **BP_RoundTable**(레벨 RoundTable): TableMesh(bar_table, 원본 정사각형 55.88) 인스턴스 스케일 1.65×2×0.6이라 Y가 길었음 → 10/16 **액터 스케일 Y 0.825**로 원형 맞춤(실제 반경 92.2). 컴포넌트 스케일을 툴로 바꾸면 컨스트럭션 재실행으로 되돌아가서 액터 스케일로 처리. 의자 ±119, 마차판 ±70은 원래 대칭.
- **BP_CardBoard**(마차 2칸): WagonTypes/WagonCounts(RepNotify→UpdateVisual). AddCard: 같은 종류면 +1, 다르면 기존 청산(개수≥MinSellCount(2)면 개수×SellPrice 골드, 아니면 무덤) 후 새 카드 1장. SellPrice [1,2,4,3,6] (Stone,Coal,Gold,Iron,Diamond 순 FloatMats 기준). WagonPivot0/1 = (0, ±8, 14), 카메라 바라보게 회전(FacePivots). 레벨 배치: CardBoard_0~3 (SeatIndex 0~3, 폴더 Table/Boards). 10/16: 부유 카드 그림이 90° 누워 있던 것 수정 → WagonCard/RevealCard/Glow 회전 **(P0,Y-90,R90)** ((0,90,-90)은 거꾸로였음) 스케일 (0.085,0.119,1), Glow (0.1,0.134,1). 덱·마차판 5개 재배치함.
- **BP_CardDeck**: RevealedCards[2](RepNotify), RevealTwo/TakeReveal/RevealIfEmpty, 펼친 카드 2장 덱 위 부유(왼쪽=Reveal0). 덱 본체는 10/16에 원본의 3배(=10/13 1.5배의 2배): DeckBody 위치 z3.9 스케일 0.189×0.264×0.078, TopCard (0.45,-0.3,7.86) 0.189×0.264×0.002, TopCardBack z **8.0**(TopCard 윗면 7.96보다 높아야 보임). 레벨에 1개(CardDeck, 폴더 Table, yaw 18). ※ 10/13 변경은 레벨 인스턴스에 반영 안 돼 있었음.
- **BP_SeatedPlayer**(폰): CharacterMesh(몸통 SM_Char_Body) + 자식 HeadMesh(SM_Char_Head, 목 기준 피벗, 상대위치 z39). 스킨: CharacterMeshes/HeadMeshes 배열[Player1, Player2], OnRep_SkinIndex에서 교체.
  - 머리 회전: Tick에서 로컬이면 LookRot=Delta(ControlRot, ActorRot) → ServerSetLook(서버 RPC, Rotator) → ApplyHead. 원격은 복제된 LookRot로 ApplyHead. 범위 좌우 ±80, 위 30/아래 35.
  - 왼손(CardFan)은 사용자가 왼쪽으로 옮김.
  - 10/16 손·카드 2배: 애니(StartDrawAnim/TickDrawAnim/ApplyEmote)가 CardFan·RightHandRoot·FlyCard에 MakeTransform(스케일 1)을 SetRelativeTransform하므로 **부모는 건드리지 말고 자식만 스케일**. LeftHand 스케일 2 + 위치 ×2(2.808,-3.49,-3.55), CardSlots 2, RightHand 2, FlyCard 아래 새 Scene **FlyCardScale**(스케일 2)에 FlyCardBody/Front/Back 재부모. 덱·펼친 카드·마차 카드는 1배 그대로.
- **UI**: WBP_Result(순위/한 판 더/메인메뉴), WBP_HUD(우상단 나무/골드, Tick에서 RefreshHUD). 아이콘 T_Icon_Wood/Gold (원본 SourceArt/UI).
- 머티리얼: M_CardGlow(흰 테두리), M_CardFloat + MI_CardFloat_* (부유 카드, 언릿).

## 3-1. 덱 드로우 애니메이션 (10/14, BP_SeatedPlayer)
- 흐름: PC 키 **W** → `ServerDrawCard`(커스텀 이벤트, **서버에서 실행·신뢰성으로 에디터에서 직접 설정 필요**) → 폰 `ServerStartDraw`(애니 중 아님 && HandCount<10) → DrawType=랜덤(0~4), DrawSeq+1(RepNotify) → `OnRep_DrawSeq` → `StartDrawAnim`(서버·클라 모두 실행).
- `StartDrawAnim`: HandCount==0 → DrawMode 1(왼손=CardFan 전체가 이동해서 집음), 아니면 DrawMode 2(RightHandRoot가 집어서 왼손 부채 쪽으로). 키프레임을 액터 로컬 Transform 배열(CardKeys/HandKeys)+시간 배열(CardTimes/HandTimes)로 만들고, 손 포즈는 PoseTimes/PoseAnims(SK_Hand_L/R Open·Grip·Relax)로 전환.
  - 착지점: 덱→내 위치 70% 지점(나와 마차판 사이), 테이블 위 0.5cm, 뒷면이 위(pivot pitch 90). 카드 pivot은 카드 아래끝이라 +4.6(X) 오프셋.
  - 시간: 발사 0→0.5s(중간 +22cm 아치), 0.6s 손 펴기, 1.15s 집기, 모드1 1.65s 손패 추가 / 모드2 1.7s 놓기·추가, 2.1s 손 복귀.
- `TickDrawAnim(Dt)`(EventTick 맨 앞에서 호출) → `EvalTrack(Keys,Times,T)`(구간 찾아 FInterpEaseInOut + Lerp(Transform))로 FlyCard·손 이동, 추가 시점에 FlyCard 숨김 + 서버만 HandCards.Add/HandCount 갱신(RepNotify).
- 새 컴포넌트: FlyCard(Scene) + FlyCardBody/Front/Back(Card0/CardFront0/CardBack0 복제, 기본 숨김).
- 테스트: 에디터 Cmd에 `slomo 0.15`, `ke * ServerStartDraw` (서버 월드 폰 전부 드로우). 콘솔 입력은 SlateInspector Type(ref=tb2 콘솔 박스)로 가능.
- 주의: 내 시점에선 착지점이 시야 아래(약 45°)라 내려다봐야 보임. 손패 기본값 HandCards=[0..4]라 모드1은 손패가 0장일 때만.

## 3-2. 게임 시작 흐름 (10/14)
- PIE 설정: 넷 모드 **Standalone**, 플레이어 2 (어느 레벨에서 Play해도 메인메뉴로 감 → 한쪽 "방 만들기", 다른 쪽 "방 참가"→방 선택).
- 대기 상태: BP_CardDeck.**bGameStarted**(Replicated)=false. WBP_HUD 하단 StartPanel — 방장(PC HasAuthority)은 "▶ 게임 시작 [SPACE]", 나머지는 "방장이 게임을 시작하기를 기다리는 중...", 아래 "접속 인원 n / 4 (최소 2명)". 시작되면 숨김 (RefreshHUD에서 매 틱 갱신).
- 손패 기본값 비움(HandCards=[], HandCount=0).
- PC PressSpace 맨 앞: 시작 전이면 방장만 GM.**StartGame** 호출(시작 후엔 기존 Space 로직).
- GM.StartGame: 인원 ≥2 확인(아니면 PrintString) → bGameStarted=true → 모든 BP_SeatedPlayer.PendingDraws=5.
- 폰 **TickDeal**(Tick 맨 앞, 서버만): PendingDraws>0 && 애니 중 아님 → NextDrawCount=PendingDraws, PendingDraws=0, ServerStartDraw → **5장 묶음 한 번에** 날아와 왼손이 집음.
- 여러 장 드로우: ServerStartDraw가 NextDrawCount(기본 1)만큼 DrawTypes(int 배열, Replicated)에 랜덤 타입을 채움 → 추가 시점에 DrawTypes 전부 HandCards에 Add. FlyCard는 장수에 비례해 두꺼워짐(Body X스케일 0.0012×N, 앞/뒷면 ±(0.06N+0.02)).

## 3-3. 턴 / 이모트 (10/14)
- 턴: BP_CardDeck.**TurnPS**(PlayerState, Replicated). StartGame에서 PlayerArray[0](보통 방장)으로 시작. GM.**EndTurn(Caller)**: 현재 턴 PS면 PlayerArray 다음 사람으로.
- PC.**IsMyTurn**() = Deck.TurnPS == 내 PlayerState. 턴 아닐 때 막힘: PressSpace(시작 후), MoveFocus(A/D), W(드로우 테스트). 마우스 시야·이모트는 항상 가능.
- 임시 턴 넘기기: **E** → ServerEndTurn(커스텀 이벤트, **서버에서 실행 설정 필요**) → GM.EndTurn. 실제 룰 생기면 그 시점에 EndTurn 호출로 교체.
- HUD 상단 TxtTurn: "▶ 내 턴 · E: 턴 넘기기" / "다른 플레이어의 턴 · 시야 회전 / 이모트(휠 클릭)만 가능" (시작 후만).
- 이모트 휠: 폰에서 **마우스 가운데 버튼** 누름 → WBP_EmoteWheel(도넛 4분할, 상=가리키기, 우=비하하기, 하=엄지척) 생성, 누르는 동안 IA_MouseLook 값을 시야 대신 WheelUpdate(DX,DY)로 누적(±60, 15 이상이면 선택; IMC_MouseLook Y가 Negate라 **위=DY<0**), 떼면 선택=0이면 EmoteTime=3초.
- 이모트 동기화: 별도 RPC 없이 **LookRot.Roll**에 실어 보냄(EmoteTime>0이면 Roll=90) → ServerSetLook으로 서버 → LookRot 복제. ApplyEmote(Tick, 모든 머신): |Roll|>45면 RightHand에 SK_Hand_RPoint 재생 + RightHandRoot를 시선(LookRot pitch±45/yaw±80) 기준 위치(카메라 앞 38, 오른쪽 24, 아래 8, 회전 +yaw-10/pitch5)로 Lerp, 끝나면 RHHome(BeginPlay에서 저장)으로 복귀. 드로우 애니 중엔 건너뜀.
- 10/16 이모트 확장: 휠 = 도넛 4분할(WBP_EmoteWheel의 Border SlotUp/Right/Down/Left 배경을 T_EmoteSeg_* 링 조각 텍스처(440, SourceArt/UI에서 생성)로, 위에 Image RingLines(테두리선), 글자는 RenderTransform ±150 이동). SetSelected(BrushColor 틴트) 그대로.
  - 휠: 위(0)=가리키기, 오른쪽(1)=**비하하기**(엄지 아래), 아래(2)=**엄지척**. 떼면 InRange(WheelSel,0,2)이면 EmoteTime=3 + **EmoteKind**=WheelSel. Roll = EmoteTime>0 ? (Kind0:90 / Kind1:**160** / Kind2:**-160**) : 0.
  - ApplyEmote: |Roll|>=135 → bThumbShown → SK_Hand_RThumbsUp(기존 에셋, Blender R_ThumbsUp) 재생. 손 오프셋 회전: Roll<0이면 MakeRotator(R**90**,P5,Y-10)=엄지 위, 아니면 (R**-90**)=엄지 아래, 가리키기는 R0. 위치·시야 따라오기는 가리키기와 동일.
- 손가락질 애니: Blender Rig_R 액션 R_Point(R_Fist 복사 + 검지=Finger3 펴기) → Export\SK_Hand_RPoint.fbx → /Game/Asset/Hands/SK_Hand_RPoint.

## 4. 캐릭터 모델 (Blender → UE)
- GameVR.blend 컬렉션 `CharSplit`: SM_Character_Round(정수리 둥글게, 키 0.782→0.767), SM_Char_Body, SM_Char_Head, SM_Char_Head_Blush. 원본 SM_Character는 그대로.
  - 목 z 0.39에서 분리, 잘린 면은 양쪽 모두 둥근 돔 캡(반경×0.6)으로 닫음. 머리 원점 = 목 중심. Subsurf 1 적용해서 내보내기.
  - 홍조(M_Blush): 기본 머리에 납작한 타원 2개(볼, x ±0.112, z 0.525)를 추가해 새로 만든 것.
  - 내보내기: Export\SM_CharBody.fbx / SM_CharHead.fbx / SM_CharHead_Blush.fbx (내보낼 때 오브젝트를 원점으로).
- UE: `/Game/Asset/Player1|Player2/SM_Char_Body_R`, `SM_Char_Head_R` (Player1 머리 = 홍조). BP_SeatedPlayer 배열·컴포넌트 기본값을 _R로 교체함. 이전 SM_Char_Body/Head는 미사용(정리 가능).
- 툴 import_file은 기존 에셋을 덮어쓰지 못함 → 새 이름으로 임포트 후 참조 교체. 슬롯 머티리얼은 import 후 set_material로 지정.
- bpy 주의: mesh.materials.clear()는 폴리곤 material_index를 0으로 리셋함.

## 5. 남은 할 일
- 마우스로 좌우 둘러볼 때 머리 회전 실기 확인 (클라→서버 RPC).
- 덱 드로우: ServerDrawCard RPC 설정 확인, 실제 룰(턴/드로우 시점)에 연결 (지금은 W 테스트키)
- 플레이어 간 카드 거래 → 8장 경매 거래로 구현됨. 나무(Wood) 획득 규칙(현재 항상 0).
- F9 디버그 키 제거, 동점 처리, 플레이어 이름 표시.
- 청산 가격/최소 개수는 임시값 → 팀 룰 확정 시 BP_CardBoard SellPrice/MinSellCount 수정.

## 6. 확정된 규칙 구현 방향 (2026-10-25)
- 기준 규칙은 루트 `README.md`의 Ore Deal 규칙이다. 기존 Furnace/연료 명칭은 Wagon/보급품으로 읽는다.
- 참고작 보난자의 핵심은 고정 순서 대기열과 협상 중심 거래다. Ore Deal은 공개 시장 자원, 자기 대기열 자원, 보급품을 협상에 쓸 수 있게 한다.
- 거래 단계는 활성 플레이어가 여러 상대와 개별 합의를 연속해서 진행할 수 있다. 합의에 따라 맞교환 없이 한쪽이 자원/보급품을 기부하는 것도 허용한다.
- 거래·기부로 받은 자원은 대기열에 넣지 않고 즉시 마차 적재로 처리한다. 시장 잔여 공개 자원은 활성 플레이어가 적재한다.
- 대기열은 배열 순서를 유지한다. 거래로 중간 항목을 제거하는 경우 나머지 항목을 재정렬하지 않고 상대 순서만 유지한다.
- 현재 구현 공백: BP_CardDeck은 공개 카드 2장/턴 소유자를 관리하고, BP_SeatedPlayer는 테스트용 드로우를 제공하지만, 전용 자원 대기열·거래 단계·보급품 정산·턴 흐름 연결은 아직 없다. BP_CardBoard는 마차 2칸, 자원 누적/정산 일부만 담당한다.
- 다음 블루프린트 작업 순서: (1) 서버 권한의 턴 단계 enum과 단계 전환, (2) 플레이어별 순서 고정 자원 대기열 및 맨 앞 처리, (3) 거래 제안/수락/기부 RPC와 다중 거래 단계 상태, (4) 거래 자원 즉시 마차 적재 및 유효성 검사, (5) 보급품 차감·정산·세 번째 마차 구매, (6) 덱 소진 종료와 HUD 반영.
- 주의: 현재 환경에는 Unreal 에디터/Blueprint 그래프 편집 도구가 연결되어 있지 않다. `.uasset` 그래프를 텍스트 패치하는 것은 안전하지 않으므로, 실제 런타임 규칙은 에디터에서 블루프린트를 수정할 수 있을 때 위 순서대로 적용해야 한다. README는 규칙 기준을 갱신했으며 블루프린트 구현 완료를 뜻하지 않는다.

## 7. 2026-11-10~11 작업 기록 (진행 중)
- **작업 폴더 변경**: 지금 프로젝트 경로는 `C:\Project\-README.me-` (예전 `C:\프로젝트\게임가상현실\...` 아님). 프로젝트에 팀원이 작성한 C++ 모듈(`Source/TeamProject`)이 있으므로 Visual Studio 빌드 필요.
- **버그는 팀원 담당**: 전체 검토에서 찾은 C++ 버그(자원 5종/8종 불일치, 덱 소진 시 2단계 정지, 1장 판매 조건 등)는 팀원이 고치기로 함. 손대지 않음.
- **살롱 적용 (완료, 레벨 저장됨)**: Blender `GameVR.blend`의 `MT_Saloon`을 `Export\SM_Saloon_*.fbx`로 내보내 `/Game/Asset/Saloon/SM_Saloon_{Structure,Ground,Exterior,Bar,Interior,Props}` 덮어씀. `Lvl_Table` 폴더 `Saloon`에 6개 배치, 위치 (40,-60,0). 기존 `Floor`는 숨김+충돌 끔.
- **조명 (완료, 저장됨)**: `Saloon/Lights`에 PointLight 11개(샹들리에 900lm 그림자 O, 테이블 보조 150lm, 촛대 9개 225lm, 3300K). PostProcessVolume 노출 보정 -0.5.
- **이동 입력 제거 (완료, 저장됨)**: `IMC_Default`에서 `IA_Move`, `IA_Jump` 매핑 삭제(템플릿 캐릭터에서만 쓰던 것). `IA_Look` 게임패드만 남음.
- **카드 랙 모형 삭제 (완료, 저장됨)**: 레벨의 `OreDeal_CardRack_*` 12개 삭제.
- **손패 보기 Q/Z/X (완료: 빌드 성공, PIE에서 Q 열기·Z/X 이동 확인)**:
  - 새 파일 `Public/OreDealHandViewWidget.h`, `Private/OreDealHandViewWidget.cpp` — 좌측 하단 카드 그림 표시, 현재 카드 1.12배, 앞뒤 카드는 0.86배부터 작아지며 좌우로 절반 이상 겹침, 좌우 3장씩만 표시, 0.25초 보간 이동.
  - `OreDealTablePlayerController` 수정: Q 열기/닫기, Z 이전, X 다음, 장터가 열리면 자동으로 닫힘. 랙 클릭 코드와 랙 탐색(전체 액터 스캔) 제거.
  - 카드 텍스처 순서: Stone, Coal, Gold, Iron, Diamond (`BP_CardBoard.FloatMats`와 같음).
- **다음 할 일**: 2인 실제 게임에서 손패 보기 확인(드로우·거래 후 자동 갱신, 장터 열릴 때 자동 닫힘). 팀원에게 컨트롤러 파일 수정 사실 공유.
- **PIE 테스트 팁**: 메인메뉴 UMG 버튼은 Slate 클릭이 안 먹음 → PIE 월드에서 콘솔 `open /Game/MaterialTraders/Maps/Lvl_Table?listen`로 호스트 진입, `ke * ServerStartDraw` 반복으로 손패 채우기, SlateInspector PressKey로 Q/Z/X.
- **Unreal Python 원격 실행**: Blender MCP의 Python에서 UDP 239.0.0.1:6766 원격 실행 프로토콜로 언리얼 명령 실행 가능(언리얼 MCP 서버가 꺼져 있을 때 대안).

## 8. 2026-11-14 경매 거래 시스템 (C++, 기존 거래창 대체)
### 8-0. 같은 날 레벨 작업
- 마차판 이미지: `Asset/Board_image/상점` 이미지 → `/Game/MaterialTraders/Materials/MI_Board_Image`, BP_CardBoard 판 비율을 이미지 비율에 맞춤. 레벨 CardBoard_0~3 재배치(SeatIndex 0~3, 폴더 Table/Boards), 사용자가 옮긴 판 거리에 맞춰 전부 **±54.816**.
- 언리얼 MCP 자동 시작: 에디터 설정 `ModelContextProtocolSettings.bAutoStartServer = true`.

### 8-1. 규칙 (사용자 확정)
1. 2/4 거래 단계에 들어가면 덱에서 2장을 펼치고, 턴 플레이어가 C로 1장 또는 2장 모두 골라 Space로 가져간다 → **가져온 카드**(덱 순서대로). 나머지는 **덱 위 카드**로 남는다.
   - 2장을 다 가져가도 2번(손패 올리기)은 반드시 거친다. 덱 위 카드가 비었는지는 2번이 끝난 뒤 확인하고, 비어 있으면 바로 가져온 카드 배치 단계로 간다.
2. 턴 플레이어가 손패에서 원하는 카드를 골라 덱 위에 올린다(0장 가능).
3. 턴 플레이어가 덱 위 카드 중 **거래 카드**를 여러 장 고른다(강조 표시).
4. 다른 플레이어는 손패에서 **제안 카드**를 골라 낸다(캐릭터 머리 위에 표시). 아무것도 고르지 않고 Space = 거래 안 함.
5. 3인 이상: 턴 플레이어가 A/D로 상대를 고르고(카메라가 그 사람 쪽으로 부드럽게 회전) Space. 2인: 지목 단계 없이 제안 카드 유무로 바로 진행.
6. 거래 성립 시 상대는 거래 카드 전부, 턴 플레이어는 상대 제안 카드 전부를 받는다 → 둘 다 **가져온 카드**로. 아무도 제안 안 하면 거래 카드는 턴 플레이어의 가져온 카드로 들어간다.
7. 덱 위 카드가 없어질 때까지 3~6 반복.
8. 모두 가져온 카드를 마차에 배치해야 한다(A/D 마차 선택, Space 배치 — 손패 배치와 같은 키, 이 동안 손패 배치는 막힘). 전원 완료 전 E → "(이름)의 자원 배치가 끝나지 않았습니다". 완료되면 3장 드로우 단계로 자동 진행.

### 8-2. 조작 통일 규칙
- 인게임 포커스(마차 1/2, 덱 위 카드, 지목할 상대) = **A/D**. 덱 위 카드는 **각 플레이어 화면 기준 좌→우**(아래 줄부터)로 이동, 시작 커서 = 화면 왼쪽 아래 카드.
- 여러 장 선택 = **C**로 토글 → **Space**로 결정.
- 내 화면에만 뜨는 카드(손패 선택 창) = **Z/X**.
- 1단계(필수 배치) 포커스는 마차만(FocusIndex 2/3). 덱 위 카드 선택은 거래 단계에서 2장 펼친 뒤에만.

### 8-3. 덱 위 카드 배치 형태 (덱 로컬 좌표, `DeckSlotLocal`)
- 1장: 덱 중앙 (0,0,20)
- 2장: Y ±9 (기존 모양)
- 3장 이상: 한 줄 최대 3장, 간격 15(2장보다 조금 좁게), 넘치면 줄이 위로(+14z) 늘어남. 장 수 제한 없음.
- BP 덱의 RevealCard0/1·RevealGlow0/1은 숨기고, `AOreDealAuctionDisplay`(로컬 전용)가 같은 카드 모양(MI_CardFloat_*, M_CardGlow, 회전/스케일 동일)으로 그림. 커서 카드는 3cm 위로·1.12배.

### 8-4. 파일 (`Source/TeamProject`)
- 새 파일
  - `OreDealAuctionBroker.h/.cpp` — 서버 권한 경매 상태(레벨에 자동 스폰, Replicated, AlwaysRelevant). Stage: Idle → PickReveal → ActiveOffer → SelectLot → Bidding → (ChoosePartner) → Consume. 좌석 `FOreDealAuctionSeat`(Player, OfferHandIndices, OfferCards, bConfirmed, PendingCards). Tick에서 덱 `TurnPS`/`TurnPhaseIndex`를 보고 시작·리셋, Consume에서 전원 배치 끝나면 단계 2→3 진행. 배치는 `BP_CardBoard.AddCard(CardType, Wagon, PS)`를 리플렉션(ProcessEvent)으로 호출 → 손패 배치와 같은 규칙(다른 종류면 기존 청산).
  - `OreDealAuctionComponent.h/.cpp` — PC에 붙는 컴포넌트. 서버 RPC(PickReveal/SubmitOffer/SelectLot/FocusPartner/ChoosePartner/PlacePending), 키 처리(`HandleInputKey`), 지목 카메라, HUD·표시 갱신.
  - `OreDealAuctionHudWidget.h/.cpp` — 상단 안내 문구, 경고 문구, 하단 손패 선택 창(선택 카드 위로 + "✔ 선택"), 우하단 가져온 카드 + 마차 선택 표시.
  - `OreDealAuctionDisplay.h/.cpp` — 덱 위 카드, 제안 카드(머리 위, 한 줄 5장), 가져온 카드(마차판 옆)를 카메라를 보는 평면으로 그림(풀링).
- 수정: `OreDealTablePlayerController`(경매 컴포넌트 생성, `InputKey`에서 경매 키 우선 처리, A/D는 마차만, 지목 카메라 보간, 브로커 스폰, 경매 중 단계 2 진행 차단, 손패 선택 창 열리면 Q 손패 보기 닫음), `OreDealTurnBridge`(단계 문구: "2/4 경매 거래", "3/4 가져온 자원 배치 (전원 완료 시 자동 진행)").
- 이전 MarketBroker(거래창)는 더 이상 스폰하지 않음.

### 8-5. 구현 메모
- 리슨 서버 호스트는 서버 RPC가 **즉시 실행**됨 → 응답 대기 플래그(`BeginAwait`, Revision 기록)는 RPC **호출 전**에 세팅해야 함. 거절된 요청 대비 1.5초 타임아웃(`AwaitStartTime`).
- 손패에서 카드를 뺄 때 순서 유지, HandCount 갱신 후 OnRep_HandCards/OnRep_HandCount 호출.
- 빌드: cpp만 바뀌면 콘솔 `LiveCoding.Compile`(약 40초). 헤더에 멤버 추가 등은 전체 빌드 필요 → `Binaries/Win64/UnrealEditor.modules`의 BuildId를 아무 값으로 바꾸고 에디터 재시작 → "Missing TeamProject Modules" 창에서 **Yes**.
- PIE 2인 입력 팁: 멀티 창에서는 마우스 캡처한 창이 키를 가져감(Shift+F1로 풀기). 클라 창은 SlateInspector `Click`(조준점 "•") 후 `PressKey`가 확실함. 상태 확인 `getall OreDealAuctionBroker Stage`, `getall OreDealAuctionBroker Seats`.

### 8-6. 남은 것
- 전체 빌드 후 호스트 가져온 카드 연속 배치 재확인.
- 덱 Y축 방향 자리에서는 3장 이상일 때 카드가 옆으로 비스듬히 보일 수 있음 → 필요하면 보는 사람 쪽으로 줄을 세우는 방식 검토.
- 팀원용 작업노트에 용어 표 추가할지 결정.

## 9. 2026-11-14 저사양 최적화 / 새 손(Hand2)
- 이유: 저사양 노트북에서 2인 이상 테스트 시 렉·강제 종료(건물 없는 버전은 정상).
- 렌더링(프로젝트 설정): Lumen GI → 없음, 반사 Lumen → 화면 공간, 가상 섀도 맵 → 일반 섀도 맵, 레이트레이싱 끔, 메시 거리 필드 생성 끔. (월드 파티션 레벨이라 Lightmass 베이크 불가 → 조명 11개는 실시간 유지)
- 모델 LOD: 캐릭터 `SM_Char_Body_R`/`SM_Char_Head_R`(P1·P2)에 25%/10% LOD(화면 크기 0.6/0.25), 살롱 Props/Bar/Structure/Interior/Exterior에 50%/25% LOD(0.3/0.12). `chair1`, `bar_table` Nanite 끔(저폴리라 불필요).
- 새 손: GameVR.blend의 `Hand_L_v2`/`Hand_R_v2`(리그 `Rig_*_v2`)를 데시메이트 0.35(약 7.6k → 3k 면) 후 `Export\SK_Hand2_L/R.fbx`로 내보냄(리그 원점, 기본 FBX 설정, 리프 본 없음, NLA 스트립 = 애니 이름).
  - UE: `/Game/Asset/Hand2/SK_Hand2_L/R` + 전용 스켈레톤 + 애니 `SK_Hand2_*_Anim_Open/Grip/Relax/Fist/ThumbsUp`(+R `Point`). 머티리얼 `Hands/M_Hand`.
  - `BP_SeatedPlayer`: LeftHand/RightHand 메시와 기본 애니, StartDrawAnim·ApplyEmote의 애니 핀을 전부 Hand2로 교체. 기존 `/Game/Asset/Hands`는 남겨둠(미사용).
  - R_Point 액션은 blend에 없어서 `Export\SK_Hand_RPoint.fbx`에서 가져옴(blend는 저장 안 함).

## 10. 2026-11-14 새 캐릭터(Char2) 3종
- Blender `GameVR.blend` 컬렉션 `MT_Character2`: `C2_Base_*`(주황 머리·파란 몸통), `C2_Cowboy_*`(모자·반다나·조끼), `C2_Ranger_*`(모자·재킷·셔츠). 머리는 매끈한 구 + 얼굴 텍스처(`T_Char2_Face_Orange/Peach`, 눈·입은 텍스처로만), 머리 원점 = 목(z 0.39), 정면 = -Y. 모자는 머리 자식, 옷은 몸통 자식. 리깅 없음(머리는 BP가 메시째 회전).
- 내보내기: 머리+모자, 몸통+옷을 각각 합쳐 `Export\SM_Char2_Head_*/Body_*.fbx`.
- UE: `/Game/Asset/Char2/SM_Char2_{Head,Body}_{Base,Cowboy,Ranger}` + 머티리얼 `M2_*`. `BP_SeatedPlayer`의 `CharacterMeshes`/`HeadMeshes` = [Cowboy, Ranger, Base], 컴포넌트 기본 메시 = Cowboy. 기존 `Player1/2`의 `SM_Char_*_R`은 남겨둠(미사용).
- 크기(UE cm): 새 머리 폭 39·목 위 높이 37.8(기존 41·38.2), 새 몸통 폭 31.7·높이 43.6(기존 37.8·48). 머리 위치(z 39)와 컴포넌트 스케일은 그대로.
- 손 수정(같은 날): 가리키기에서 손가락이 안 접히던 원인 = `R_Point` 액션이 쿼터니언 키인데 손 리그 본은 XYZ 오일러 → `R_Fist` 복사 후 Finger3(검지)만 0으로 다시 만듦. 엄지척 손날 튀어나옴 = 손가락 본 웨이트가 손바닥 옆면까지 퍼져 있던 것 → `Hand_*_v2` 웨이트를 관절 위쪽으로 제한 + 스무딩(원본은 메시 `Hand_*_v2_weights_backup`). 다시 내보낸 `SK_Hand2b_L/R` + 애니 `SK_Hand2b_*_Anim_*`를 `BP_SeatedPlayer`에 연결. 이전 `SK_Hand2_*`는 미사용으로 남음.
- **주의 재확인**: PIE 실행 중 BP 수정·컴파일로 에디터 크래시 발생 → 툴 작업 전 항상 `IsPIERunning` 확인.
