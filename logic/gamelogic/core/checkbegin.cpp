#include "Comm/ITableGame.h"
#include "common/macros.h"
#include "common/nndef.h"
#include "gameroot.h"
#include "logic/gamelogic/core/checkbegin.h"
#include "message/sendroommessage.h"
#include "logic/gamelogic/core/begintimer.h"
#include "logic/gamelogic/core/endtimer.h"
#include "logic/clientlogic/core/bubi.h"
#include "utils/tarslog.h"
#include "context/context.h"
#include "config/gameconfig.h"
#include "process/process.h"
#include "logic/gamelogic/core/endtimer.h"
#include "CommonCode.pb.h"
#include "message/sendclientmessage.h"
#include "suoha.pb.h"

using namespace nndef;

namespace game
{
    namespace logic
    {
        namespace gamelogic
        {
            using namespace context;
            using namespace process;
            using namespace gamelogic;
            using namespace config;
            using namespace nninvalid;
            using namespace RoomSo;
            using namespace message;

            int CheckBegin(GameRoot *root)
            {
                //清除玩家列表
                std::vector<long> vdelUser;
                std::vector<cid_t> vbubiCid;
                int readyUserCount = 0;
                std::map<cid_t, User> &usermap = root->con->refUserMap();
                for (auto it = usermap.begin(); it != usermap.end(); it++)
                {
                    DLOG_TRACE("roomid:" << root->roomid() << ", checkbegin cid" << it->first << ", uid: "<< it->second.getUid() << ", shwealth: "<< it->second.getSHWealth() << ", midsit:"<<it->second.isMidSit() << ", left: "<< it->second.isLeft() );
                    if (it->second.isMidSit())
                    {
                        continue;
                    }
                    if(it->second.isLeft())
                    {
                        vdelUser.push_back(it->second.getUid());
                        continue;
                    }
                    if(it->second.getSHWealth() <= 0 )
                    {
                        vbubiCid.push_back(it->second.getCid());
                        continue;
                    }
                    readyUserCount++;
                }
                RemoveUser(root, vdelUser);

                DLOG_TRACE("roomid:" << root->roomid() << ", checkbegin usermap size:" << usermap.size() << ", readyUserCount: "<< readyUserCount);
                if(readyUserCount >= 2)
                {
                    root->pro->setProcess(NN_STATE_GAME_BEGIN);
                    root->con->setGameCal(false);
                    EndTimer(NN_XTIME_GAME_BEGIN, root, false);
                    BeginTimer(NN_XTIME_GAME_BEGIN, root->cfg->getDelayTime(), [](TimerParam & param)->int
                    {
                        auto body = static_cast<std::tuple<GameRoot *> const *>(param.getBody());
                        auto root = std::get<0>(*body);

                        root->pro->turnProcess(NN_STATE_GAME_BEGIN);

                        return 0;
                    }, root, false);
                    
                }
                else
                {
                    //通知买币
                    DLOG_TRACE("roomid:" << root->roomid() << ", checkbegin ->vbubiCid size:" << vbubiCid.size() << ", vdelUser size:"<< vdelUser.size());

                    for (auto iter = vbubiCid.begin(); iter != vbubiCid.end(); iter++)
                    {
                        XGameSHProto::SH_msg2cBuBiNotify shcm;
                        shcm.set_icid(*iter); 
                        sendAllClientMessage<XGameSHProto::SH_msg2cBuBiNotify>(XGameSHProto::SH_msg2cBuBiNotify_E, shcm, root);

                        User* user = root->con->getUserByCid(*iter);
                        if(user && user->isRobot() && readyUserCount > 0) // 自动补币
                        {
                            EndTimer(NN_XTIME_AUTO_BUBI, root, false);
                            BeginTimer(NN_XTIME_AUTO_BUBI, rand() % 5 + 1, [user](TimerParam & param)->int
                            {
                                auto body = static_cast<std::tuple<GameRoot *> const *>(param.getBody());
                                auto root = std::get<0>(*body);
                                
                                DLOG_TRACE("roomid:" << root->roomid() << ", auto bibi cid:" << user->getCid()<< ", uid:"<< user->getUid());
                                XGameSHProto::SH_msg2csBuBi nncm;
                                nncm.set_icid(user->getCid());
                                game::logic::clientlogic::BuBi(user->getUid(), pbTobuffer<XGameSHProto::SH_msg2csBuBi>(nncm), root);
                               
                                return 0;
                            }, root, false);
                        }
                    }
                    //
                    if(readyUserCount == 1)
                    {
                        EndTimer(NN_XTIME_BUBI, root, false);
                        BeginTimer(NN_XTIME_BUBI, 5, [](TimerParam & param)->int
                        {
                            auto body = static_cast<std::tuple<GameRoot *> const *>(param.getBody());
                            auto root = std::get<0>(*body);

                            std::vector<long> vdelUser;
                            std::map<cid_t, User> &usermap = root->con->refUserMap();

                            //只剩下机器人
                            bool allRobotFlag = true;
                            for(auto it = usermap.begin(); it != usermap.end(); it++)
                            {
                                if(!it->second.isRobot() && it->second.getSHWealth() > 0)
                                {
                                    allRobotFlag = false;
                                }
                            }

                            for (auto it = usermap.begin(); it != usermap.end(); it++)
                            {
                                //更新房间玩家状态
                                TGAME_UserStatus smm;
                                if(it->second.getSHWealth() > 0 && !allRobotFlag)
                                {
                                    XGameSHProto::SH_msg2cReMatchNotify shn;
                                    shn.set_icid(it->first);
                                    shn.set_ibetid(root->cfg->getBlindLevel());
                                    shn.set_iseatnum(root->cfg->getMaxSeatNum());
                                    DLOG_TRACE("roomid:" << root->roomid() << ", checkbegin SH_msg2cReMatchNotify cid:" << it->first);
                                    sendAllClientMessage<XGameSHProto::SH_msg2cReMatchNotify>(XGameSHProto::SH_msg2cReMatchNotify_E, shn, root);
                                
                                    smm.userStatus.insert(std::make_pair(it->second.getUid(), 8));

                                  /*  //变更货币
                                    TGAME_PlayerScoreChange tmm;
                                    tmm.info.buyInNum = 0;
                                    tmm.info.winNum = 0;
                                    tmm.info.buyInCount = 0;
                                    tmm.aiInfo.iAiGameRound = 0;
                                    tmm.aiInfo.iAiGamePoint = 0;
                                    tmm.iNowRound = 0;
                                    tmm.changeType = 2;
                                    tmm.changePoint.insert(std::make_pair(it->second.getUid(), it->second.getSHWealth()));
                                    sendRoomMessage<TGAME_PlayerScoreChange>(TGAME_PlayerScoreChange_E, tmm, root);*/

                                    it->second.setSHWealth(0);
                                }
                                else
                                {
                                    vdelUser.push_back(it->second.getUid());
                                }
                                if(smm.userStatus.size() > 0)
                                {
                                    sendRoomMessage<TGAME_UserStatus>(TGAME_UserStatus_E, smm, root);
                                }   
                            }
                            RemoveUser(root, vdelUser);
                            return 0;
                        }, root, false);
                    }  
                }
                return 0;
            }

