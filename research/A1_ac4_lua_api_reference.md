# AC4 Lua `eventOperation` API reference

Derived from every `eventOperation:Method` call in the AC4 disc's script tree, cross-checked
against the string table of the decrypted `EBOOT.elf`.

`engine=Y` means the method name exists in the AC4 executable (a C-registered Lua function).
`ACFA uses` shows whether the successor game uses the same method (useful as a reference for
argument shapes and semantics).

Total distinct methods used by AC4: **104**

| method | AC4 uses | engine | ACFA uses |
|---|---|---|---|
| `GetCharaEventInterface` | 307 | Y | 895 |
| `SetEventHandleGroup` | 305 | Y | 292 |
| `SetEventPropertyGroup` | 137 | Y | 404 |
| `DeleteHandlGroup` | 106 | Y | 40 |
| `GetObjectEventInterface` | 89 | Y | 30 |
| `GetApRateOfAC` | 73 | Y | 21 |
| `RegistHandle` | 71 | Y | 82 |
| `SetEnableGravityGroup` | 56 | Y | 64 |
| `SetEventProperty` | 48 | Y | 131 |
| `SetRadarShapeIDGroup` | 43 | Y | 113 |
| `TerminateTutorialTask` | 42 | Y | 168 |
| `GetEventGroupNum` | 39 | Y | 0 |
| `PlaySE` | 33 | Y | 150 |
| `SetEnableAcFightMsg` | 27 | Y | 22 |
| `ShowRadarAllowGroup` | 27 | Y | 0 |
| `DisablePadAll` | 26 | Y | 72 |
| `EnablePadFlag` | 23 | Y | 60 |
| `GetEventGroupAlives` | 22 | Y | 114 |
| `GetGlobalValueManager` | 22 | Y | 21 |
| `SetTutorialTaskID` | 21 | Y | 84 |
| `ShowTutorialProgressWindow` | 21 | Y | 43 |
| `HideTutorialProgressWindow` | 20 | Y | 84 |
| `SetRadarShapeID` | 19 | Y | 4 |
| `ShowRadarRegion` | 19 | Y | 2 |
| `DeleteAllHandleWhichIs` | 18 | Y | 0 |
| `SetEnableTree` | 18 | Y | 0 |
| `SetEnableImmortal` | 16 | Y | 0 |
| `RegistRegionObserver` | 14 | Y | 15 |
| `SetMissionEndingFlowCAPEvent` | 14 | Y | 5 |
| `ShowFEObjectEmphasize` | 14 | Y | 14 |
| `BroadcastMessageToEventGroup` | 13 | Y | 0 |
| `HideEventGauge` | 13 | Y | 0 |
| `SetMissionMode` | 13 | Y | 0 |
| `KillEntity` | 11 | Y | 7 |
| `PushTutorlalOperation_Walk` | 11 | Y | 10 |
| `KillEventGroup` | 10 | Y | 56 |
| `SetEnableTreeGroup` | 9 | Y | 22 |
| `PushTutorlalOperation_QuickBoost` | 8 | Y | 1 |
| `SetEnableImmortalGroup` | 8 | Y | 4 |
| `SetEnableLockGroup` | 8 | Y | 0 |
| `SetEnablePA` | 8 | Y | 0 |
| `ShowRadarAllow` | 8 | Y | 0 |
| `EnableControllerShowingOnFE` | 7 | Y | 9 |
| `SetEnableGravity` | 7 | Y | 6 |
| `SetMissionCAPEvent` | 7 | Y | 2 |
| `DeleteAllHandle` | 6 | Y | 0 |
| `SetTutorialInputMode` | 6 | Y | 6 |
| `SetEnableAttack` | 5 | Y | 29 |
| `SetEnableMoveGroup` | 5 | Y | 88 |
| `ShowEventGauge` | 5 | Y | 0 |
| `SetEnableSfxPAState` | 4 | Y | 0 |
| `WatchEntityPairRelation` | 4 | Y | 4 |
| `AutoPilot` | 3 | Y | 0 |
| `EnablePAEnergySupply` | 3 | Y | 0 |
| `GetDistEntityPair` | 3 | Y | 0 |
| `PAAttenuaiton` | 3 | Y | 0 |
| `PushTutorlalOperation_Dash` | 3 | Y | 1 |
| `SetFadeScene` | 3 | Y | 0 |
| `ShowText` | 3 | Y | 3 |
| `DeleteAllHandleExceptFor` | 2 | Y | 0 |
| `OpenSubWindow` | 2 | Y | 2 |
| `PushTutorlalOperation_Shoot` | 2 | Y | 2 |
| `SetCameraShakeEntity` | 2 | Y | 6 |
| `SetEnableAttackGroup` | 2 | Y | 110 |
| `SetEnableEntityCollideGroup` | 2 | Y | 0 |
| `SetEnableLock` | 2 | Y | 0 |
| `SetEnableMapCollideGroup` | 2 | Y | 0 |
| `SetEnableMove` | 2 | Y | 32 |
| `SetMissionResult` | 2 | Y | 0 |
| `SetTimer` | 2 | Y | 3 |
| `ShiftRegionLevel` | 2 | Y | 6 |
| `AbortTextMessage` | 1 | Y | 9 |
| `AccomplishMission` | 1 | Y | 0 |
| `DetachLinkAction` | 1 | Y | 0 |
| `EnableDamageNoiseFilter` | 1 | Y | 0 |
| `EnablePadAll` | 1 | Y | 24 |
| `EnablePadTranslationFlag` | 1 | Y | 0 |
| `EnableWarningSound` | 1 | Y | 2 |
| `FixMissionResult` | 1 | Y | 0 |
| `GetMissionResult` | 1 | Y | 0 |
| `KillGroupInRegion` | 1 | Y | 6 |
| `ObserveRegionEventGroupInsideSomeone` | 1 | Y | 2 |
| `OpenPolyGWindow` | 1 | Y | 1 |
| `PlayBGM` | 1 | Y | 1 |
| `PushTutorlalOperation_AutoSighting` | 1 | Y | 1 |
| `PushTutorlalOperation_Boost` | 1 | Y | 1 |
| `PushTutorlalOperation_Jump` | 1 | Y | 1 |
| `PushTutorlalOperation_OverdBoost` | 1 | Y | 1 |
| `PushTutorlalOperation_Turn` | 1 | Y | 1 |
| `PushTutorlalOperation_View` | 1 | Y | 1 |
| `PushTutorlalOperation_ViewReset` | 1 | Y | 1 |
| `PushTutorlalOperation_WeaponChange` | 1 | Y | 1 |
| `SendMessageToEntity` | 1 | Y | 4 |
| `SetCameraShakeMyself` | 1 | Y | 1 |
| `SetDamageAltitude` | 1 | Y | 18 |
| `SetEnableAutoWeaponChange` | 1 | Y | 1 |
| `SetFEObjectEmphasize` | 1 | Y | 1 |
| `ShifCameraParameter` | 1 | Y | 0 |
| `ShiftEnvironment` | 1 | Y | 3 |
| `ShowRadarAllowEntityPtr` | 1 | Y | 1 |
| `ShowRadarAllowToRegion` | 1 | Y | 1 |
| `TerminateMission` | 1 | Y | 0 |
| `UserInput` | 1 | Y | 0 |
| `WatchShareScriptValue` | 1 | Y | 37 |

