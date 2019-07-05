#include "stdafx.h"
#include "GameLogic.h"
#include <boost/python.hpp>
#include <boost/python/module.hpp>
#include <boost/python/def.hpp>
#include <boost/python/list.hpp>
#include <boost/bind.hpp>
#include <boost/shared_ptr.hpp>
#include <boost/enable_shared_from_this.hpp>
#include <boost/asio.hpp>
#include <iostream> 
#include <string>
#include <boost/unordered_map.hpp>
#include <stdio.h>
#include <fstream>
#include <iomanip>
#include <map>
#include <boost/atomic.hpp> 
#include <windows.h>  
#include <memory>
#include <vector>
using namespace boost::python;

class CardAuto
{
private:
	CGameLogic m_AI;


private:

	void game_cards_2_ai_cards(std::vector<BYTE>& src) {
		for (int i = 0; i < src.size(); i++)
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

	void ai_cards_2_game_cards(std::vector<BYTE>& src) {
		for (int i = 0; i < src.size(); i++)
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

public:
	CardAuto() {
	
	}

	void set_usr_card(BYTE usr_idx, list cards) {
		std::vector<BYTE> v_cards;
		for (int i = 0; i < len(cards); i++) v_cards.push_back(boost::python::extract<BYTE>(cards[i]));
		game_cards_2_ai_cards(v_cards);
		m_AI.SetUserCard(usr_idx, &v_cards[0], v_cards.size());
	}

	void set_banker(BYTE usr_idx, list banker_cards) {
		std::vector<BYTE> v_cards;
		for (int i = 0; i < len(banker_cards); i++) v_cards.push_back(boost::python::extract<BYTE>(banker_cards[i]));
		game_cards_2_ai_cards(v_cards);
		m_AI.SetBanker(usr_idx);
		m_AI.SetBackCard(usr_idx,&v_cards[0],v_cards.size());
	}

	list search_out_card(BYTE usr_idx, BYTE desk_idx, list desk_card) {
		tagOutCardResult result;
		std::vector<BYTE> v_desk_cards;
		for (int i = 0; i < len(desk_card); i++) v_desk_cards.push_back(boost::python::extract<BYTE>(desk_card[i]));
		game_cards_2_ai_cards(v_desk_cards);
		if (usr_idx == desk_idx) {
			v_desk_cards.clear();
		}
		std::vector<BYTE> v_usr_cards;
		for (int i = 0; i < m_AI.m_cbUserCardCount[usr_idx]; i++)v_usr_cards.push_back(m_AI.m_cbAllCardData[usr_idx][i]);
		m_AI.SortCardList(&v_usr_cards[0], v_usr_cards.size(), ST_ORDER);
		m_AI.SortCardList(&v_desk_cards[0], v_desk_cards.size(), ST_ORDER);
		m_AI.SearchOutCard(
			&v_usr_cards[0],
			v_usr_cards.size(),
			&v_desk_cards[0],
			v_desk_cards.size(),
			desk_idx,//当前出牌最大
			usr_idx,//我的位置
			result);
		std::vector<BYTE> v_cards;
		for (int i = 0; i < result.cbCardCount; i++)v_cards.push_back(result.cbResultCard[i]);
		ai_cards_2_game_cards(v_cards);

		list r;
		for (int i = 0; i < v_cards.size(); i++)r.append(v_cards[i]);
		return r;
	}

	list suggest_out_card(BYTE usr_idx, BYTE desk_idx, list desk_card) {
		tagOutCardResult result;
		std::vector<BYTE> v_desk_cards;
		for (int i = 0; i < len(desk_card); i++) v_desk_cards.push_back(boost::python::extract<BYTE>(desk_card[i]));
		game_cards_2_ai_cards(v_desk_cards);
		if (usr_idx == desk_idx) {
			v_desk_cards.clear();
		}
		std::vector<BYTE> v_usr_cards;
		for (int i = 0; i < m_AI.m_cbUserCardCount[usr_idx]; i++)v_usr_cards.push_back(m_AI.m_cbAllCardData[usr_idx][i]);
		m_AI.SortCardList(&v_usr_cards[0], v_usr_cards.size(), ST_ORDER);
		m_AI.SortCardList(&v_desk_cards[0], v_desk_cards.size(), ST_ORDER);
		m_AI.SearchOutCard(
			&v_usr_cards[0],
			v_usr_cards.size(),
			&v_desk_cards[0],
			v_desk_cards.size(),
			result);
		std::vector<BYTE> v_cards;
		for (int i = 0; i < result.cbCardCount; i++)v_cards.push_back(result.cbResultCard[i]);
		ai_cards_2_game_cards(v_cards);

		list r;
		for (int i = 0; i < v_cards.size(); i++)r.append(v_cards[i]);
		return r;

	}

	bool cmp_out_card(list out_card, list desk_card) {
		std::vector<BYTE> v_desk_cards;
		for (int i = 0; i < len(desk_card); i++) v_desk_cards.push_back(boost::python::extract<BYTE>(desk_card[i]));
		game_cards_2_ai_cards(v_desk_cards);

		std::vector<BYTE> v_out_cards;
		for (int i = 0; i < len(out_card); i++) v_out_cards.push_back(boost::python::extract<BYTE>(out_card[i]));
		game_cards_2_ai_cards(v_out_cards);

		return m_AI.CompareCard(
			&v_desk_cards[0],
			&v_out_cards[0],
			v_desk_cards.size(),
			v_out_cards.size());
	}

	void out_card(BYTE usr_idx, list cards) {
		std::vector<BYTE> v_out_cards;
		for (int i = 0; i < len(cards); i++) v_out_cards.push_back(boost::python::extract<BYTE>(cards[i]));
		game_cards_2_ai_cards(v_out_cards);
		m_AI.RemoveUserCardData(usr_idx, &v_out_cards[0], v_out_cards.size());
	}

	bool land_score(BYTE usr_idx,int score, list cards) {
		std::vector<BYTE> v_hand_cards;
		for (int i = 0; i < m_AI.m_cbUserCardCount[usr_idx]; i++)v_hand_cards.push_back(m_AI.m_cbAllCardData[usr_idx][i]);

		//banker cards
		std::vector<BYTE> v_banker_cards;
		for (int i = 0; i < len(cards); i++) v_banker_cards.push_back(boost::python::extract<BYTE>(cards[i]));
		game_cards_2_ai_cards(v_banker_cards);
		v_hand_cards.insert(v_hand_cards.end(), v_banker_cards.begin(), v_banker_cards.end());

		m_AI.SetLandScoreCardData(&v_hand_cards[0], v_hand_cards.size());
		return m_AI.LandScore(usr_idx, score) !=255 ;
	}


	int get_card_type(list cards) {
		std::vector<BYTE> v_out_cards;
		for (int i = 0; i < len(cards); i++) v_out_cards.push_back(boost::python::extract<BYTE>(cards[i]));
		game_cards_2_ai_cards(v_out_cards);
		return m_AI.GetCardType(&v_out_cards[0], v_out_cards.size());
	}


};

BOOST_PYTHON_MODULE(hlddz)
{
	class_<CardAuto>("CardAuto", init<>())
		.def("set_usr_card", &CardAuto::set_usr_card)
		.def("set_banker", &CardAuto::set_banker)
		.def("search_out_card", &CardAuto::search_out_card)
		.def("suggest_out_card", &CardAuto::suggest_out_card)
		.def("out_card", &CardAuto::out_card)
		.def("cmp_out_card", &CardAuto::cmp_out_card)
		.def("land_score", &CardAuto::land_score)
		.def("get_card_type", &CardAuto::get_card_type)
		;
};