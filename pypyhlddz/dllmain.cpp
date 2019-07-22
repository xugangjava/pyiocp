// dllmain.cpp : 定义 DLL 应用程序的入口点。
#include "pch.h"
#include "GameLogic.h"

void game_cards_2_ai_cards(BYTE* src,int src_count) {
	for (int i = 0; i < src_count; i++)
	{
		if (src[i] == 53) {
			src[i] = 78;
		}
		else if (src[i] == 54) {
			src[i] = 79;
		}
		else if (src[i] >= 14 && src[i] <= 26) {
			src[i] = src[i] + 3;
		}
		else if (src[i] >= 27 && src[i] <= 39) {
			src[i] = src[i] + 6;
		}
		else if (src[i] >= 40 && src[i] <= 52) {
			src[i] = src[i] + 9;
		}
	}
}

void ai_cards_2_game_cards(BYTE* src, int src_count) {
	for (int i = 0; i < src_count; i++)
	{
		if (src[i] == 78) {
			src[i] = 53;
		}
		else if (src[i] == 79) {
			src[i] = 54;
		}
		else if (src[i] >= 17 && src[i] <= 29) {
			src[i] = src[i] - 3;
		}
		else if (src[i] >= 33 && src[i] <= 45) {
			src[i] = src[i] - 6;
		}
		else if (src[i] >= 49 && src[i] <= 61) {
			src[i] = src[i] - 9;
		}
	}
}
extern "C" _declspec(dllexport)  void hlddz_search_out_card(
	const BYTE* usr_card_1,
	const int  usr_card_count_1, 
	const BYTE* usr_card_2,
	const int  usr_card_count_2, 
	const BYTE* usr_card_3,
	const int  usr_card_count_3,
	const BYTE* desk_card,
	const int  desk_card_count,
	const BYTE usr_idx,
	const BYTE desk_idx,
	const BYTE banker_idx,
	tagOutCardResult* result
	) {

	BYTE all_card_data[4][MAX_COUNT];
	memcpy(all_card_data[0], usr_card_1, usr_card_count_1);
	memcpy(all_card_data[1], usr_card_2, usr_card_count_2);
	memcpy(all_card_data[2], usr_card_3, usr_card_count_3);
	memcpy(all_card_data[3], desk_card, desk_card_count);

	game_cards_2_ai_cards(all_card_data[0], usr_card_count_1);
	game_cards_2_ai_cards(all_card_data[1], usr_card_count_2);
	game_cards_2_ai_cards(all_card_data[2], usr_card_count_3);
	game_cards_2_ai_cards(all_card_data[3], desk_card_count);
	
	
	int dsk_card_count = desk_card_count;
	if (usr_idx == desk_idx) {
		dsk_card_count = 0;
	}

	CGameLogic logic;
	logic.SetBanker(banker_idx);
	logic.SetUserCard(0, all_card_data[0], usr_card_count_1);
	logic.SetUserCard(1, all_card_data[1], usr_card_count_2);
	logic.SetUserCard(2, all_card_data[2], usr_card_count_3);
	int usr_card_count = logic.m_cbUserCardCount[usr_idx];
	logic.SortCardList(all_card_data[usr_idx], usr_card_count, ST_ORDER);
	logic.SortCardList(all_card_data[3], desk_card_count, ST_ORDER);
	tagOutCardResult rs;
	logic.SearchOutCard(
		all_card_data[usr_idx],
		usr_card_count,
		all_card_data[3], 
		desk_card_count,
		desk_idx,//当前出牌最大
		usr_idx,//我的位置
		*result);
	ai_cards_2_game_cards(result->cbResultCard, result->cbCardCount);
}

extern "C" _declspec(dllexport) bool hlddz_cmp_out_card(
	const BYTE* out_card,
	const int  out_card_cout,
	const BYTE* desk_card,
	const int  desk_card_count) {
	
	BYTE all_card_data[2][MAX_COUNT];
	memcpy(all_card_data[0], desk_card, desk_card_count);
	memcpy(all_card_data[1], out_card, out_card_cout);
	game_cards_2_ai_cards(all_card_data[0], desk_card_count);
	game_cards_2_ai_cards(all_card_data[1], out_card_cout);

	CGameLogic logic;
	logic.SortCardList(all_card_data[0], desk_card_count, ST_ORDER);
	logic.SortCardList(all_card_data[1], out_card_cout, ST_ORDER);
	return logic.CompareCard(
		all_card_data[0],
		all_card_data[1],
		desk_card_count,
		out_card_cout);
}

