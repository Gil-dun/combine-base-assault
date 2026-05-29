//========= Copyright Valve Corporation, All rights reserved. ============//
//
// Purpose: 
//
//=============================================================================


#include "cbase.h"

#ifdef GAME_DLL

#include "achievementmgr.h"
#include "baseachievement.h"

#define ACHIEVEMENT_CBAR_COMPLETE_BASEASSAULT 151
#define ACHIEVEMENT_CBAR_COMPLETE_DANGEROUSENV 152
#define ACHIEVEMENT_CBAR_COMPLETE_STREETCHAOS 153
#define ACHIEVEMENT_CBAR_COMPLETE_FOUNDISLAND 154
#define ACHIEVEMENT_CBAR_ALEXANDER_SURVIVE 163
#define ACHIEVEMENT_CBAR_DIED_ON_CBA00 164
#define ACHIEVEMENT_CBAR_KILL_100_SOLDIERS 165
#define ACHIEVEMENT_CBAR_SPEEDRUN_CBA02 166
#define ACHIEVEMENT_CBAR_COLLECT_AWARDS 167
#define ACHIEVEMENT_BG_COMPLETE_UNDAREAS 155
#define ACHIEVEMENT_BG_COMPLETE_CMBOUTPOST 156
#define ACHIEVEMENT_BG_COMPLETE_NEWFRIEND 157
#define ACHIEVEMENT_BG_COMPLETE_TPP 158
#define ACHIEVEMENT_CBAR_BADEND 159
#define ACHIEVEMENT_CBAR_GOODEND 160
#define ACHIEVEMENT_BG_GOODEND 161
#define ACHIEVEMENT_BG_BADEND 162


class CAchievementCBARAlexander : public CFailableAchievement
{
	void Init() override
	{
		SetFlags(ACH_LISTEN_MAP_EVENTS | ACH_LISTEN_KILL_EVENTS | ACH_SAVE_GLOBAL | ACH_SAVE_WITH_GAME);
		SetGoal(1);
		SetVictimFilter("npc_citizen");
	}

	virtual void Event_EntityKilled(CBaseEntity* pVictim, CBaseEntity* pAttacker, CBaseEntity* pInflictor, IGameEvent* event)
	{
		const char* pszName = GetModelName(pVictim);

		// skip past any directories and get just the file name
		pszName = V_UnqualifiedFileName(pszName);
		// if model name matches one which Alexander Uses, Achievement fails.
		if ((0 == Q_stricmp(pszName, "alexander.mdl")))
		{
			SetAchieved(false);
			SetFailed();
		}
	}

	// map event where achievement evalution starts
	virtual const char* GetActivationEventName() { return "CBAR_ALEXANDER_SURVIVE_START"; }
	// map event where achievement is evaluated for success
	virtual const char* GetEvaluationEventName() { return "CBAR_ALEXANDER_SURVIVE_END"; }
};
DECLARE_ACHIEVEMENT(CAchievementCBARAlexander, ACHIEVEMENT_CBAR_ALEXANDER_SURVIVE, "CBAR_ALEXANDER_SURVIVE", 1);


class CAchievementCBARKillCombines : public CBaseAchievement
{
	void Init()
	{
		SetFlags(ACH_LISTEN_KILL_EVENTS | ACH_SAVE_GLOBAL);
		SetGoal(100);
	}

	virtual void Event_EntityKilled(CBaseEntity* pVictim, CBaseEntity* pAttacker, CBaseEntity* pInflictor, IGameEvent* event)
	{
		if (!pAttacker)
			return;

		if (!pVictim)
			return;

		if (!pAttacker->IsPlayer())
			return;

		CBaseEntity* pVictimClassname = static_cast<CBaseEntity*>(pVictim);
		if (pVictimClassname && pVictimClassname->ClassMatches("npc_combine_s") || pVictimClassname->ClassMatches("npc_metropolice") || pVictimClassname->ClassMatches("npc_sniper"))
		{
			IncrementCount();
		}
	}
};
DECLARE_ACHIEVEMENT(CAchievementCBARKillCombines, ACHIEVEMENT_CBAR_KILL_100_SOLDIERS, "CBAR_KILL_100_SOLDIERS", 5);

class CAchievementCBARSpeedRun02 : public CFailableAchievement
{
public:
	DECLARE_CLASS(CAchievementCBARSpeedRun02, CFailableAchievement);

	static float GetGoalTime()
	{
		return 120.0F;	// 2 minutes
	}

	void Init() OVERRIDE
	{
		SetFlags(ACH_LISTEN_MAP_EVENTS | ACH_SAVE_WITH_GAME);
		SetGoal(1);
		m_flStartTime = 0.0f;
	}

	const char* GetActivationEventName() OVERRIDE
	{
		return "CBAR_02_SPEEDRUN_START";
	}

	const char* GetEvaluationEventName() OVERRIDE
	{
		return "CBAR_02_SPEEDRUN_END";
	}

	void PreRestoreSavedGame() OVERRIDE
	{
		m_flStartTime = 0.0f;
		BaseClass::PreRestoreSavedGame();
	}