## Methods ACFA has that AC4 never calls

| method | engine in AC4 | ACFA uses | Lua-defined in ACFA |
|---|---|---|---|
| `AddMapECM` | - | 4 | - |
| `AttachEntityEventGroup` | - | 30 | - |
| `AttachRegion` | - | 58 | - |
| `CreateAcFightMessage` | - | 8 | - |
| `DetachConstraintGroup` | - | 24 | - |
| `DisablePadFlag` | Y | 12 | - |
| `GetEventGroupDestroys` | - | 2 | - |
| `GetSharingGlobalValueManager` | Y | 206 | - |
| `IsCooperationMission` | - | 7 | - |
| `IsEventGroupInRegionAll` | - | 15 | - |
| `IsEventGroupInRegionSomeone` | - | 34 | - |
| `MissionResultIsFixed` | - | 1 | - |
| `MoveRegionToEntityRelation` | - | 8 | - |
| `ObservePlayerMissionRegionOutOrTimeout` | - | 1 | yes |
| `PlayFullPolyG` | - | 1 | - |
| `SendMessageToGroup` | - | 4 | - |
| `SetAirResistance` | - | 6 | - |
| `SetCameraShake` | Y | 2 | - |
| `SetDecreaseDurability` | - | 1 | - |
| `SetDestroyedRanker` | - | 40 | - |
| `SetEnableGeneralMessage` | - | 28 | - |
| `SetEnablePAGroup` | - | 1 | - |
| `SetEnableRoute` | - | 12 | - |
| `SetEnableSfxPAStateGroup` | - | 1 | - |
| `SetEnableSuperOverBoostGroup` | - | 10 | - |
| `SetEnvTypeID` | - | 6 | - |
| `SetGravityPower` | - | 6 | - |
| `SetLightScatteringID` | - | 2 | - |
| `SetMissionModeAllEntity` | - | 5 | - |
| `SetMissionResult_Failure` | - | 2 | yes |
| `SetMissionResult_Success` | - | 1 | yes |
| `SetNoCountEnemyNum` | - | 2 | - |
| `ShowGeneralAcFightMessage` | - | 102 | - |
| `ShowTextMyselfFEOnly` | - | 1 | - |
| `StopOverBoost` | - | 42 | - |
