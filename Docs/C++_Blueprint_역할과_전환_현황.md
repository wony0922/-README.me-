# C++ / Blueprint 역할 및 전환 현황

> 기준일: 2026-11-21  
> 프로젝트: Unreal Engine 5.8, `-README.me-`  
> 이 문서는 C++ 파일의 책임, 관련 Blueprint의 역할, 실제 전환 여부를 한곳에서 추적한다. 작업별 상세 이력은 [AI_작업노트.md](./AI_작업노트.md)를 참고한다.

## 상태 기준

- **전환 완료(활성 경로)**: Blueprint 구현이 실제 사용 경로에 연결되어 C++ 구현을 대체한다.
- **대체본 준비**: Blueprint 대체 에셋은 만들었지만 레벨·에셋 참조를 바꾸기 전이다.
- **부분 전환**: 일부 표시·데이터·흐름은 Blueprint가 담당하지만 핵심 책임이 C++에 남아 있다.
- **미전환**: 현재 기능 구현이 C++에 있으며 Blueprint 대체는 아직 적용되지 않았다.
- **C++ 유지**: 서버 권한, RPC, 복제 상태, 모듈 등록 등 Blueprint에 옮기지 않을 책임이다.

## C++ 파일별 역할과 전환 상태

| C++ 파일 | 역할 | Blueprint 관계 및 현재 상태 |
|---|---|---|
| `CardActionPulseComponent.h/.cpp` | 카드 글로우 메시의 크기와 `PulseOpacity` 펄스 처리 | **전환 완료(활성 경로)**: `BP_CardActionPulse`가 구현을 대신하며 `BP_CardBoard`의 네이티브 컴포넌트도 BP 클래스로 교체했다. 기존 소스는 다른 참조 확인 전까지 보존한다. |
| `OreDealAuctionBroker.h/.cpp` | 경매 단계, 제안·거래·가져온 카드 상태의 서버 판정 및 복제 | **C++ 유지**: 서버 권한과 복제 경계다. |
| `OreDealAuctionComponent.h/.cpp` | 경매 RPC, 소유 클라이언트 입력, HUD·카드 표시 연결 | **부분 전환 후보**: RPC는 C++에 두고 로컬 입력·표시 연결을 Blueprint로 분리할 수 있다. |
| `OreDealAuctionDisplay.h/.cpp` | 로컬 관점에 맞춘 덱 위·제안·가져온 카드 메시 표시 | **미전환**: 시각 전용 BP Actor로 옮길 수 있다. |
| `OreDealAuctionHudWidget.h/.cpp` | 경매 안내, 선택, 가져온 카드 HUD 구성 및 갱신 | **미전환**: WBP로 옮길 수 있다. |
| `OreDealHandViewWidget.h/.cpp` | 손패 카드 선택, 부채꼴 레이아웃, 애니메이션 | **미전환**: WBP로 옮길 수 있다. |
| `OreDealInteractionWidget.h/.cpp` | 채팅·상점 버튼·시장 거래 UI 및 로컬 상호작용 | **미전환**: UI는 WBP 후보이며 네트워크 요청 경계는 C++에 둘 수 있다. |
| `OreDealMarketBroker.h/.cpp` | 구형 장터 거래 상태·제안·동의의 서버 복제 | **미전환/구형 경로**: 사용하지 않는다고 기록되어 있다. 참조를 확인한 뒤 유지·정리 여부를 결정한다. |
| `OreDealMerchantStand.h/.cpp` | 상인의 정적 메시·재질·트랜스폼 구성 | **대체본 준비**: `BP_MerchantStand`에 메시·재질·트랜스폼·충돌·복제 설정을 옮겼다. 레벨·에셋 참조를 확인하고 대체 배치하기 전까지 C++를 유지한다. |
| `OreDealPreGameComponent.h/.cpp` | 게임 시작 전 상인·HUD 숨김과 왼손 대기 포즈 | **미전환**: 로컬 프레젠테이션 컴포넌트라 BP 후보이다. |
| `OreDealStoreComponent.h/.cpp` | 상점 입력·상태 전달·구매 서버 RPC | **부분 전환 후보**: 구매 검증과 RPC는 C++에 유지하고 로컬 표시·입력을 BP로 분리할 수 있다. |
| `OreDealStoreWidget.h/.cpp` | 상점 입력·선택 상태 및 화면 구성 | **부분 전환**: 상점 상태·구매/판매 요청은 C++에 유지한다. `WBP_Store`의 간소화된 화면에서 글자가 겹치고 기존 레이아웃과 달라지는 문제를 확인해, 2026-11-22부터 파생 WBP에서도 기존 `Build()` 화면을 구성하도록 복원했다. 이 화면은 이전의 배경·탭·상품 슬롯·가격 태그·마차 카드·판매 버튼·확인창 배치를 사용한다. 빌드 성공; 에디터에서 새 모듈 로드 후 PIE 시각 확인은 남아 있다. |
| `OreDealTablePlayerController.h/.cpp` | 플레이어 입력, 서버 RPC, 위젯·게임플레이 컴포넌트 연결 | **부분 전환**: `BP_PC_Table`은 자식 Blueprint다. 중앙 조준점은 `OreDealInteractionWidget`에서 C++로 그리고, 중앙 좌클릭 라인 트레이스도 C++에서 처리한다. 상인 클릭은 `OreDealMerchant` 태그를 찾는데 `Lvl_Table`의 배치된 `BP_Merchant` 2개에 태그가 없어 동작하지 않았다. 두 인스턴스에 태그를 추가해 기존 라인 트레이스 경로에 연결했다. 태그 저장은 확인했으며, 실제 PIE 클릭 검증은 남아 있다. 네트워크·소유권 경계는 C++에 유지한다. |
| `OreDealTradeManager.h/.cpp` | 복제되는 턴·자원·거래 규칙 상태 | **C++ 유지**: `BP_TradeManager`는 자식 Blueprint이며 네이티브 규칙 로직을 대체하지 않는다. |
| `OreDealTurnBridge.h/.cpp` | 레거시 덱의 턴 단계 확인·진행 및 HUD 문구 제공 | **부분 전환 후보**: 복제 상태 변경은 C++에 유지하고 순수 문구·표시는 BP로 옮길 수 있다. |
| `TeamProject.cpp` | Unreal 게임 모듈 등록 | 변환 대상이 아니다. 게임 모듈 부트스트랩 코드다. |
| `OreDealStoreCatalog.h` | Blueprint에서 편집하는 상점 상품·판매 표시 구조체와 enum | **이미 Blueprint 사용 가능**: `BP_PC_Table.StoreItems` 데이터 타입으로 사용한다. |

