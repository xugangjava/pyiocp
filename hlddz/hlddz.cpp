// hlddz.cpp : 定义 DLL 应用程序的导出函数。
//

#include "stdafx.h"


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
#include <boost/python/suite/indexing/vector_indexing_suite.hpp>
#include <queue>
#include "GameLogic.h"
using namespace boost::python;
typedef std::vector<BYTE> card_list;
class ai
{

public:
	ai() {
		for (int i = 0; i < GAME_PLAYER; i++) {
			m_usr_card[i] = card_list();
		}
	}

	~ai() {

	}


	void set_banker(int bDeskStation, card_list banker_card) {
		m_AI.SetBackCard(bDeskStation, &banker_card[0], banker_card.size());
		m_AI.SetBanker(bDeskStation);
		for (int i = 0; i < banker_card.size(); i++) {
			m_usr_card[bDeskStation].push_back(banker_card[i]);
		}
	}

	void set_usr_card(int bDeskStation, card_list cards) {
		m_usr_card[bDeskStation].clear();
		m_usr_card[bDeskStation].assign(cards.begin(), cards.end());
		m_AI.SetUserCard(bDeskStation, &cards[0], cards.size());
	}


	bool out_card(int bDeskStation, card_list cards) {
		//检查是否为我的手牌
		for (int i = 0; i < cards.size(); i++)
		{
			if (std::find(m_usr_card[bDeskStation].begin(), m_usr_card[bDeskStation].end(), cards[i]) == m_usr_card[bDeskStation].end()) {
				return false;
			}
		}
		//牌型扑克
		m_AI.SortCardList(&cards[0], cards.size(), ST_ORDER);
		//分析类型
		BYTE bCardType = m_AI.GetCardType(&cards[0], cards.size());
		//类型判断
		if (bCardType == CT_ERROR) return false;

		bool is_first_out_card = m_desk_card.empty();
		bool is_my_trun = bDeskStation == m_desk_card_station;
		bool is_need_cmp_card = !is_first_out_card && !is_my_trun;
		if (is_need_cmp_card) {
			if (!m_AI.CompareCard(&m_desk_card[0], &cards[0], m_desk_card.size(), cards.size())) {
				return false;
			}
		}
		m_AI.RemoveUserCardData(bDeskStation, &cards[0], cards.size());
		for (int i = 0; i < cards.size(); i++)
		{
			m_usr_card[bDeskStation].erase(std::find(
				m_usr_card[bDeskStation].begin(), 
				m_usr_card[bDeskStation].end(), 
				cards[i]));
		}
		m_desk_card_station = bDeskStation;
		return true;
	}

	card_list search_out_card(int bDeskStation) {
		tagOutCardResult result;
		m_AI.SearchOutCard(
			&m_usr_card[bDeskStation][0],//我的牌
			m_usr_card[bDeskStation].size(),//我的牌数量
			&m_desk_card[0],//当前桌子上的牌
			m_desk_card.size(),//桌子上的牌数量
			m_desk_card_station,//当前出牌最大
			bDeskStation,//我的位置
			result);
		card_list r;
		for (int i = 0; i < result.cbCardCount; i++) {
			r.push_back(result.cbResultCard[i]);
		}
		return r;
	}

	int land_score(int bDeskStation,int cbCurrentLandScore) {
		m_AI.SetLandScoreCardData(&m_usr_card[bDeskStation][0], m_usr_card[bDeskStation].size());
		return m_AI.LandScore(bDeskStation, cbCurrentLandScore);
	}

private:
	CGameLogic m_AI;
	card_list m_desk_card;//桌子上的牌
	int m_desk_card_station;//当前上手玩家
	std::map<int, card_list> m_usr_card;
};



BOOST_PYTHON_MODULE(ai)
{
	class_<card_list>("hlddz_card_list")
		.def(vector_indexing_suite<card_list>());

	class_<ai>("hlddz_ai")
		.def("set_banker", &ai::set_banker)
		.def("land_score", &ai::land_score)
		.def("set_usr_card", &ai::set_usr_card)
		.def("out_card", &ai::out_card)
		.def("search_out_card", &ai::search_out_card)
		;
}