/**
 * File: BehaviorBlackJack.cpp
 *
 * Author: Sam Russell
 * Created: 2018-05-09
 *
 * Description: BlackJack
 *
 * Copyright: Anki, Inc. 2018
 *
 **/

#include "engine/aiComponent/behaviorComponent/behaviors/blackjack/behaviorBlackJack.h"

#include "engine/actions/animActions.h"
#include "engine/aiComponent/behaviorComponent/behaviorContainer.h"
#include "engine/aiComponent/behaviorComponent/behaviorExternalInterface/beiRobotInfo.h"
#include "engine/aiComponent/behaviorComponent/behaviors/animationWrappers/behaviorTextToSpeechLoop.h"
#include "engine/aiComponent/behaviorComponent/behaviors/basicWorldInteractions/behaviorLookAtFaceInFront.h"
#include "engine/aiComponent/behaviorComponent/behaviors/robotDrivenDialog/behaviorPromptUserForVoiceCommand.h"
#include "engine/aiComponent/behaviorComponent/userIntentComponent.h"
#include "engine/aiComponent/behaviorComponent/userIntents.h"
#include "engine/components/localeComponent.h"
#include "engine/components/robotStatsTracker.h"
#include "engine/clad/types/animationTypes.h"

#include "clad/types/behaviorComponent/behaviorStats.h"
#include "coretech/common/engine/utils/timer.h"
#include "util/logging/DAS.h"

#define SET_STATE(s) do{ \
                          _dVars.state = EState::s; \
                          PRINT_CH_INFO("Behaviors", "BehaviorBlackJack.State", "State = %s", #s); \
                        } while(0);

//
// Localization keys defined in BlackJackStrings.json
//
namespace {
  const char * kDealerGoodLuck = "BlackJack.DealerGoodLuck";
  const char * kDealerScore = "BlackJack.DealerScore";
  const char * kDealerScoreBusted = "BlackJack.DealerScoreBusted";
  const char * kDealerScorePush = "BlackJack.DealerScorePush";
  const char * kDealerScoreDealerWins = "BlackJack.DealerScoreDealerWins";
  const char * kDealerScorePlayerWins = "BlackJack.DealerScorePlayerWins";
  const char * kDealerTwentyOne = "BlackJack.DealerTwentyOne";
  const char * kDealerTurn = "BlackJack.DealerTurn";

  const char * kPlayerScore = "BlackJack.PlayerScore";
  const char * kPlayerScoreBusted = "BlackJack.PlayerScoreBusted";
  const char * kPlayerTwentyOne = "BlackJack.PlayerTwentyOne";
  const char * kPlayerWinsBlackJack = "BlackJack.PlayerWinsBlackJack";
  const char * kPlayerWinsNaturalBlackJack = "BlackJack.PlayerWinsNaturalBlackJack";
  const char * kPlayerWinsFiveCardCharlie = "BlackJack.PlayerWinsFiveCardCharlie";
}

namespace Anki {
namespace Vector {

namespace{
  const bool kFaceUp = true;
  const bool kFaceDown = false;

