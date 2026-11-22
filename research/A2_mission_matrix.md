# AC4 mission co-op hazard matrix

One row per story mission script (`common/event/mission/mXXX.lua`). Columns are counts of the
constructs that a 2-player conversion has to touch.

- **dead**: `ON_DEAD_ENTITY(ENTITY_ID__PLAYER,...)` handlers — each currently fails the mission on P1 death.
- **region**: region-out/timeout observers bound to `ENTITY_ID__PLAYER`.
- **ap**: `GetApRateOfAC` calls — AP-threshold messages/ranks, must average across players.
- **cam**: scripted/sub-window camera calls — need review for a second viewpoint.
- **grp**: existing group/event-group usage — reused for per-player groups.
- **chara**: number of per-mission character scripts; **allies** lists the non-player AC/vehicle scripts present.

| mission | size | dead | region | ap | cam | msg | diff | grp | chara | allies |
|---|---|---|---|---|---|---|---|---|---|---|
| m000 | 16503 | 1 | 1 | 2 | 6 | 15 | 5 | 21 | 5 | ac,dummy,joshua,vehicle |
| m010 | 13457 | 1 | 1 | 0 | 0 | 8 | 2 | 35 | 1 |  |
| m020 | 16166 | 1 | 5 | 0 | 12 | 28 | 4 | 26 | 3 | fortress,ship |
| m030 | 6938 | 1 | 1 | 1 | 2 | 7 | 0 | 2 | 2 | ac |
| m040 | 18521 | 1 | 1 | 0 | 0 | 25 | 4 | 22 | 5 | bomb,dummy,eye,fortress |
| m050 | 8778 | 1 | 1 | 11 | 2 | 14 | 5 | 2 | 3 | ac,joshua |
| m070 | 9669 | 1 | 0 | 0 | 0 | 8 | 1 | 11 | 1 |  |
| m080 | 12415 | 1 | 1 | 4 | 0 | 15 | 2 | 15 | 3 | ac,surprise |
| m090 | 10695 | 1 | 1 | 2 | 0 | 15 | 3 | 13 | 3 | ac,white_ac |
| m100 | 6268 | 1 | 1 | 0 | 2 | 7 | 0 | 4 | 2 | cannon |
| m110 | 12124 | 1 | 1 | 0 | 0 | 12 | 2 | 12 | 3 | flare_bomb,fortress |
| m120 | 11942 | 1 | 1 | 0 | 0 | 13 | 1 | 16 | 3 | battle_ship,ship |
| m130 | 6918 | 1 | 1 | 0 | 0 | 11 | 2 | 4 | 1 |  |
| m140 | 5814 | 1 | 1 | 0 | 0 | 5 | 1 | 8 | 2 | fortress |
| m150 | 13490 | 1 | 1 | 0 | 0 | 13 | 1 | 9 | 1 |  |
| m160 | 7247 | 1 | 1 | 0 | 0 | 10 | 1 | 11 | 1 |  |
| m170 | 5616 | 1 | 1 | 1 | 2 | 4 | 0 | 2 | 3 | ac,joshua |
| m180 | 16167 | 1 | 1 | 0 | 0 | 13 | 1 | 26 | 3 | submarine,tanker |
| m190 | 8568 | 1 | 1 | 1 | 0 | 6 | 2 | 12 | 2 | fortress |
| m200 | 21180 | 1 | 1 | 6 | 0 | 22 | 7 | 57 | 6 | ac,carrier,ene_ac,heli,transport |
| m210 | 13182 | 1 | 1 | 3 | 0 | 20 | 4 | 10 | 2 | ac |
| m220 | 7787 | 1 | 0 | 0 | 0 | 11 | 0 | 16 | 2 | ac |
| m230 | 4861 | 1 | 1 | 0 | 0 | 4 | 0 | 3 | 1 |  |
| m240 | 10005 | 1 | 1 | 0 | 6 | 9 | 2 | 20 | 3 | fortress,vehicle |
| m250 | 8009 | 1 | 1 | 0 | 0 | 11 | 1 | 4 | 1 |  |
| m260 | 13676 | 1 | 1 | 0 | 0 | 8 | 3 | 28 | 2 | fortress |
| m270 | 5083 | 1 | 1 | 0 | 6 | 5 | 1 | 3 | 1 |  |
| m280 | 12701 | 1 | 1 | 4 | 2 | 25 | 5 | 9 | 2 | ac |
| m290 | 9879 | 1 | 1 | 0 | 0 | 14 | 0 | 3 | 4 | carrier,cruiser,head_ship |
| m300 | 7985 | 1 | 1 | 0 | 0 | 10 | 1 | 10 | 3 | ac,cruiser |
| m310 | 4798 | 1 | 0 | 1 | 0 | 3 | 0 | 2 | 2 | ac |
| m320 | 23074 | 1 | 1 | 0 | 6 | 27 | 1 | 79 | 1 |  |
| m330 | 5347 | 1 | 1 | 0 | 0 | 8 | 1 | 3 | 1 |  |
| m350 | 8347 | 1 | 1 | 0 | 0 | 6 | 0 | 5 | 2 | boss |
| m360 | 7084 | 1 | 1 | 0 | 0 | 8 | 1 | 4 | 1 |  |
| m380 | 13020 | 1 | 1 | 6 | 0 | 19 | 4 | 28 | 3 | ac,fortress |
| m390 | 9738 | 1 | 1 | 0 | 0 | 10 | 0 | 18 | 4 | ac,tgsmssl,vehicle |
| m490 | 10178 | 2 | 1 | 3 | 0 | 6 | 3 | 6 | 4 | boss,ene,genius |
| m710 | 1964 | 1 | 1 | 0 | 0 | 0 | 0 | 2 | 1 |  |
| m711 | 1965 | 1 | 1 | 0 | 0 | 0 | 0 | 2 | 1 |  |
| m712 | 1970 | 1 | 1 | 0 | 0 | 0 | 0 | 2 | 1 |  |
| m713 | 1964 | 1 | 1 | 0 | 0 | 0 | 0 | 2 | 1 |  |
| m714 | 3060 | 1 | 1 | 0 | 0 | 0 | 0 | 4 | 1 |  |
| m715 | 3973 | 1 | 1 | 0 | 0 | 0 | 0 | 6 | 1 |  |
| m716 | 1969 | 1 | 1 | 0 | 0 | 0 | 0 | 2 | 1 |  |
| m717 | 1963 | 1 | 1 | 0 | 0 | 0 | 0 | 2 | 2 |  |
| m718 | 1967 | 1 | 1 | 0 | 0 | 0 | 0 | 2 | 1 |  |
| m719 | 3613 | 1 | 1 | 0 | 0 | 0 | 0 | 9 | 1 |  |

Total mission scripts: 48
