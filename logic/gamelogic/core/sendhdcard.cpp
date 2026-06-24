#include "common/macros.h"
#include "common/nndef.h"
#include "common/nnlogic.h"
#include "gameroot.h"
#include "logic/gamelogic/core/sendhdcard.h"
#include "utils/tarslog.h"
#include "context/context.h"
#include "message/sendclientmessage.h"
#include "config/gameconfig.h"
#include "logic/gamelogic/core/tokento.h"
#include "logic/gamelogic/core/begintimer.h"
#include "logic/gamelogic/core/endtimer.h"
#include "process/process.h"
#include "message/sendroommessage.h"
#include "third.pb.h"
#include "XGameComm.pb.h"
#include "suoha.pb.h"

using namespace nndef;

namespace game
{
    namespace logic
    {
        namespace gamelogic
        {
            void SendHdCard(GameRoot *root)
            {
                PERFSTATS_ENTRY();
                __TRY__

                DLOG_TRACE("roomid:" << root->roomid() << ", " << "SendHdCard roomid:" << root->roomid());

                using namespace context;
                using namespace message;
                using namespace nnlogic;
                using namespace config;
                using namespace process;
                using namespace RoomSo;

                if(root->pro->getProcess() < NN_STATE_FIRST_CARD || root->pro->getProcess() > NN_STATE_FOUR_CARD)
                {
                    DLOG_TRACE("roomid:" << root->roomid() << ", " << "SendHdCard process err. process:" << root->pro->getProcess());
                    return ;
                }

                vecc_t &vecWallCards = root->con->refVecWallCard();

                XGameSHProto::SH_msg2cSendCard shcm;
                shcm.set_igameround(root->pro->getProcess());
                shcm.set_lpoolscore(root->con->getTotalPoolNum());

                std::vector<uid_t> playing_uids;
                
                SetWinnerCard(root);
                std::map<cid_t, User> &usermap = root->con->refUserMap();
                for (auto it = usermap.begin(); it != usermap.end(); it++)
                {
                    if(it->second.isMidSit())
                    {
                        continue;
                    }

                    vecc_t debugCards;
                    root->con->getDebugCardsByCid(it->first, debugCards);
                    vecc_t &vecCards = it->second.refVecCards();
                    auto specCards = it->second.getVecSpecCards();

                    DLOG_TRACE("roomid:" << root->roomid() << ", cid :" << it->first<<", debugCards: "<< debugCards.size()<<", specCards: "<< specCards.size()<< ", process: "<< root->pro->getProcess());

                    for(auto card : specCards)
                    {
                        DLOG_TRACE("roomid:" << root->roomid() << ", spec card :" << card);
                    }
                    if(root->pro->getProcess() == NN_STATE_FIRST_CARD)
                    {
                        vecCards.clear();
                        if(debugCards.size() >= 2)
                        {
                            vecCards.insert(vecCards.begin(), debugCards.begin(), debugCards.begin() + 2);
                            nnlogic::vecremove(vecWallCards, vecCards);
                        }
                        else
                        {
                            if(specCards.size() >= 2)
                            {
                                vecCards.insert(vecCards.begin(), specCards.begin(), specCards.begin() + 2);
                                nnlogic::vecremove(vecWallCards, vecCards);
                            }
                            else
                            {
                                nnlogic::deal(vecWallCards, vecCards, 2);
                            } 
                        }
                    }
                    else
                    {
                        if(debugCards.size() >= root->pro->getProcess())
                        {
                            vecCards.insert(vecCards.end(), debugCards.begin() + root->pro->getProcess() -1, debugCards.begin() + root->pro->getProcess());
                            nnlogic::vecremove(vecWallCards, vecCards);
                        }
                        else
                        {
                            if(specCards.size() >= root->pro->getProcess())
                            {
                                vecCards.insert(vecCards.end(), specCards.begin() + root->pro->getProcess() -1, specCards.begin() + root->pro->getProcess());
                                nnlogic::vecremove(vecWallCards, vecCards);
                            }
                            else
                            {
                                nnlogic::deal(vecWallCards, vecCards, 1);
                            }
                        } 
                    }

                    for(auto card : vecCards)
                    {
                        DLOG_TRACE("roomid:" << root->roomid() << ", hd card :" << card);
                    }

                    it->second.setDone(false);
                    it->second.setOption((E_NN_ACT) -1);
                    playing_uids.push_back(it->second.getUid());

                    it->second.setRobotUserParam("isBB", "", false);
                    it->second.setRobotUserParam("isSelfBet", "", false);
                    it->second.setRobotUserParam("isSelfAdd", "", false);

                    //牌力计算
                   /* if(it->second.isRobot())
                    {
                        TRobotDecideReq decide;
                        vecc_t hdCards;
                        vecc_t commCards;
                        if(vecCards.size() <= 2)
                        {
                            hdCards.insert(hdCards.begin(), vecCards.begin(), vecCards.end());
                        }
                        else
                        {
                            hdCards.insert(hdCards.begin(), vecCards.begin(), vecCards.begin() + 2);
                            commCards.insert(commCards.begin(), vecCards.begin() + 2, vecCards.end());
                        }
                        decide.robotHd[it->second.getUid()] = hdCards;
                        decide.comCards = commCards;
                        sendRoomMessage<TRobotDecideReq>(TGAME_RobotDecide_E, decide, root);
                    }  */
                }

                //发包
                map<int, XGameSHProto::SH_Card> mhdcardpack;
                for (auto it = usermap.begin(); it != usermap.end(); it++)
                {
                    if(it->second.isMidSit())
                    {
                        continue;
                    }
                    vecc_t vecCards = it->second.getVecCards();
                    if(vecCards.size() != root->pro->getProcess())
                    {
                        DLOG_TRACE("roomid:" << root->roomid() <<"send card err process: "<< root->pro->getProcess() <<" , cid: "<< it->first <<", card size:"<< vecCards.size());
                        continue;
                    }

                    //self card
                    XGameSHProto::SH_Card sh_card;
                    if(vecCards.size() == 2)
                    {
                        sh_card.set_icardtype(-1);
                        for(auto card : vecCards)
                        {
                            sh_card.add_vcards(card);
                        }
                    }
                    else
                    {
                        sh_card.set_icardtype(-1);
                        sh_card.add_vcards(vecCards.back());
                    }
                    mhdcardpack[it->first] = sh_card;
                }

                for (auto it = usermap.begin(); it != usermap.end(); it++)
                {
                    shcm.clear_mhdcard();
                    for(auto hdcard : mhdcardpack)
                    {
                        (*shcm.mutable_mhdcard())[hdcard.first] = hdcard.second;
                    }
                   
                    if(it->second.isMidSit())
                    {
                        continue;
                    }

                    if(root->pro->getProcess() == 2)
                    {
                        for(auto& item : shcm.mhdcard())
                        {
                            if(item.first != it->first)
                            {
                                const_cast<XGameSHProto::SH_Card &>(item.second).set_vcards(0, nil_card);
                            }
                        }
                    }
                    
                    sendClientMessage<XGameSHProto::SH_msg2cSendCard>(it->second.getUid(), XGameSHProto::SH_msg2cSendCard_E, shcm, root);
                    DLOG_TRACE("roomid:" << root->roomid() <<"send card process: "<< root->pro->getProcess() <<" , uid: "<< it->second.getUid() << ", shcm:"<< logPb(shcm));
                }     
                //按照牌型排序
                if(root->pro->getProcess() == NN_STATE_FOUR_CARD)
                {
                    vecc_t vecHallCards;
                    vecHallCards.insert(vecHallCards.begin(), vecWallCards.begin(), vecWallCards.end());
                    for (auto it = usermap.begin(); it != usermap.end(); it++)
                    {
                        if(it->second.isMidSit() && it->second.getVecCards().size() < 2)
                        {
                            continue;
                        }
                        vecHallCards.push_back(*it->second.getVecCards().begin());
                    }

                    for (auto it = usermap.begin(); it != usermap.end(); it++)
                    {
                        if(it->second.isMidSit())
                        {
                            continue;
                        }
                        vecc_t lCards;
                        vecc_t lDesCards;
                        lCards.insert(lCards.begin(), it->second.getVecCards().begin() + 1, it->second.getVecCards().end());
                        nnlogic::BestCards(lCards, vecHallCards, lDesCards);

                        DLOG_TRACE(" >>>>>>>>>>>>>>>>>>>>>> ");
                        for(auto card: lDesCards)
                        {
                            DLOG_TRACE(" compare lDesCards num: "<<card <<", face: "<< nncard::getNNNum(card)<< ", suit: "<< nncard::getNNType(card));
                        }
                        DLOG_TRACE(" <<<<<<<<<<<<<<<<<<<<<<<<< ");

                        E_NN_TYPE nnType = nnlogic::getnn(lDesCards);
                        it->second.setCardType(nnType);
                        it->second.setBestCards(lDesCards);
                    }
                   
                    std::sort(playing_uids.begin(), playing_uids.end(), [root](long luid, long ruid)->bool{
                        User* luser = root->con->getUserByUid(luid);
                        User* ruser = root->con->getUserByUid(ruid);
                        if(luser->getCardType() != ruser->getCardType())
                        {
                            return luser->getCardType() < ruser->getCardType();
                        }
                        else
                        {
                            return nnlogic::compare(luser->getBestCards(), ruser->getBestCards(), luser->getCardType());
                        }
                    });
                }
                else
                {
                    //
                    std::sort(playing_uids.begin(), playing_uids.end(), [root, vecWallCards](long luid, long ruid)->bool{
                        User* luser = root->con->getUserByUid(luid);
                        User* ruser = root->con->getUserByUid(ruid);
                        if(!luser || !ruser)
                        {
                            DLOG_TRACE("roomid:" << root->roomid() <<"send card process: "<< root->pro->getProcess() <<", send card err.");
                            return !luser;
                        }
                        if((luser->getVecCards().size() >= NN_STATE_FOUR_CARD || luser->getVecCards().size() < NN_STATE_FIRST_CARD) ||
                            (ruser->getVecCards().size() >= NN_STATE_FOUR_CARD || ruser->getVecCards().size() < NN_STATE_FIRST_CARD))
                        {
                            DLOG_TRACE("roomid:" << root->roomid() <<"send card process: "<< root->pro->getProcess() <<", send card err.");
                            return false;
                        }
                        vecc_t lCards;
                        vecc_t rCards;
                        lCards.insert(lCards.begin(), luser->getVecCards().begin() + 1, luser->getVecCards().end());
                        rCards.insert(rCards.begin(), ruser->getVecCards().begin() + 1, ruser->getVecCards().end());
                        return nnlogic::compare(lCards, rCards);
                    });
                }
                
                auto maxCardTypeUid = playing_uids.back();
                User* maxUser = root->con->getUserByUid(maxCardTypeUid);
                if(maxUser)
                {
                    DLOG_TRACE("roomid:" << root->roomid() <<"send card process: "<< root->pro->getProcess() <<" , max card cid: "<< maxUser->getCid());
                    root->con->setTokenCid(maxUser->getCid());
                }

                root->con->setRobotComParam("round", "", root->pro->getProcess() - 1);
                root->con->setRobotComParam("allAddNum", "set", 0);
                root->con->setRobotComParam("isBet", "", false);
                root->con->setRoundMaxBetNum(0);

                int delayTime =  std::round(0.5 * (root->pro->getProcess() == NN_STATE_FIRST_CARD ? 2 : 1) * playing_uids.size() + 1);
                EndTimer(NN_XTIME_GAME_BEGIN, root, false);
                BeginTimer(NN_XTIME_GAME_BEGIN, delayTime, [](TimerParam & param)->int
                {
                    auto body = static_cast<std::tuple<GameRoot *> const *>(param.getBody());
                    auto root = std::get<0>(*body);

                    TokenTo(root);
                    return 0;
                }, root, false);

                __CATCH__
                PERFSTATS_EXIT();
            }