  static const UserIntentTag affirmativeIntent = USER_INTENT(imperative_affirmative);
  static const UserIntentTag negativeIntent = USER_INTENT(imperative_negative);
  static const UserIntentTag silenceIntent = USER_INTENT(silence);
  static const UserIntentTag playerHitIntent = USER_INTENT(blackjack_hit);
  static const UserIntentTag playerStandIntent = USER_INTENT(blackjack_stand);
  static const UserIntentTag playAgainIntent = USER_INTENT(blackjack_playagain);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorBlackJack::InstanceConfig::InstanceConfig()
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorBlackJack::DynamicVariables::DynamicVariables()
: state(EState::TurnToFace)
, dealingState(EDealingState::PlayerFirstCard)
, outcome(EOutcome::Tie)
, gameStartTime_s(0.0f)
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorBlackJack::BehaviorBlackJack(const Json::Value& config)
: ICozmoBehavior(config)
, _game()
, _visualizer(&_game)
, _sessionStartTime_s(0.0f)
, _humanWinsInSession(0)
, _robotWinsInSession(0)
, _gamesInSession(0)
, _newSession(true)
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
BehaviorBlackJack::~BehaviorBlackJack()
{
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorBlackJack::GetBehaviorOperationModifiers(BehaviorOperationModifiers& modifiers) const
{
  modifiers.wantsToBeActivatedWhenOffTreads = true;
  modifiers.behaviorAlwaysDelegates = false;
  modifiers.visionModesForActiveScope->insert({ VisionMode::Faces, EVisionUpdateFrequency::Low });
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorBlackJack::GetAllDelegates(std::set<IBehavior*>& delegates) const
{
  delegates.insert(_iConfig.hitOrStandPromptBehavior.get());
  delegates.insert(_iConfig.playAgainPromptBehavior.get());
  delegates.insert(_iConfig.ttsBehavior.get());
  delegates.insert(_iConfig.goodLuckTTSBehavior.get());
  delegates.insert(_iConfig.lookAtFaceInFrontBehavior.get());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
bool BehaviorBlackJack::WantsToBeActivatedBehavior() const
{
  return true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorBlackJack::InitBehavior()
{
  const auto& BC = GetBEI().GetBehaviorContainer();
  BC.FindBehaviorByIDAndDowncast( BEHAVIOR_ID(BlackJackHitOrStandPrompt),
                                  BEHAVIOR_CLASS(PromptUserForVoiceCommand),
                                  _iConfig.hitOrStandPromptBehavior );

  BC.FindBehaviorByIDAndDowncast( BEHAVIOR_ID(BlackJackRequestToPlayAgain),
                                  BEHAVIOR_CLASS(PromptUserForVoiceCommand),
                                  _iConfig.playAgainPromptBehavior );

  BC.FindBehaviorByIDAndDowncast( BEHAVIOR_ID(BlackJackTextToSpeech),
                                  BEHAVIOR_CLASS(TextToSpeechLoop),
                                  _iConfig.ttsBehavior );

  BC.FindBehaviorByIDAndDowncast( BEHAVIOR_ID(BlackJackGoodLuckTTS),
                                  BEHAVIOR_CLASS(TextToSpeechLoop),
                                  _iConfig.goodLuckTTSBehavior );

  BC.FindBehaviorByIDAndDowncast( BEHAVIOR_ID(BlackJackLookAtFaceInFront),
                                  BEHAVIOR_CLASS(LookAtFaceInFront),
                                  _iConfig.lookAtFaceInFrontBehavior );

  _visualizer.VerifySpriteAssets(GetBEI());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorBlackJack::OnBehaviorActivated()
{
  // reset dynamic variables
  _dVars = DynamicVariables();
  _game.Init(&GetRNG());
  _visualizer.Init(GetBEI());

  _dVars.gameStartTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();

  // --- Log DAS events ---
  // Session DAS
  if(_newSession){
    DASMSG(behavior_blackjack_session_start,
           "behavior.blackjack_session_start",
           "A new session of BlackJack has started");
    DASMSG_SEND();
    _sessionStartTime_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds();
    _newSession = false;
  }

  // Game DAS
  DASMSG(behavior_blackjack_game_start,
         "behavior.blackjack_game_start",
         "A Game of BlackJack has just started");
  DASMSG_SEND();

  if (IsXray()) {
    // Up the cpu frequency to the max
    (void)system("curl 'http://localhost:8080/api/mods/modify/FreqChange' -X POST -H 'Referer: http://localhost:8080/' -H 'Origin: http://localhost:8080' -H 'Content-Type: application/json' --data-raw '{\"freq\":2}'");
  }

  // --- On With the Game ---
  TransitionToTurnToFace();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorBlackJack::OnBehaviorDeactivated()
{
  _visualizer.ReleaseControlAndClearState(GetBEI());

  if (IsXray()) {
    // Now that the behavior has finished set the cpu speed back to something reasonable
    (void)system("curl 'http://localhost:8080/api/mods/modify/FreqChange' -X POST -H 'Referer: http://localhost:8080/' -H 'Origin: http://localhost:8080' -H 'Content-Type: application/json' --data-raw '{\"freq\":1}'");
  }

  // Log session end DAS events and track DAS related state
  std::string sessionWinLoseString(std::to_string(_humanWinsInSession) + "," + std::to_string(_robotWinsInSession));
  float timeInSession_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() - _sessionStartTime_s;
  DASMSG(behavior_blackjack_session_end,
         "behavior.blackjack_session_end",
         "User has ended blackjack session");
  DASMSG_SET(s1, sessionWinLoseString, "win/lose status: 'numHumanWins,numRobotWins'");
  DASMSG_SET(s2, _gamesInSession, "Games in session (played back to back)");
  DASMSG_SET(i1, std::round(timeInSession_s), "Time spent in current session (seconds)");
  DASMSG_SEND();

  _sessionStartTime_s = 0.0f;
  _humanWinsInSession = 0;
  _robotWinsInSession = 0;
  _gamesInSession = 0;
  _newSession = true;
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorBlackJack::BehaviorUpdate()
{
  if( !IsActivated() ) {
    return;
  }

  _visualizer.Update(GetBEI());
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorBlackJack::TransitionToTurnToFace()
{
  SET_STATE(TurnToFace);
  if(_iConfig.lookAtFaceInFrontBehavior->WantsToBeActivated()){
    DelegateIfInControl(_iConfig.lookAtFaceInFrontBehavior.get(), &BehaviorBlackJack::TransitionToGetIn);
  } else {
    TransitionToGetIn();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorBlackJack::TransitionToGetIn()
{
  SET_STATE(GetIn);
  DelegateIfInControl(new TriggerAnimationAction(AnimationTrigger::BlackJack_GetIn),
                      &BehaviorBlackJack::TransitionToDealing);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorBlackJack::TransitionToDealing()
{
  SET_STATE(Dealing);
  switch(_dVars.dealingState){
    case EDealingState::PlayerFirstCard:
    {
      _game.DealToPlayer(kFaceUp);
      _visualizer.DealToPlayer(GetBEI(),[this]()
        {
          _dVars.dealingState = EDealingState::DealerFirstCard;
          // keep an eye out for Aces
          if(_game.LastCard().IsAnAce()) {
            const auto & tts = GetLocalizedString(kDealerGoodLuck);
            _iConfig.goodLuckTTSBehavior->SetTextToSay(tts);
            if(!ANKI_VERIFY(_iConfig.goodLuckTTSBehavior->WantsToBeActivated(),
                            "BehaviorBlackjack.TTSError",
                            "The Good Luck TTS behavior did not want to be activated, this indicates a usage error")){
              CancelSelf();
            } else{
              DelegateIfInControl(_iConfig.goodLuckTTSBehavior.get(),
                                  &BehaviorBlackJack::TransitionToDealing);
            }
          } else {
            TransitionToDealing();
          }
        });
      break;
    }
    case EDealingState::DealerFirstCard:
    {
      _game.DealToDealer(kFaceDown);
      _visualizer.DealToDealer(GetBEI(), [this]()
        {
          _dVars.dealingState = EDealingState::PlayerSecondCard;
          TransitionToDealing();
        });
      break;
    }
    case EDealingState::PlayerSecondCard:
    {
      _game.DealToPlayer(kFaceUp);
      _visualizer.DealToPlayer(GetBEI(), [this]()
        {
          _dVars.dealingState = EDealingState::DealerSecondCard;
          // Keep an eye out for natural BlackJack
          if(_game.PlayerHasBlackJack()){
            _dVars.outcome = EOutcome::VictorLosesBlackJack;
            const auto & tts = GetLocalizedString(kPlayerWinsNaturalBlackJack);
            DelegateIfInControl(SetUpSpeakingBehavior(tts), &BehaviorBlackJack::TransitionToEndGame);

          } else {
            TransitionToDealing();
          }
        });
      break;
    }
    case EDealingState::DealerSecondCard:
    {
      _game.DealToDealer(kFaceUp);
      _visualizer.DealToDealer(GetBEI(), [this]()
        {
          _dVars.dealingState = EDealingState::Finished;
          // Respond to BlackJack for the player
          if(_game.PlayerHasBlackJack()){
            _dVars.outcome = EOutcome::VictorLosesBlackJack;
            const auto & tts = GetLocalizedString(kPlayerWinsBlackJack);
            DelegateIfInControl(SetUpSpeakingBehavior(tts), &BehaviorBlackJack::TransitionToEndGame);
          } else {
            TransitionToReactToPlayerCard();
          }
        });
      break;
    }
    case EDealingState::Finished:
    {
      PRINT_NAMED_ERROR("BehaviorBlackJack.InvalidDealingState",
                        "Should never enter TransitionToDealing() when DealingState is: Finished");
      break;
    }
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorBlackJack::TransitionToReactToPlayerCard()
{
  SET_STATE(ReactToPlayerCard);
  if(_game.PlayerBusted()) {
    // Build the card value and bust string and action
    const auto & tts = GetLocalizedString(kPlayerScoreBusted, _game.GetPlayerScore());
    _dVars.outcome = EOutcome::VictorWins;
    DelegateIfInControl(SetUpSpeakingBehavior(tts), &BehaviorBlackJack::TransitionToEndGame);
  } else if(_game.PlayerHasCharlie()) {
    const auto & tts = GetLocalizedString(kPlayerScore, _game.GetPlayerScore());
    DelegateIfInControl(SetUpSpeakingBehavior(tts), [this]()
      {
        _visualizer.DisplayCharlieFrame(GetBEI(), [this]()
          {
            _dVars.outcome = EOutcome::VictorLosesBlackJack;
            const auto & tts = GetLocalizedString(kPlayerWinsFiveCardCharlie);
            DelegateIfInControl(SetUpSpeakingBehavior(tts), &BehaviorBlackJack::TransitionToEndGame);
          }
        );
      }
    );
  }else if(_game.PlayerHasBlackJack()){
    // Player got a BlackJack, but Victor could still tie
    const auto & tts = GetLocalizedString(kPlayerTwentyOne);
    DelegateIfInControl(SetUpSpeakingBehavior(tts), &BehaviorBlackJack::TransitionToVictorsTurn);
  } else {
    // Build the card value string and read out action
    const auto & tts = GetLocalizedString(kPlayerScore, _game.GetPlayerScore());
    DelegateIfInControl(SetUpSpeakingBehavior(tts), &BehaviorBlackJack::TransitionToHitOrStandPrompt);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorBlackJack::TransitionToHitOrStandPrompt()
{
  SET_STATE(HitOrStandPrompt);
  if(_iConfig.hitOrStandPromptBehavior->WantsToBeActivated()){
    DelegateIfInControl(_iConfig.hitOrStandPromptBehavior.get(), &BehaviorBlackJack::TransitionToHitOrStand);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorBlackJack::TransitionToHitOrStand()
{
  SET_STATE(HitOrStand);
  UserIntentComponent& uic = GetBehaviorComp<UserIntentComponent>();

  if(uic.IsUserIntentPending(playerHitIntent) ||
     uic.IsUserIntentPending(affirmativeIntent) ){

    if(uic.IsUserIntentPending(playerHitIntent)){
      uic.DropUserIntent(playerHitIntent);
    } else if (uic.IsUserIntentPending(affirmativeIntent)){
      uic.DropUserIntent(affirmativeIntent);
    }

    _game.DealToPlayer();
    _visualizer.DealToPlayer(GetBEI(), std::bind(&BehaviorBlackJack::TransitionToReactToPlayerCard, this));

  } else {
    // Stand if:
    // 1. We received a valid playerStandIntent or imperative_negative
    if (uic.IsUserIntentPending(playerStandIntent)) {
      uic.DropUserIntent(playerStandIntent);
    } else if(uic.IsUserIntentPending(negativeIntent)) {
      uic.DropUserIntent(negativeIntent);
    } else if(uic.IsUserIntentPending(silenceIntent)) {
      uic.DropUserIntent(silenceIntent);
    }

    // 2. We didn't receive any intents at all
    DelegateIfInControl(new TriggerAnimationAction(AnimationTrigger::BlackJack_Response),
      [this](){
        TransitionToVictorsTurn();
      });
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorBlackJack::TransitionToVictorsTurn()
{
  SET_STATE(VictorsTurn);

  // TODO:(str) work out whether a "spread" is happening or not, dependent on dynamic layouts
  // _visualizer.VisualizeSpread();
  const auto & tts = GetLocalizedString(kDealerTurn);
  DelegateIfInControl(SetUpSpeakingBehavior(tts),
    [this](){
      _game.Flop();
      _visualizer.Flop(GetBEI(), std::bind(&BehaviorBlackJack::TransitionToReactToDealerCard, this));
    });
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorBlackJack::TransitionToDealToVictor()
{
  SET_STATE(DealToVictor);

  _game.DealToDealer();
  _visualizer.DealToDealer(GetBEI(), std::bind(&BehaviorBlackJack::TransitionToReactToDealerCard, this));
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorBlackJack::TransitionToReactToDealerCard()
{
  SET_STATE(ReactToDealerCard);

  if(_game.DealerBusted()){
    // Announce score and bust
    _dVars.outcome = EOutcome::VictorBusts;
    const auto & tts = GetLocalizedString(kDealerScoreBusted, _game.GetDealerScore());
    DelegateIfInControl(SetUpSpeakingBehavior(tts), &BehaviorBlackJack::TransitionToEndGame);
  } else if(_game.DealerTied()){
    _dVars.outcome = EOutcome::Tie;
    const auto & tts = GetLocalizedString(kDealerScorePush, _game.GetDealerScore());
    DelegateIfInControl(SetUpSpeakingBehavior(tts), &BehaviorBlackJack::TransitionToEndGame);
  } else if(_game.DealerHasBlackJack()){
    _dVars.outcome = EOutcome::VictorWinsBlackJack;
    const auto & tts = GetLocalizedString(kDealerTwentyOne);
    DelegateIfInControl(SetUpSpeakingBehavior(tts), &BehaviorBlackJack::TransitionToEndGame);
  } else if(_game.DealerHasWon()){
    _dVars.outcome = EOutcome::VictorWins;
    const auto & tts = GetLocalizedString(kDealerScoreDealerWins, _game.GetDealerScore());
    DelegateIfInControl(SetUpSpeakingBehavior(tts), &BehaviorBlackJack::TransitionToEndGame);
  } else if(_game.DealerHasLost()) {
    _dVars.outcome = EOutcome::VictorLoses;
    const auto & tts = GetLocalizedString(kDealerScorePlayerWins, _game.GetDealerScore());
    DelegateIfInControl(SetUpSpeakingBehavior(tts), &BehaviorBlackJack::TransitionToEndGame);
  } else {
    // Announce score and Hit again
    const auto & tts = GetLocalizedString(kDealerScore, _game.GetDealerScore());
    DelegateIfInControl(SetUpSpeakingBehavior(tts), &BehaviorBlackJack::TransitionToDealToVictor);
  }

}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorBlackJack::TransitionToEndGame(){

  SET_STATE(EndGame);

  GetBehaviorComp<RobotStatsTracker>().IncrementBehaviorStat(BehaviorStat::BlackjackGameComplete);

  // dasOutcome represents the HUMAN outcome of the game
  std::string dasOutcome;
  TriggerAnimationAction* endGameAction = nullptr;
  switch(_dVars.outcome){
    case EOutcome::Tie:
    {
      dasOutcome = "tie";
      endGameAction = new TriggerAnimationAction(AnimationTrigger::BlackJack_VictorPush);
      break;
    }
    case EOutcome::VictorWinsBlackJack:
    {
      GetBehaviorComp<RobotStatsTracker>().IncrementBehaviorStat(BehaviorStat::BlackjackDealerWon);
      dasOutcome = "loss";
      endGameAction = new TriggerAnimationAction(AnimationTrigger::BlackJack_VictorBlackJackWin);
      break;
    }
    case EOutcome::VictorWins:
    {
      GetBehaviorComp<RobotStatsTracker>().IncrementBehaviorStat(BehaviorStat::BlackjackDealerWon);
      dasOutcome = "loss";
      endGameAction = new TriggerAnimationAction(AnimationTrigger::BlackJack_VictorWin);
      break;
    }
    case EOutcome::VictorLosesBlackJack:
    {
      dasOutcome = "win";
      endGameAction = new TriggerAnimationAction(AnimationTrigger::BlackJack_VictorBlackJackLose);
      break;
    }
    case EOutcome::VictorBusts:
    {
      dasOutcome = "win";
      endGameAction = new TriggerAnimationAction(AnimationTrigger::BlackJack_VictorBust);
      break;
    }
    case EOutcome::VictorLoses:
    {
      dasOutcome = "win";
      endGameAction = new TriggerAnimationAction(AnimationTrigger::BlackJack_VictorLose);
      break;
    }
  }

  _visualizer.SwipeToClearFace(GetBEI(),
    [this, endGameAction](){
      DelegateIfInControl(endGameAction, &BehaviorBlackJack::TransitionToPlayAgainPrompt);
    });

  // --- Log DAS Events ---
  _gamesInSession++;
  std::string dasWinningScoreString;
  if(dasOutcome == "win"){
    dasWinningScoreString = std::to_string(_game.GetPlayerScore());
    _humanWinsInSession++;
  }
  else if (dasOutcome == "loss"){
    dasWinningScoreString = std::to_string(_game.GetDealerScore());
    _robotWinsInSession++;
  }
  else if (dasOutcome == "tie"){
    dasWinningScoreString = std::to_string(_game.GetDealerScore());
  }

  float timeInGame_s = BaseStationTimer::getInstance()->GetCurrentTimeInSeconds() - _dVars.gameStartTime_s;

  DASMSG(behavior_blackjack_game_end,
         "behavior.blackjack_game_end",
         "BlackJack game finished, reporting outcome");
  DASMSG_SET(s1, dasOutcome, "Outcome of the game for the user (win, loss, tie)");
  DASMSG_SET(s2, dasWinningScoreString, "Winning score i.e. user score if user won, robot score if robot won");
  DASMSG_SET(i1, std::round(timeInGame_s), "time spent in this round of the game (seconds)");
  DASMSG_SEND();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorBlackJack::TransitionToPlayAgainPrompt()
{
  SET_STATE(PlayAgainPrompt);
  if(_iConfig.playAgainPromptBehavior->WantsToBeActivated()){
    DelegateIfInControl(_iConfig.playAgainPromptBehavior.get(), &BehaviorBlackJack::TransitionToPlayAgain);
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorBlackJack::TransitionToPlayAgain()
{
  SET_STATE(PlayAgain);
  UserIntentComponent& uic = GetBehaviorComp<UserIntentComponent>();

  if(uic.IsUserIntentPending(playAgainIntent)){
    uic.DropUserIntent(playAgainIntent);
    OnBehaviorActivated();
  } else if(uic.IsUserIntentPending(affirmativeIntent)){
    uic.DropUserIntent(affirmativeIntent);
    OnBehaviorActivated();
  } else {
    if (uic.IsUserIntentPending(negativeIntent)){
      uic.DropUserIntent(negativeIntent);
    } else if(uic.IsUserIntentPending(silenceIntent)) {
      uic.DropUserIntent(silenceIntent);
    }
    TransitionToGetOut();
  }
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
void BehaviorBlackJack::TransitionToGetOut()
{
  _visualizer.ClearCards(GetBEI());
  DelegateIfInControl(new TriggerAnimationAction(AnimationTrigger::BlackJack_Quit), [this]()
    {
      CancelSelf();
    }
  );
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
IBehavior* BehaviorBlackJack::SetUpSpeakingBehavior(const std::string& vocalizationString)
{
  _iConfig.ttsBehavior->SetTextToSay(vocalizationString);
  if(!ANKI_VERIFY(_iConfig.ttsBehavior->WantsToBeActivated(),
                 "BehaviorBlackjack.TTSError",
                 "The TTSLoop behavior did not want to be activated, this indicates a usage error")){
    CancelSelf();
  }
  return _iConfig.ttsBehavior.get();
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::string BehaviorBlackJack::GetLocalizedString(const std::string & key)
{
  const auto & localeComponent = GetBEI().GetRobotInfo().GetLocaleComponent();
  return localeComponent.GetString(key);
}

// - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - - -
std::string BehaviorBlackJack::GetLocalizedString(const std::string & key, const int score)
{
  const auto & localeComponent = GetBEI().GetRobotInfo().GetLocaleComponent();
  return localeComponent.GetString(key, std::to_string(score));
}

} // namespace Vector
} // namespace Anki
