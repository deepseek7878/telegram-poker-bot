#pragma once
#include "suoha.pb.h"
namespace game
{
    class GameRoot;

    namespace logic
    {
        namespace timeoutlogic
        {
            void BetTimeOut(GameRoot *root);
            void CalRobotAction(GameRoot *root, XGameSHProto::SH_msg2csTokenBet &shcm);
            void CheckBetAction(GameRoot *root, XGameSHProto::SH_msg2csTokenBet &shcm);
        }
    }
}