            void SetWinnerCard(GameRoot *root)
            {
                using namespace context;
                using namespace message;
                using namespace nnlogic;
                using namespace config;
                using namespace process;
                using namespace RoomSo;

                XGameSHProto::SH_msg2cProfitStatNotify shcm;
                shcm.set_luid(0);
                if(root->con->getWinnerCid() == nil_cid)
                {
                    shcm.set_lprofit(root->con->getTotalProft());
                    if(root->cfg->getnVersion() == 0 && root->pro->getProcess() == NN_STATE_FIRST_CARD)
                    {
                        sendAllClientMessage<XGameSHProto::SH_msg2cProfitStatNotify>(XGameSHProto::SH_msg2cProfitStatNotify_E, shcm, root);
                        DLOG_TRACE("roomid:" << root->roomid() <<"profit: " << logPb(shcm));
                    }
                    return;
                }

                DLOG_TRACE("roomid:" << root->roomid() <<"set spec card winner cid: " << root->con->getWinnerCid());
                vecc_t &vecWallCards = root->con->refVecWallCard();
                std::vector<vecc_t> vecAllCards;
                for(int i = 0; i< 2; i++)
                {
                    vecc_t vecCards;
                    nnlogic::deal(vecWallCards, vecCards, 5);
                    vecAllCards.push_back(vecCards);
                }
                
                if(vecAllCards.size() != 2)
                {
                    DLOG_TRACE("roomid:" << root->roomid() <<"set spec card err. ");
                    return ;
                }

                //牌型排序
                std::sort(vecAllCards.begin(), vecAllCards.end(), [](vecc_t lcards, vecc_t rcards)->bool{
                    E_NN_TYPE lType = nnlogic::getnn(lcards);
                    E_NN_TYPE rType = nnlogic::getnn(rcards);
                    if( lType != rType)
                    {
                        return lType < rType;
                    }
                    else
                    {
                        return nnlogic::compare(lcards, rcards, lType);
                    }
                });

                
                std::map<cid_t, User> &usermap = root->con->refUserMap();
                for (auto it = usermap.begin(); it != usermap.end(); it++)
                {
                    if(it->second.isMidSit())
                    {
                        continue;
                    }
                    vecc_t &vecCards = it->second.refVecSpecCards();
                    if(it->first == root->con->getWinnerCid())
                    {
                        vecCards = vecAllCards[1];
                        shcm.set_luid(it->second.getUid());
                    }
                    else
                    {
                        vecCards = vecAllCards[0];
                    }
                }
                
                shcm.set_lprofit(root->con->getTotalProft());
                if(root->cfg->getnVersion() == 0)
                {
                    sendAllClientMessage<XGameSHProto::SH_msg2cProfitStatNotify>(XGameSHProto::SH_msg2cProfitStatNotify_E, shcm, root);
                    DLOG_TRACE("roomid:" << root->roomid() <<"profit: " << logPb(shcm));
                }
                
                root->con->setWinnerCid(nil_cid);
            }
        }
    }
}
