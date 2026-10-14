# AI 작업노트 — Ore Deal (TeamProject)

> 새 대화에서 Claude에게: "Docs/AI_작업노트.md 읽고 이어서 하자" 라고 하면 됩니다.
> 마지막 갱신: 2026-10-14

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
- **BP_GM_Table**: PlayerController=BP_PC_Table, PlayerState=BP_PS_Table. EndGame(bGameOver 가드)→모든 PC ClientShowResult. DebugEndGame(F9, 랜덤 골드). CheckRematch(전원 준비 시 servertravel Lvl_Table).
- **BP_PS_Table**: Gold, Wood(int, Replicated), bReadyRematch. AddGold(Amount).
- **BP_PC_Table**: BeginPlay(로컬이면 GameOnly 입력 + WBP_HUD 생성). 키 A/D=MoveFocus(-1/+1), Space=PressSpace, F9=디버그 종료.
  - Focus 0,1=펼친카드, 2,3=마차. PressSpace: 펼친카드 선택/해제, 마차에서 선택카드 있으면 ServerPlace(true,…) 없으면 손패 맨왼쪽 ServerPlace(false,…).
  - RPC: ClientShowResult(소유클라), ServerReadyRematch/ServerDebugEndGame/ServerPlace(서버, reliable; bool 파라미터 NewParam=FromReveal, NewParam2=Reveal1, NewParam3=Wagon1).
  - DoPlace: 내 보드 찾기(FindMyBoard: 폰과 가장 가까운 보드) → 펼친카드 or 손패[0] → board.AddCard → 덱 RevealIfEmpty.
- **BP_CardBoard**(마차 2칸): WagonTypes/WagonCounts(RepNotify→UpdateVisual). AddCard: 같은 종류면 +1, 다르면 기존 청산(개수≥MinSellCount(2)면 개수×SellPrice 골드, 아니면 무덤) 후 새 카드 1장. SellPrice [1,2,4,3,6] (Stone,Coal,Gold,Iron,Diamond 순 FloatMats 기준). WagonPivot0/1 = (0, ±8, 14), 카메라 바라보게 회전(FacePivots). 레벨 배치: CardBoard_0~3 (SeatIndex 0~3, 폴더 Table/Boards).
- **BP_CardDeck**: RevealedCards[2](RepNotify), RevealTwo/TakeReveal/RevealIfEmpty, 펼친 카드 2장 덱 위 부유(왼쪽=Reveal0). 덱 본체(DeckBody/TopCard/TopCardBack)는 10/13에 1.5배 확대(DeckBody 스케일 0.0945×0.132×0.039). 레벨에 1개(CardDeck).
- **BP_SeatedPlayer**(폰): CharacterMesh(몸통 SM_Char_Body) + 자식 HeadMesh(SM_Char_Head, 목 기준 피벗, 상대위치 z39). 스킨: CharacterMeshes/HeadMeshes 배열[Player1, Player2], OnRep_SkinIndex에서 교체.
  - 머리 회전: Tick에서 로컬이면 LookRot=Delta(ControlRot, ActorRot) → ServerSetLook(서버 RPC, Rotator) → ApplyHead. 원격은 복제된 LookRot로 ApplyHead. 범위 좌우 ±80, 위 30/아래 35.
  - 왼손(CardFan)은 사용자가 왼쪽으로 옮김.
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
- PIE 설정: 넷 모드 **Standalone**, 플레이어 2 (Lvl_MainMenu 열고 Play → 한쪽 "방 만들기", 다른 쪽 "방 참가"→방 선택).
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
- 이모트 휠: 폰에서 **마우스 가운데 버튼** 누름 → WBP_EmoteWheel(상/우/하/좌 4칸, 상=가리키기) 생성, 누르는 동안 IA_MouseLook 값을 시야 대신 WheelUpdate(DX,DY)로 누적(±60, 15 이상이면 선택; IMC_MouseLook Y가 Negate라 **위=DY<0**), 떼면 선택=0이면 EmoteTime=3초.
- 이모트 동기화: 별도 RPC 없이 **LookRot.Roll**에 실어 보냄(EmoteTime>0이면 Roll=90) → ServerSetLook으로 서버 → LookRot 복제. ApplyEmote(Tick, 모든 머신): |Roll|>45면 RightHand에 SK_Hand_RPoint 재생 + RightHandRoot를 시선(LookRot pitch±45/yaw±80) 기준 위치(카메라 앞 38, 오른쪽 24, 아래 8, 회전 +yaw-10/pitch5)로 Lerp, 끝나면 RHHome(BeginPlay에서 저장)으로 복귀. 드로우 애니 중엔 건너뜀.
- 손가락질 애니: Blender Rig_R 액션 R_Point(R_Fist 복사 + 검지=Finger3 펴기) → Export\SK_Hand_RPoint.fbx → /Game/Asset/Hands/SK_Hand_RPoint.

## 4. 캐릭터 모델 (Blender → UE)
- **언리얼은 반드시 git 클론의 TeamProject.uproject로 열 것** (10/13 OneDrive 옛 사본을 열어서 작업이 사라진 것처럼 보였음).
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
- 플레이어 간 카드 거래(룰 확정 후), 나무(Wood) 획득 규칙(현재 항상 0).
- F9 디버그 키 제거, 동점 처리, 플레이어 이름 표시.
- 청산 가격/최소 개수는 임시값 → 팀 룰 확정 시 BP_CardBoard SellPrice/MinSellCount 수정.
