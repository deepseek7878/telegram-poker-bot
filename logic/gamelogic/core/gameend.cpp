#include "Comm/ITableGame.h"
#include "common/macros.h"
#include "common/nnlogic.h"
#include "gameroot.h"
#include "logic/gamelogic/core/gameend.h"
#include "utils/tarslog.h"
#include "context/context.h"
#include "config/gameconfig.h"
#include "suoha.pb.h"
#include "process/process.h"
#include "message/sendclientmessage.h"
#include "message/sendroommessage.h"
#include "logic/gamelogic/core/begintimer.h"
#include "logic/gamelogic/core/endtimer.h"
#include "common/nndef.h"
#include "xtime4lib.h"
#include "logic/gamelogic/core/gamecalculate.h"
#include "logic/gamelogic/core/checkbegin.h"
#include "third.pb.h"
#include <math.h>

using namespace nndef;

namespace game
{
    namespace logic
    {
        namespace gamelogic
        {
            void GameEnd(GameRoot *root)
            {
                PERFSTATS_ENTRY();
                __TRY__
                DLOG_TRACE("roomid:" << root->roomid() << ", " << "GameEnd roomid:" << root->roomid());

                using namespace context;
                using namespace process;
                using namespace config;
                using namespace message;
                using namespace RoomSo;
                using namespace nnlogic;

                //计算奖池
                GameCalculate(root);

                XGameSHProto::SH_msg2sGameEnd shcm;
                std::map<cid_t, User> &usermap = root->con->refUserMap();
                for (auto it = usermap.begin(); it != usermap.end(); it++)
                {
                    TGAME_PlayerScoreChange tmm;
                    tmm.changeType = 2;
                    tmm.changePoint.insert(std::make_pair(it->second.getUid(), it->second.getChangeNum() - it->second.getRoundBetNum(-1)));
                    sendRoomMessage<TGAME_PlayerScoreChange>(TGAME_PlayerScoreChange_E, tmm, root);
                    
                    if(it->second.isMidSit() || it->second.isFold())
                    {
                        continue;
                    }

                    XGameSHProto::SH_Card hdcard;
                    hdcard.set_icardtype(it->second.getCardType());
                    for(auto card : it->second.getVecCards())
                    {
                        hdcard.add_vcards(card);
                    }

                    (*shcm.mutable_mwinscore())[it->first] = it->second.getChangeNum();
                    (*shcm.mutable_mbestcard())[it->first] = hdcard;

                    DLOG_TRACE("roomid:" << root->roomid() <<"game end. uid:"<< it->second.getUid() <<", wealth:"<< it->second.getSHWealth());
                }
                shcm.set_bcompare(root->con->isNormalEnd());

                DLOG_TRACE("roomid:" << root->roomid() <<"game end. shcm:"<< logPb(shcm));
                for(auto it = usermap.begin(); it != usermap.end(); it++)
                {
                    sendClientMessage<XGameSHProto::SH_msg2sGameEnd>(it->second.getUid(), XGameSHProto::SH_msg2sGameEnd_E, shcm, root);
                }
                sendGameFinish2Room(root);

                root->con->setGameCal(true);

                EndTimer(NN_XTIME_GAME_BEGIN, root, false);
                BeginTimer(NN_XTIME_GAME_BEGIN, root->con->getFirstEnd()? 6 : 3, [](TimerParam & param)->int
                {
                    auto body = static_cast<std::tuple<GameRoot *> const *>(param.getBody());
                    auto root = std::get<0>(*body);

                    //初始化牌桌
                    root->con->roundInit();
                    //初始状态
                    root->pro->setProcess(nil_nnstate);
                    //清除所有定时器
                    EndTimer(NN_XTIME_KILL_ALL, root, false);
                    //检查游戏开始
                    CheckBegin(root);
                    return 0;
                }, root, false);

                __CATCH__
                PERFSTATS_EXIT();
            }

