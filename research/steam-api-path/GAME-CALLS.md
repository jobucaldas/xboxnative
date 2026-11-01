# Static game-to-Steamworks call sites

43 direct calls/tail jumps in the pinned IL2CPP files. Not a runtime sequence.
Shared/indirect/inlined calls and methods without unwind ranges are omitted.
See calls.json for tokens, target RVAs and native export-name references.

| Instruction RVA | Game method | Steamworks target |
|---|---|---|
| 0x3ebe50 | SteamAchievementsManager.TryUnlockAchievement | SteamUserStats.GetAchievement |
| 0x3ebe6d | SteamAchievementsManager.TryUnlockAchievement | SteamUserStats.SetAchievement |
| 0x3ec257 | SteamLeaderboardNew.OnDownloadResultsFriends | SteamUserStats.GetLeaderboardEntryCount |
| 0x3ec2d4 | SteamLeaderboardNew.OnDownloadResultsFriends | SteamUserStats.GetDownloadedLeaderboardEntry |
| 0x3ec51f | SteamLeaderboardNew.OnDownloadResultsGlobal | SteamUserStats.GetLeaderboardEntryCount |
| 0x3ec5d4 | SteamLeaderboardNew.OnDownloadResultsGlobal | SteamUserStats.GetDownloadedLeaderboardEntry |
| 0x3eded2 | SteamRichPresenceManager.OnDestroy | SteamFriends.ClearRichPresence |
| 0x3ee051 | SteamRichPresenceManager.SetKeyValue | SteamFriends.SetRichPresence |
| 0x3ee7c6 | SteamStatsManager.Init | SteamUserStats.RequestCurrentStats |
| 0x3ef6b1 | SteamStatsManager.SetCachedStats | SteamUserStats.SetStat |
| 0x3f02d5 | SteamTimelineManager.Init | SteamTimeline.SetTimelineGameMode |
| 0x3f03f0 | SteamTimelineManager.OnBossSpawned | SteamTimeline.AddTimelineEvent |
| 0x3f0b10 | SteamTimelineManager.OnFinalSwarmStarted | SteamTimeline.AddTimelineEvent |
| 0x3f0ba0 | SteamTimelineManager.OnMiniBoss | SteamTimeline.AddTimelineEvent |
| 0x3f0c20 | SteamTimelineManager.OnPlayerDied | SteamTimeline.AddTimelineEvent |
| 0x3f0f50 | SteamTimelineManager.OnSwarmStarted | SteamTimeline.AddTimelineEvent |
| 0x3f1154 | SteamUtility.LoadAvatar | SteamFriends.GetLargeFriendAvatar |
| 0x3f1162 | SteamUtility.LoadAvatar | SteamFriends.GetMediumFriendAvatar |
| 0x3f1170 | SteamUtility.LoadAvatar | SteamFriends.GetSmallFriendAvatar |
| 0x3f118c | SteamUtility.LoadAvatar | SteamUtils.GetImageSize |
| 0x3f11d1 | SteamUtility.LoadAvatar | SteamUtils.GetImageRGBA |
| 0x406ccb | MyAchievements.SyncToSteamAchievements | SteamUserStats.GetAchievement |
| 0x409088 | MyStats.SynToSteamStats | SteamUserStats.GetStat |
| 0x5285d1 | SteamLeaderboardsManagerNew.DownloadLeaderboardEntryLocal | SteamUser.GetSteamID |
| 0x5285fa | SteamLeaderboardsManagerNew.DownloadLeaderboardEntryLocal | SteamUserStats.DownloadLeaderboardEntriesForUsers |
| 0x52874f | SteamLeaderboardsManagerNew.FindLeaderboard | SteamUserStats.FindLeaderboard |
| 0x528e4e | SteamLeaderboardsManagerNew.LeaderboardScoreUploaded | SteamUserStats.GetLeaderboardName |
| 0x528ed8 | SteamLeaderboardsManagerNew.LeaderboardScoreUploaded | SteamUserStats.GetLeaderboardName |
| 0x528f2d | SteamLeaderboardsManagerNew.LeaderboardScoreUploaded | SteamUserStats.GetLeaderboardName |
| 0x529067 | SteamLeaderboardsManagerNew.LeaderboardScoresDownloadedLocal | SteamUserStats.GetLeaderboardName |
| 0x529ad6 | SteamLeaderboardsManagerNew.OnLeaderboardFindResult | SteamUserStats.GetLeaderboardName |
| 0x52ad6b | SteamManager.Load | Packsize.Test |
| 0x52adf5 | SteamManager.Load | NativeMethods.SteamAPI_RestartAppIfNecessary, SteamAPI.RestartAppIfNecessary |
| 0x52ae04 | SteamManager.Load | SteamAPI.Init |
| 0x52af60 | SteamManager.Load | SteamUser.GetSteamID |
| 0x52b28c | SteamManager.OnDestroy | SteamAPI.Shutdown |
| 0x52b35d | SteamManager.Update | SteamAPI.RunCallbacks |
| 0x52cfe3 | <>c__DisplayClass27_0.<DownloadLeaderboardEntries>b__0 | SteamUserStats.GetLeaderboardName |
| 0x56aabc | LanguageStartup.SetSystemLanguage | SteamApps.GetCurrentGameLanguage |
| 0x56aaf1 | LanguageStartup.SetSystemLanguage | SteamApps.GetCurrentGameLanguage |
| 0x56ad9f | LanguageStartup.SetSystemLanguage | SteamApps.GetCurrentGameLanguage |
| 0x56c394 | LeaderboardUiNew.Refresh | SteamFriends.GetFriendPersonaName |
| 0x56c417 | LeaderboardUiNew.Refresh | SteamUser.GetSteamID |
