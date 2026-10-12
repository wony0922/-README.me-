# AI 작업노트 — Ore Deal (TeamProject)

> 새 대화에서 Claude에게: "Docs/AI_작업노트.md 읽고 이어서 하자" 라고 하면 됩니다.
> 마지막 갱신: 2026-10-12

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
- **BP_CardBoard**(마차 2칸): WagonTypes/WagonCounts(RepNotify→UpdateVisual). AddCard: 같은 종류면 +1, 다르면 기존 청산(개수≥MinSellCount(2)면 개수×SellPrice 골드, 아니면 무덤) 후 새 카드 1장. SellPrice [1,2,4,3,6] (Stone,Coal,Gold,Iron,Diamond 순 FloatMats 기준). WagonPivot0/1 = (0, ±9, 14), 카메라 바라보게 회전(FacePivots). 레벨 배치: CardBoard_0~3 (SeatIndex 0~3, 폴더 Table/Boards).
- **BP_CardDeck**: RevealedCards[2](RepNotify), RevealTwo/TakeReveal/RevealIfEmpty, 펼친 카드 2장 덱 위 부유(왼쪽=Reveal0). 레벨에 1개(CardDeck).
- **BP_SeatedPlayer**(폰): CharacterMesh(몸통 SM_Char_Body) + 자식 HeadMesh(SM_Char_Head, 목 기준 피벗, 상대위치 z39). 스킨: CharacterMeshes/HeadMeshes 배열[Player1, Player2], OnRep_SkinIndex에서 교체.
  - 머리 회전: Tick에서 로컬이면 LookRot=Delta(ControlRot, ActorRot) → ServerSetLook(서버 RPC, Rotator) → ApplyHead. 원격은 복제된 LookRot로 ApplyHead. 범위 좌우 ±80, 위 30/아래 35.
  - 왼손(CardFan)은 사용자가 왼쪽으로 옮김.
- **UI**: WBP_Result(순위/한 판 더/메인메뉴), WBP_HUD(우상단 나무/골드, Tick에서 RefreshHUD). 아이콘 T_Icon_Wood/Gold (원본 SourceArt/UI).
- 머티리얼: M_CardGlow(흰 테두리), M_CardFloat + MI_CardFloat_* (부유 카드, 언릿).

## 4. 캐릭터 모델 분리 (Blender)
- SM_Character를 목(z 0.39m)에서 분리. 몸통 위쪽은 머리 안으로 들어가는 둥근 목(~0.53m), 노멀은 원본에서 전사(Data Transfer). 머리 원점 = 목 중심.
- 결과: `/Game/Asset/Player1|Player2/SM_Char_Body`, `SM_Char_Head`. 원본 .blend는 수정 안 함.
- 미해결: Player1 볼터치(M_Blush) 버전 모델이 .blend에 없어 머리에 볼터치 빠짐.

## 5. 남은 할 일
- 마우스로 좌우 둘러볼 때 머리 회전 실기 확인 (클라→서버 RPC).
- 플레이어 간 카드 거래(룰 확정 후), 나무(Wood) 획득 규칙(현재 항상 0).
- F9 디버그 키 제거, 동점 처리, 플레이어 이름 표시.
- 청산 가격/최소 개수는 임시값 → 팀 룰 확정 시 BP_CardBoard SellPrice/MinSellCount 수정.