            int sendGameFinish2Room(GameRoot *root)
            {
                using namespace context;
                using namespace process;
                using namespace config;
                using namespace message;
                using namespace RoomSo;
                using namespace nnlogic;

                TGAME_GameFinish tmm3;

                std::map<cid_t, User> &usermap = root->con->refUserMap();

                auto& gameDetails = root->con->getGameDetails();
                gameDetails.set_ibankerid(root->con->getBankerCid());
                for (auto it = usermap.begin(); it != usermap.end(); it++)
                {
                    User *user = root->con->getUserByUid(it->second.getUid());
                    if (user == NULL || user->isMidSit())
                        continue;

                    //牌局详情-玩家信息
                    auto userinfo = gameDetails.add_userinfo();
                    userinfo->set_lplayerid(it->second.getUid());
                    userinfo->set_snickname(it->second.getNick());
                    userinfo->set_sheadstr(it->second.getUrl());
                    userinfo->set_iplayergender(it->second.getGender());
                    userinfo->set_ichairid(it->second.getCid());
                    for(auto card : it->second.getVecCards())
                    {
                        userinfo->add_hdcards(card);
                    }

                    //牌局详情-结算信息
                    auto calinfo = gameDetails.add_calinfo();
                    calinfo->set_ichairid(it->second.getCid());
                    calinfo->set_lchangenum(it->second.getChangeNum() - it->second.getRoundBetNum(-1) + it->second.getProfitNum());
                    calinfo->set_besttype(it->second.getCardType());
                    for(auto card : it->second.getVecCards())
                    {
                        calinfo->add_bestcards(card);
                    }

                    TGAME_UserActInfo act_info;
                    act_info.uid = it->second.getUid();
                    act_info.change = it->second.getChangeNum() - it->second.getRoundBetNum(-1) + it->second.getProfitNum();
                    act_info.hdCards = it->second.getVecCards();
                    act_info.isRobot = it->second.isRobot();
                    act_info.level = it->second.getDiff();
                    act_info.intoPoolCount = it->second.getRoundBetNum(-1) > root->cfg->getFrontBet() ? 1 : 0;
                    
                    //optional map<int, map<int, int>> actionCount; //操作统计  {round: {type: count}}
                    auto action_info = it->second.getRoundActionList();
                    for(auto item : action_info)
                    {
                        map<int, int> action_count_stat;
                        for(auto action : item.second)
                        {
                            auto it = action_count_stat.find(action);
                            if(it == action_count_stat.end())
                            {
                                action_count_stat.insert(std::make_pair(action, 1));
                            }
                            else
                            {
                                it->second += 1;
                            }
                            if(action >= NN_ACT_RAISE)
                            {
                                act_info.raiseCount += 1;
                            }
                        }
                        act_info.actionCount[item.first -1] = action_count_stat;
                    }
                    tmm3.mapUserActInfo[it->first] = act_info;
                }
                DLOG_TRACE("roomid:" << root->roomid() <<"gameDetails:"<< logPb(gameDetails));

                std::string smsg;
                gameDetails.SerializeToString(&smsg);
                tmm3.game_details = smsg;

                /////////////每日数据统计
                {
                    std::map<cid_t, User> &usermap = root->con->refUserMap();
                    long total_robot_win = 0;
                    long total_user_win = 0;
                    long sys_recyle = 0;
                    for (auto it = usermap.begin(); it != usermap.end(); it++)
                    {
                        tmm3.realUWealth[it->second.getUid()] = it->second.getSHWealth();
                        tmm3.mapUBet[it->second.getUid()] = it->second.getRoundBetNum(-1);
                        tmm3.mapUWinInfo[it->second.getUid()] = it->second.getChangeNum() - it->second.getRoundBetNum(-1);

                        if (it->second.isRobot())
                        {
                            total_robot_win += it->second.getChangeNum() - it->second.getRoundBetNum(-1);
                        }
                        else
                        {
                            auto changeNum = it->second.getChangeNum() - it->second.getRoundBetNum(-1);
                            total_user_win += changeNum;
                            if(changeNum > 0)
                            {
                                tmm3.day_stat.total_win += changeNum;
                            }
                            else
                            {
                                tmm3.day_stat.total_lose += changeNum;
                            }
                            tmm3.day_stat.m_UBet[it->second.getUid()] = it->second.getRoundBetNum(-1) > 0 ? 1 : 0;
                            tmm3.day_stat.m_UGame[it->second.getUid()] = 1;
                        }

                        sys_recyle += it->second.getProfitNum();
                    }

                    tmm3.day_stat.robot_win = total_robot_win;
                    tmm3.day_stat.sys_recyle = sys_recyle;
                    tmm3.day_stat.game_time = tmm3.day_stat.m_UGame.size() == 0 ? 0 : (time(NULL) - root->con->getBeginTime());

                    DLOG_TRACE("roomid:" << root->roomid() <<", sys_recyle:"<< sys_recyle << ", day_stat: "<< printTars(tmm3.day_stat));
                }

                sendRoomMessage<TGAME_GameFinish>(TGAME_GameFinish_E, tmm3, root);
                return 0;
            }
        }
    }
}