	void OnActivationEvent() OVERRIDE
	{
		m_flStartTime = gpGlobals->curtime;
		BaseClass::OnActivationEvent();
	}

	void OnEvaluationEvent() OVERRIDE
	{
		if (IsAchieved())
			return;

		if (gpGlobals->curtime > m_flStartTime + GetGoalTime())
			SetFailed();

		BaseClass::OnEvaluationEvent();
	}

	void PrintAdditionalStatus() OVERRIDE
	{
		if (IsActive())
		{
			Msg(
				"Time since Speedrun Started: %2.2f seconds  Goal time: %2.2f seconds",
				gpGlobals->curtime - m_flStartTime,
				GetGoalTime()
			);
		}
	}

	DECLARE_DATADESC();

private:
	float m_flStartTime;
};
DECLARE_ACHIEVEMENT(CAchievementCBARSpeedRun02, ACHIEVEMENT_CBAR_SPEEDRUN_CBA02, "CBAR_SPEEDRUN_CBA02", 1);

BEGIN_DATADESC(CAchievementCBARSpeedRun02)
DEFINE_FIELD(m_flStartTime, FIELD_TIME),
END_DATADESC()


class CAchievementCBARDiedOn00 : public CBaseAchievement
{
	void Init()
	{
		SetFlags(ACH_LISTEN_KILL_EVENTS | ACH_SAVE_WITH_GAME);
		SetMapNameFilter("cba_00");
		SetVictimFilter("player");
		SetGoal(1);
	}
};
DECLARE_ACHIEVEMENT(CAchievementCBARDiedOn00, ACHIEVEMENT_CBAR_DIED_ON_CBA00, "CBAR_DIED_ON_CBA00", 5);

class CAchievementCBARCollectAwards : public CBaseAchievement
{
	virtual void Init()
	{
		static const char* szComponents[] =
		{
			"CBAR_AWARD_00", "CBAR_AWARD_01", "CBAR_AWARD_02", "CBAR_AWARD_05",
		};
		SetFlags(ACH_HAS_COMPONENTS | ACH_LISTEN_COMPONENT_EVENTS | ACH_SAVE_GLOBAL);
		m_pszComponentNames = szComponents;
		m_iNumComponents = ARRAYSIZE(szComponents);
		SetComponentPrefix("CBAR_AWARD");
		SetGoal(m_iNumComponents);
	}
};
DECLARE_ACHIEVEMENT(CAchievementCBARCollectAwards, ACHIEVEMENT_CBAR_COLLECT_AWARDS, "CBAR_COLLECT_AWARDS", 5);


// Map events go here
DECLARE_MAP_EVENT_ACHIEVEMENT( ACHIEVEMENT_CBAR_COMPLETE_BASEASSAULT, "CBAR_COMPLETE_BASEASSAULT", 1 );
DECLARE_MAP_EVENT_ACHIEVEMENT( ACHIEVEMENT_CBAR_COMPLETE_DANGEROUSENV, "CBAR_COMPLETE_DANGEROUSENV", 1 );
DECLARE_MAP_EVENT_ACHIEVEMENT( ACHIEVEMENT_CBAR_COMPLETE_STREETCHAOS, "CBAR_COMPLETE_STREETCHAOS", 1 );
DECLARE_MAP_EVENT_ACHIEVEMENT( ACHIEVEMENT_CBAR_COMPLETE_FOUNDISLAND, "CBAR_COMPLETE_FOUNDISLAND", 1 );
//DECLARE_MAP_EVENT_ACHIEVEMENT( ACHIEVEMENT_CBAR_DIED_ON_CBA00, "CBAR_DIED_ON_CBA00", 1 );

DECLARE_MAP_EVENT_ACHIEVEMENT_HIDDEN( ACHIEVEMENT_CBAR_BADEND, "CBAR_BADEND", 1 );
DECLARE_MAP_EVENT_ACHIEVEMENT_HIDDEN( ACHIEVEMENT_CBAR_GOODEND, "CBAR_GOODEND", 1 );

DECLARE_MAP_EVENT_ACHIEVEMENT( ACHIEVEMENT_BG_COMPLETE_UNDAREAS, "BG_COMPLETE_UNDAREAS", 1 );
DECLARE_MAP_EVENT_ACHIEVEMENT( ACHIEVEMENT_BG_COMPLETE_CMBOUTPOST, "BG_COMPLETE_CMBOUTPOST", 1 );
DECLARE_MAP_EVENT_ACHIEVEMENT( ACHIEVEMENT_BG_COMPLETE_NEWFRIEND, "BG_COMPLETE_NEWFRIEND", 1 );
DECLARE_MAP_EVENT_ACHIEVEMENT( ACHIEVEMENT_BG_COMPLETE_TPP, "BG_COMPLETE_TPP", 1 );

DECLARE_MAP_EVENT_ACHIEVEMENT_HIDDEN( ACHIEVEMENT_BG_GOODEND, "BG_GOODEND", 1 );
DECLARE_MAP_EVENT_ACHIEVEMENT_HIDDEN( ACHIEVEMENT_BG_BADEND, "BG_BADEND", 1 );

#endif // GAME_DLL