## Blueprint 역할과 대응 상태

| Blueprint | 역할 | 전환 상태 및 참고 |
|---|---|---|
| `BP_GM_Table` | 게임 모드 설정, 게임 시작·종료, 재경기 흐름 | 게임 흐름 일부를 Blueprint에서 담당한다. |
| `BP_PS_Table` | 플레이어 Gold·Wood 및 재경기 준비 상태 | 관련 데이터 중 Gold·Wood는 복제 변수로 사용한다. |
| `BP_PC_Table` | 로컬 입력, 포커스·카드 배치 흐름, HUD 생성 | `AOreDealTablePlayerController`의 자식이다. 서버 RPC와 소유권 책임까지 옮긴 것은 아니다. |
| `BP_CardBoard` | 마차 슬롯, 카드 적재·표시 및 판매 정산 일부 | 카드 펄스 컴포넌트는 `BP_CardActionPulse`로 교체됐다. `Lvl_Table`의 보드 4개 인스턴스에 남아 있던 3번 `WagonText2`의 `Text` 자리표시 문구를 빈 문자열로 정리하고 레벨을 저장했다. 이 컴포넌트는 `ApplySlot`에서 참조되므로 삭제하지 않았다. |
| `BP_CardDeck` | 공개 카드 두 장, 턴 소유자, 덱·드로우 흐름 | Blueprint 게임 흐름의 일부를 담당한다. |
| `BP_Merchant` | 레벨에 배치된 상인과 `ShopButtonMash` 메시 | `Lvl_Table`의 두 인스턴스에 `OreDealMerchant` 태그를 지정했다. 중앙 포인터 라인 트레이스가 이 태그를 통해 상점 열기 입력을 찾는다. |
| `BP_SeatedPlayer` | 캐릭터 구성, 손패·드로우 애니메이션 및 이모트 | Blueprint에서 외형·애니메이션 흐름을 담당한다. |
| `BP_RoundTable` | 레벨의 원형 테이블 배치와 스케일 | 레벨 배치용 Blueprint다. |
| `BP_TradeManager` | C++ 거래 매니저의 자식 클래스 | 자식 BP가 존재하지만 복제 거래 규칙은 C++에 남아 있다. |
| `BP_CardActionPulse` | 카드 글로우 펄스 컴포넌트 | **활성 경로에서 C++ 구현 대체 완료**. |
| `BP_MerchantStand` | 상인 정적 구성 요소 | 대체 Blueprint는 준비됐지만 기존 C++ 참조를 확인하고 교체 배치해야 한다. |
| `WBP_HUD` | 골드·나무, 턴, 게임 시작 상태 등 HUD 표시 | 화면 구성과 표시를 담당한다. |
| `WBP_Result` | 순위, 재경기, 메인 메뉴 UI | 결과 화면을 담당한다. |
| `WBP_Store` | 상점 화면 에셋 | Blueprint 위젯 트리와 `BP_RefreshStore`는 남아 있지만, 현재 화면 구성은 부모 C++의 `Build()`를 다시 사용한다. 간소화된 트리에서 발생한 판매 텍스트 겹침을 피하고 이전 화면과 같은 구성을 복구하기 위한 조치다. Blueprint 화면의 동등한 레이아웃 전환은 미완료이며, 에디터 새 모듈 로드 후 PIE에서 확인해야 한다. |
| `WBP_EmoteWheel` | 이모트 선택 휠 | 로컬 휠 표시·선택을 담당한다. |

## 전환 원칙 및 남은 작업

- Blueprint가 C++ 클래스를 상속한다는 이유만으로 부모 클래스의 책임이 전환된 것으로 보지 않는다.
- 우선 UI·표시·로컬 프레젠테이션을 옮기고, 서버 검증·RPC·복제 상태는 경계를 확인한 뒤 C++에 유지한다.
- C++ 소스 삭제 전 Blueprint·레벨 참조를 확인하고 전체 에디터 빌드를 통과시킨다.
- 다음 UI·시각 전환 후보: `OreDealPreGameComponent`, `OreDealAuctionDisplay`, `OreDealAuctionHudWidget`, `OreDealHandViewWidget`, `OreDealInteractionWidget`, `OreDealStoreWidget`.
- `OreDealMarketBroker`와 `OreDealMerchantStand`는 실제 사용 참조 확인 전까지 삭제하거나 대체 완료로 간주하지 않는다.
- 상점의 기존 화면 구성을 복원하기 위해 네이티브 `Build()`를 다시 활성화했다. 현재 활성 화면의 Blueprint 전환은 완료되지 않았다. 새 모듈이 로드된 뒤 PIE 시각 확인과 구매·판매·확인 흐름 검증을 수행한다. 상인 중앙 조준 클릭 검증은 별도로 남아 있다.
