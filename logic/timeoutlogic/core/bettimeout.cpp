#include "common/macros.h"
#include "common/nndef.h"
#include "gameroot.h"
#include "logic/timeoutlogic/core/bettimeout.h"
#include "utils/tarslog.h"
#include "context/context.h"
#include "process/process.h"
#include "message/sendclientmessage.h"
#include "logic/gamelogic/core/begintimer.h"
#include "logic/gamelogic/core/endtimer.h"
#include "logic/gamelogic/core/tokento.h"
#include "logic/clientlogic/core/tokenbet.h"
#include "config/gameconfig.h"
#include "common/nndef.h"
#include "common/nnlogic.h"
#include "suoha.pb.h"

using namespace nndef;

namespace game
{
    namespace logic
    {
        namespace timeoutlogic
        {
            std::map<int, int> actMap =
            {
                { 1, NN_ACT_FOLD },
                { 2, NN_ACT_PASS },
                { 3, NN_ACT_UNKNOWN},
                { 4, NN_ACT_FOLLOW },
                { 5, NN_ACT_ALLIN },
                { 6, NN_ACT_RAISE },
            };

            using namespace context;
            using namespace process;
            using namespace message;
            using namespace gamelogic;
            using namespace config;
            using namespace clientlogic;

            void BetTimeOut(GameRoot *root)
            {
                PERFSTATS_ENTRY();
                __TRY__

                DLOG_TRACE("roomid:" << root->roomid() << ", " << "BetTimeOut roomid:" << root->roomid());

                cid_t tokencid = root->con->getTokenCid();
                User *user = root->con->getUserByCid(tokencid);
                if(!user)
                {
                    DLOG_TRACE("roomid:" << root->roomid() <<"BetTimeOut err process: "<< root->pro->getProcess() <<" , tokencid: "<< tokencid ); 
                    return;
                }
                long round_bet_num = user->getRoundBetNum(root->pro->getProcess());

                XGameSHProto::SH_msg2csTokenBet shcm;
                shcm.set_icid(root->con->getTokenCid());
                if(user->isRobot())
                {
                    auto aiAction = root->con->getAiBetAction();
                    shcm.set_iact(NN_ACT_FOLD);
                    for(auto param : aiAction)
                    {
                        DLOG_TRACE("roomid:" << root->roomid() << ", param :" << param);
                    }
                    if(aiAction.size() == 4 && aiAction[0] == user->getUid())
                    {
                        shcm.set_iact(aiAction[1]);
                        shcm.set_lbetscore(aiAction[2]);
                    }
                    /*CalRobotAction(root, shcm);*/
                    CheckBetAction(root, shcm);
                }
                else
                {
                    long call_num = root->con->getRoundMaxBetNum() - round_bet_num;
                    if(call_num == 0)
                    {
                        shcm.set_iact(NN_ACT_PASS);
                    }
                    else
                    {
                        shcm.set_iact(NN_ACT_FOLD);
                    }
                }
               
                vector<char> vecOutBuffer;
                pbTobuffer(shcm, vecOutBuffer);
                clientlogic::TokenBet(user->getUid(), vecOutBuffer, root, false);

                __CATCH__
                PERFSTATS_EXIT();
            }

            void CalRobotAction(GameRoot *root, XGameSHProto::SH_msg2csTokenBet &shcm)
            {
                std::vector<int> vResult;
                User* user = root->con->getUserByCid(root->con->getTokenCid());
                if(!user)
                {
                    return;
                }
                E_NN_ACT act = user->getAct();
                
                float param = root->con->getRobotAction(root->cfg->getRobotAllAction(), root->con->getTokenCid(), root->roomid(), vResult);
                DLOG_TRACE("roomid:" << root->roomid() << ", " << "vResult size: " << vResult.size());
                if (vResult.size() != 2 || (vResult.size() > 1 && vResult[0] == 3))
                {
                    if ((act & NN_ACT_PASS) && !user->isLeft())
                        shcm.set_iact(NN_ACT_PASS);
                    else
                        shcm.set_iact(NN_ACT_FOLD);
                }
                else
                {
                    DLOG_TRACE("roomid:" << root->roomid() << ", " << "vResult : " << vResult[0] << "|" << vResult[1] << "|" << param);

                    long roundBetNum =  user->getRoundBetNum(root->pro->getProcess());
                    long curCallBet = root->con->getRoundMaxBetNum() - roundBetNum;
                    long minRaiseNum = root->con->getRoundMaxBetNum() - roundBetNum + root->cfg->getFrontBet();

                    auto it = actMap.find(vResult[0]);
                    if (it != actMap.end())
                    {
                        if ((act & it->second) && !user->isLeft())
                        {
                            shcm.set_iact(it->second);
                            if (it->second != NN_ACT_RAISE)
                            {
                                shcm.set_lbetscore(0);
                            }
                            else
                            {
                                std::map<int, int> addNumMap =
                                {
                                    { 1, param * root->cfg->getFrontBet() + curCallBet},
                                    { 2, param *(curCallBet + root->con->getTotalPoolNum()) + curCallBet},
                                    { 3, param * minRaiseNum + curCallBet},
                                };

                                DLOG_TRACE("roomid:" << root->roomid() << ", " << "type : " << vResult[1] << "|" << curCallBet << "|" << root->cfg->getFrontBet());

                                auto itt = addNumMap.find(vResult[1]);
                                if (itt != addNumMap.end())
                                    shcm.set_lbetscore(itt->second);
                            }
                        }
                        // else
                        // {
                        //     if ((it->second & NN_ACT_RAISE) || (it->second & NN_ACT_FOLLOW))
                        //     {
                        //         if(act & NN_ACT_PASS)
                        //         {
                        //             shcm.set_iact(NN_ACT_PASS);
                        //             shcm.set_lbetscore(0);
                        //         }
                        //     }
                        // }
                    }
                    else
                    {
                        std::map<int, int> flowNumMap =
                        {
                            { 7, 1 * root->cfg->getFrontBet()},
                            { 8, 3 * root->cfg->getFrontBet()},
                            { 9, 5 * root->cfg->getFrontBet()},
                            { 10, 0.5 * user->getSHWealth()},
                            { 11, 5 * root->cfg->getFrontBet()},
                            { 12, 5 * root->cfg->getFrontBet()},
                        };

                        auto itt = flowNumMap.find(vResult[0]);
                        if (itt != flowNumMap.end())
                        {
                            if (vResult[0] >= 7 && vResult[0] <= 10)
                            {
                                shcm.set_iact(curCallBet > itt->second ? NN_ACT_FOLD : NN_ACT_FOLLOW);
                                shcm.set_lbetscore(0);
                            }
                            else if (vResult[0] == 11)
                            {
                                shcm.set_iact(curCallBet <= 0.5 * user->getSHWealth() && curCallBet <= itt->second ? NN_ACT_FOLLOW : NN_ACT_FOLD);
                                shcm.set_lbetscore(0);
                            }
                            else if (vResult[0] == 12)
                            {
                                shcm.set_iact(curCallBet > itt->second ? NN_ACT_FOLD : NN_ACT_RAISE);
                                shcm.set_lbetscore(curCallBet > itt->second ? 0 : 2 * root->cfg->getFrontBet());
                            }
                        }
                    }
                }
            }