            int RemoveUser(GameRoot *root, std::vector<long> vdelUser)
            {
                for (auto iter = vdelUser.begin(); iter != vdelUser.end(); iter++)
                {
                    User* user = root->con->getUserByUid(*iter);
                    if(user)
                    {
                        /*if(user->getSHWealth() > 0)
                        {
                            //变更货币
                            TGAME_PlayerScoreChange tmm;
                            tmm.info.buyInNum = 0;
                            tmm.info.winNum = 0;
                            tmm.info.buyInCount = 0;
                            tmm.aiInfo.iAiGameRound = 0;
                            tmm.aiInfo.iAiGamePoint = 0;
                            tmm.iNowRound = 0;
                            tmm.changeType = 2;
                            tmm.changePoint.insert(std::make_pair(user->getUid(), user->getSHWealth()));
                            sendRoomMessage<TGAME_PlayerScoreChange>(TGAME_PlayerScoreChange_E, tmm, root);
                        }*/
                        
                        DLOG_TRACE("roomid:" << root->roomid() << ", del, uid: " << user->getUid() << ", Coin: " << user->getSHWealth());
                        
                        //站起消息
                        TGAME_Stand tmm;
                        tmm.lPlayerID = *iter;
                        tmm.iType = 0;
                        tmm.bBuyIn = false;
                        sendRoomMessage<TGAME_Stand>(TGAME_Stand_E, tmm, root);
                        root->con->delUser(*iter);
                    }
                }
                return 0;
            }
        }
    }
}