extern "C" _declspec(dllexport) int hlddz_get_card_type(
	const BYTE* card,
	const int  card_count) {
	
	BYTE all_card_data[MAX_COUNT];
	memcpy(all_card_data, card, card_count);
	game_cards_2_ai_cards(all_card_data, card_count);

	CGameLogic logic;
	logic.SortCardList(all_card_data, card_count, ST_ORDER);
	return logic.GetCardType(all_card_data, card_count);
}


extern "C" _declspec(dllexport) void hlddz_search_can_out_card(
	const BYTE* usr_card,
	const int  usr_card_count,
	const BYTE* desk_card,
	const int  desk_card_count,
	tagOutCardResult* result
) {
	BYTE all_card_data[2][MAX_COUNT];
	memcpy(all_card_data[0], usr_card, usr_card_count);
	memcpy(all_card_data[1], desk_card, desk_card_count);

	game_cards_2_ai_cards(all_card_data[0], usr_card_count);
	game_cards_2_ai_cards(all_card_data[1], desk_card_count);

	CGameLogic logic;
	logic.SortCardList(all_card_data[0], usr_card_count, ST_ORDER);
	logic.SortCardList(all_card_data[1], desk_card_count, ST_ORDER);
	logic.SearchOutCard(
		all_card_data[0],
		usr_card_count,
		all_card_data[1],
		desk_card_count,
		*result);
	ai_cards_2_game_cards(result->cbResultCard, result->cbCardCount);

}

////////////////////////////////////////////////////////////////////////////////


extern "C" _declspec(dllexport)  void tdlz_search_out_card(
	const BYTE* usr_card_1,
	const int  usr_card_count_1,
	const BYTE* usr_card_2,
	const int  usr_card_count_2,
	const BYTE* usr_card_3,
	const int  usr_card_count_3,
	const BYTE* desk_card,
	const int  desk_card_count,
	const BYTE usr_idx,
	const BYTE desk_idx,
	const BYTE banker_idx,
	const BYTE tlz,		//天癞子
	const BYTE dlz,		//地癞子
	tagOutCardResult* result
) {
	BYTE all_card_data[4][MAX_COUNT];
	memcpy(all_card_data[0], usr_card_1, usr_card_count_1);
	memcpy(all_card_data[1], usr_card_2, usr_card_count_2);
	memcpy(all_card_data[2], usr_card_3, usr_card_count_3);
	memcpy(all_card_data[3], desk_card, desk_card_count);
	game_cards_2_ai_cards(all_card_data[0], usr_card_count_1);
	game_cards_2_ai_cards(all_card_data[1], usr_card_count_2);
	game_cards_2_ai_cards(all_card_data[2], usr_card_count_3);
	game_cards_2_ai_cards(all_card_data[3], desk_card_count);
	int dsk_card_count = desk_card_count;
	if (usr_idx == desk_idx) {
		dsk_card_count = 0;
	}
	CGameLogic logic;
	logic.SetBanker(banker_idx);
	logic.SetUserCard(0, all_card_data[0], usr_card_count_1);
	logic.SetUserCard(1, all_card_data[1], usr_card_count_2);
	logic.SetUserCard(2, all_card_data[2], usr_card_count_3);
	int usr_card_count = logic.m_cbUserCardCount[usr_idx];
	logic.SortCardList(all_card_data[3], desk_card_count, ST_ORDER);
	BYTE lz_card[MAX_COUNT] ;
	BYTE lz_card_maping[MAX_COUNT] ;
	ZeroMemory(lz_card, MAX_COUNT);
	ZeroMemory(lz_card_maping, MAX_COUNT);

	for (int i = 0; i < usr_card_count; i++)
	{
		int c = all_card_data[usr_idx][i];
		int lv = logic.GetCardValue(c);
		if (lv == tlz || lv == dlz) {
			lz_card[i] = c;
			lz_card_maping[i] = lv;
		}
	}

	logic.SortCardList(all_card_data[usr_idx], usr_card_count, ST_ORDER);
	tagOutCardResult rs;
	logic.SearchOutCard(
		all_card_data[usr_idx],
		usr_card_count,
		all_card_data[3],
		desk_card_count,
		desk_idx,//当前出牌最大
		usr_idx,//我的位置
		rs);
	ai_cards_2_game_cards(rs.cbResultCard, rs.cbCardCount);
	memcpy(result, &rs, sizeof(tagOutCardResult));
}





BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