            //check bet is allow
            void CheckBetAction(GameRoot *root, XGameSHProto::SH_msg2csTokenBet &shcm)
            {
                DLOG_TRACE("roomid:" << root->roomid() << ", " << "shcm: " << logPb(shcm));

                int startOp = shcm.iact();                
                cid_t tokencid = root->con->getTokenCid();
                User *user = root->con->getUserByCid(tokencid);
                E_NN_ACT act = user->getAct();
                if (user == NULL)
                {
                    DLOG_TRACE("roomid:" << root->roomid() << ", " << "user not exist, cid: " << tokencid);
                    return;
                }

                if(tokencid == root->con->getWinnerCid() && shcm.iact() == NN_ACT_FOLD)
                {
                    if(NN_ACT_FOLLOW & act)
                    {
                        shcm.set_iact(NN_ACT_FOLLOW);
                        DLOG_TRACE("roomid:" << root->roomid() << ", " << "CheckBetAction flag: 1");
                    }
                    return;
                }

                long roundBetNum =  user->getRoundBetNum(root->pro->getProcess());
                long call_num = root->con->getRoundMaxBetNum() - roundBetNum;
                long minRaiseNum = root->con->getRoundMaxBetNum() - roundBetNum + root->cfg->getFrontBet();
                long allin_num = user->getSHWealth() < (root->con->getUserRemainMinWealth() + call_num) ? user->getSHWealth() : (root->con->getUserRemainMinWealth() + call_num);
                DLOG_TRACE("roomid:" << root->roomid() << ", " << "roundBetNum: " << roundBetNum << ", call_num: "<< call_num<< ", minRaiseNum: "<< minRaiseNum << ", allin_num: "<< allin_num);

                if(shcm.iact() == NN_ACT_FOLLOW)
                {
                    if(call_num == 0)
                    {
                        shcm.set_iact(NN_ACT_PASS);
                    }
                    else if(call_num >= user->getSHWealth() )
                    {
                        shcm.set_iact(NN_ACT_ALLIN);
                    }
                    /*if(call_num == 0)
                    {
                        shcm.set_iact(NN_ACT_RAISE);
                        shcm.set_lbetscore(root->cfg->getFrontBet());
                    }
                    else if(shcm.lbetscore() >= allin_num || (!(NN_ACT_FOLLOW & act) && (NN_ACT_ALLIN & act)))
                    {
                        shcm.set_iact(NN_ACT_ALLIN);
                    }*/
                }
               
                else if (shcm.iact() == NN_ACT_RAISE)
                {
                    if (shcm.lbetscore() >= allin_num)
                    {
                        shcm.set_iact(NN_ACT_ALLIN);
                    }
                    else    
                    {
                        //不正常的下注
                        if (shcm.lbetscore() < minRaiseNum)
                        {
                            DLOG_TRACE("roomid:" << root->roomid() << ", " << "user bet num less than min bet num, uid: " << user->getUid() << ", lbetscore: " << shcm.lbetscore() << ", minRaiseNum: " << minRaiseNum);

                            if ((NN_ACT_RAISE & act) && minRaiseNum <= user->getSHWealth())
                                shcm.set_lbetscore(minRaiseNum);
                            else if (NN_ACT_FOLLOW & act)
                                shcm.set_iact(NN_ACT_FOLLOW);
                            else
                                shcm.set_iact(NN_ACT_FOLD);
                        }
                    }
                }
                else if (!(shcm.iact() & act))
                {
                    DLOG_TRACE("roomid:" << root->roomid() << ", " << "act: " << act << ", bet act:" << shcm.iact());
                    shcm.set_iact(NN_ACT_FOLD);
                }

                if (shcm.iact() != NN_ACT_RAISE)
                {
                    shcm.set_lbetscore(0);
                }

                bool changeFlag = shcm.iact() == startOp;
                DLOG_TRACE("roomid:" << root->roomid() << ", " << "CheckBetAction flag: "<< changeFlag);
                DLOG_TRACE("roomid:" << root->roomid() << ", " << "shcm: " << logPb(shcm));
                return;
            }
        }
    }
}
