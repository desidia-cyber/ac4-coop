# Build the co-op overlay: every mission's event bundle with the co-op Lua runtime appended to its
# mission script (research\28_HOST_DRIVEN_SESSION.md).
#
#   python coop_patch.py [--out <overlay root>] [m010 m020 ...]
#
# The runtime makes the HOST's mission script the only one that decides anything:
#   * host: every engine-dispatched handler (a function of this script whose name appears as a
#     string literal in it: the names handed to RegistHandle through ON_*/SetTimerWithHandler/...)
#     is reported to the port before it runs ("\1Cev" through print), with its arguments;
#   * join: the engine's own calls of those handlers do nothing; a 0.1 s timer polls the port for the
#     host's handler calls and replays them, in order, with the join's own eventOperation.
#   * solo (no co-op link): print("\1Crole") returns nothing and the runtime does not install.
# Nested calls (a handler calling another handler directly) pass through unchanged on both sides.
import argparse
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import bnd3tool as T  # noqa: E402

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DISC = os.path.join(ROOT, r"disc\PS3_GAME\USRDIR\bind\world")
OUT = os.path.join(ROOT, r"payload\overlay_coop")
KILL_TIMER = 492  # test aid only (AC4_COOP_TEST_AUTOKILL)
POLL_TIMER = 491  # no mission uses it (ids in use: 1..500 sparse; 491 is free in every script)

RUNTIME = r'''

-- ===== AC4 CO-OP RUNTIME (tools/coop_patch.py; research/28) =====
if __COOP_RT == nil then
	__COOP_RT = {}
	local R = __COOP_RT
	R.role = print( "\1Crole" )
	R.depth = 0
	R.orig = {}
	R.eo = nil
	R.skipped = 0
	local function ser( v )
		local t = type( v )
		if t == "number" then return "n" .. tostring( v )
		elseif t == "string" then return "s" .. v
		elseif t == "boolean" then if v then return "b1" else return "b0" end
		elseif t == "nil" then return "z"
		end
		return "u"
	end
	local function des( s )
		local c = string.sub( s, 1, 1 )
		local r = string.sub( s, 2 )
		if c == "n" then return tonumber( r )
		elseif c == "s" then return r
		elseif c == "b" then return r == "1"
		elseif c == "z" then return nil
		end
		return R.eo
	end
	-- Queries (Get*/Is*/Difficulty* functions, eventOperation:Get*/Is* methods, math.random) made while
	-- a handler runs: the host records their results with the event; the join's replay of the event
	-- gets the same results in the same order, so both scripts take the same branches whatever the
	-- local engine state (s28g: OnAllDestroyed read a different "KamikazeDeads" on each side).
	R.qorig = {}
	function R.isquery( k )
		return type( k ) == "string" and ( string.find( k, "^Get" ) or string.find( k, "^Is" ) or string.find( k, "^Difficulty" ) or string.find( k, "^Random" ) ) ~= nil
	end
	function R.qwrap( f, name )
		return function( ... )
			-- outside handlers, or inside another query (a Get* helper calling a Get* method): untouched
			if R.depth == 0 or R.inq then return f( unpack( arg ) ) end
			if R.rq ~= nil then
				R.rqi = R.rqi + 1
				local v = R.rq[ R.rqi ]
				if v ~= nil and v ~= "u" then return des( v ) end
			end
			R.inq = true
			local ok, r = pcall( f, unpack( arg ) )
			-- Co-op host: average GetApRateOfAC(PLAYER) across P1 (1000) & P2 (1001) per ACFA's rule,
			-- and take min distance for GetDistEntityPair(PLAYER, other) so either player triggers it.
			if ok and R.role == "host" and type( r ) == "number" then
				local p1 = ENTITY_ID__PLAYER or 1000
				if name == "GetApRateOfAC" and arg[ 2 ] == p1 then
					local ok2, r2 = pcall( f, arg[ 1 ], 1001 )
					if ok2 and type( r2 ) == "number" and r2 >= 0 and r2 <= 1.01 then
						r = ( r + r2 ) * 0.5
					end
				elseif name == "GetDistEntityPair" then
					if arg[ 2 ] == p1 then
						local ok2, r2 = pcall( f, arg[ 1 ], 1001, arg[ 3 ] )
						if ok2 and type( r2 ) == "number" and r2 >= 0 and r2 < r then r = r2 end
					elseif arg[ 3 ] == p1 then
						local ok2, r2 = pcall( f, arg[ 1 ], arg[ 2 ], 1001 )
						if ok2 and type( r2 ) == "number" and r2 >= 0 and r2 < r then r = r2 end
					end
				end
			end
			R.inq = false
			if not ok then error( r, 0 ) end
			if R.q ~= nil then table.insert( R.q, ser( r ) ) end
			return r
		end
	end
	function R.wrapq_globals()
		local names = {}
		for k, v in pairs( _G ) do
			if type( v ) == "function" and R.isquery( k ) and R.qorig[ k ] == nil then table.insert( names, k ) end
		end
		for _, k in ipairs( names ) do
			R.qorig[ k ] = _G[ k ]
			_G[ k ] = R.qwrap( _G[ k ], k )
		end
		if type( math ) == "table" and type( math.random ) == "function" and R.qorig[ "math.random" ] == nil then
			R.qorig[ "math.random" ] = math.random
			math.random = R.qwrap( math.random, "math.random" )
		end
		return table.getn( names )
	end
	-- eventOperation methods: through the userdata's metatable (__index table or function).
	-- In addition to recording query results, mirror player-targeted environmental effects (PA disable,
	-- PA SFX, altitude damage, PA energy supply) from ENTITY_ID__PLAYER (1000) onto entity 1001 so
	-- the second player obeys the same mission hazards as the first (m040, m130, m200, m360).
	local P2_MIRROR = {
		SetEnablePA = true,
		SetEnableSfxPAState = true,
		SetDamageAltitude = true,
		EnablePAEnergySupply = true,
	}
	function R.p2mwrap( f )
		return function( self, id, a3, a4, a5 )
			local r = f( self, id, a3, a4, a5 )
			if id == ( ENTITY_ID__PLAYER or 1000 ) then
				f( self, 1001, a3, a4, a5 )
			end
			return r
		end
	end
	function R.wrapq_methods( eo )
		if R.methods_done or type( eo ) ~= "userdata" then return end
		R.methods_done = true
		local ok, mt = pcall( getmetatable, eo )
		if not ok or type( mt ) ~= "table" then return end
		local idx = mt.__index
		if type( idx ) == "table" then
			local n = 0
			for k, v in pairs( idx ) do
				if type( v ) == "function" then
					if R.isquery( k ) then idx[ k ] = R.qwrap( v, k ); n = n + 1
					elseif P2_MIRROR[ k ] then idx[ k ] = R.p2mwrap( v ) end
				end
			end
			print( "\1Clog\2queries: " .. n .. " eventOperation method(s) (table)" )
		elseif type( idx ) == "function" then
			local cache = {}
			mt.__index = function( t, k )
				local v = idx( t, k )
				if type( v ) == "function" then
					if R.isquery( k ) then
						if cache[ k ] == nil then cache[ k ] = R.qwrap( v, k ) end
						return cache[ k ]
					elseif P2_MIRROR[ k ] then
						if cache[ k ] == nil then cache[ k ] = R.p2mwrap( v ) end
						return cache[ k ]
					end
				end
				return v
			end
			print( "\1Clog\2queries: eventOperation methods through __index function" )
		end
	end
	local function split( s, sep )
		local out = {}
		if s == nil or s == "" then return out end
		local i = 1
		while true do
			local j = string.find( s, sep, i, 1 )
			if j == nil then table.insert( out, string.sub( s, i ) ); break end
			table.insert( out, string.sub( s, i, j - 1 ) )
			i = j + 1
		end
		return out
	end
	function R.wrap( name )
		local f = _G[ name ]
		if type( f ) ~= "function" or R.orig[ name ] ~= nil then return end
		R.orig[ name ] = f
		_G[ name ] = function( ... )
			if R.depth > 0 then
				return f( unpack( arg ) )
			end
			if type( arg[ 1 ] ) == "userdata" then R.eo = arg[ 1 ]; R.wrapq_methods( arg[ 1 ] ) end
			if R.role == "join" then
				R.skipped = R.skipped + 1
				if R.skipped <= 200 then print( "\1Clog\2join: local " .. name .. " left to the host" ) end
				return
			end
			local s = name
			for i = 1, arg.n do s = s .. "\2" .. ser( arg[ i ] ) end
			R.depth = 1
			R.q = {}
			print( "\1Chbeg" )
			local r = { pcall( f, unpack( arg ) ) }
			print( "\1Chend" )
			R.depth = 0
			-- published after the run: the query results it saw go with it ("\3" then "\2"-separated)
			print( "\1Cev\2" .. s .. "\3" .. table.concat( R.q, "\2" ) )
			R.q = nil
			if not r[ 1 ] then error( r[ 2 ], 0 ) end
			table.remove( r, 1 )
			return unpack( r )
		end
	end
	function __CoopPoll( eventOperation )
		R.eo = eventOperation
		R.wrapq_methods( eventOperation )
		for k = 1, 64 do
			local s = print( "\1Cpoll" )
			if s == nil then break end
			-- Native C++ player-object event originated by an unpatched chara script on host.
			if string.sub( s, 1, 3 ) == "\1N\2" then
				print( "\1Cexec\2" .. s )
			else
				local cut = string.find( s, "\3", 1, 1 )
				local head, tail = s, ""
				if cut ~= nil then head = string.sub( s, 1, cut - 1 ); tail = string.sub( s, cut + 1 ) end
				local fields = split( head, "\2" )
				local name = fields[ 1 ]
				local f = R.orig[ name ]
				if f == nil then f = _G[ name ] end
				if type( f ) == "function" then
					local a = { n = table.getn( fields ) - 1 }
					for m = 2, table.getn( fields ) do a[ m - 1 ] = des( fields[ m ] ) end
					R.rq = split( tail, "\2" )
					R.rqi = 0
					R.depth = 1
					print( "\1Chbeg" )
					local ok, err = pcall( f, unpack( a ) )
					print( "\1Chend" )
					R.depth = 0
					if R.rqi ~= table.getn( R.rq ) then
						print( "\1Clog\2replay " .. tostring( name ) .. ": " .. R.rqi .. " query(ies) here, " .. table.getn( R.rq ) .. " on the host" )
					end
					R.rq = nil
					if not ok then print( "\1Clog\2replay " .. tostring( name ) .. " failed: " .. tostring( err ) ) end
				else
					print( "\1Clog\2replay: no handler " .. tostring( name ) )
				end
			end
		end
		SetTimerWithHandler( eventOperation, %(timer)d, 0.1, "__CoopPoll" )
	end
	function R.install_initial()
		local init = _G[ "Initial" ]
		if type( init ) ~= "function" or R.init_done then return end
		R.init_done = true
		_G[ "Initial" ] = function( eventOperation )
			-- One copy of this script runs per player body, each with its own event manager and
			-- eventOperation, and both are initialised: the event-group observations (OnAllDestroyed)
			-- only fire through the second manager (run s28s). The port records which per-player object
			-- each copy drives and sends the player actions aimed at the second one (ending flow, mission
			-- CAP, mission mode, immortal) to the first, the local player (research/28, runs s28h-s28s).
			local who = print( "\1Cwho", eventOperation )
			-- The script watches ENTITY_ID__PLAYER (1000) for death and for leaving the mission area.
			-- The second player's body is entity 1001 (the port renumbers it, research/28 s28y), so
			-- on the host, the second copy also watches 1001 with the same callbacks: the join's death
			-- or region-out ends the mission for both, as the player's own does (it is published and
			-- replayed like any handler). Wrapped here, before Initial registers anything, so the
			-- script's own handle groups apply to the extra watches too.
			if R.role == "host" and who == "2" and not R.p2watch then
				R.p2watch = true
				local P2 = 1001
				-- (a function per name: Lua 5.0 closures made in a for loop do not keep the loop
				-- variable, s28z4; and no "..." in expressions, 5.0 has only arg)
				local function p2wrap2( name )
					local f = _G[ name ]
					if type( f ) ~= "function" then return end
					_G[ name ] = function( eo, id, a3, a4, a5 )
						f( eo, id, a3, a4, a5 )
						if id == ( ENTITY_ID__PLAYER or 1000 ) then
							if name == "ON_DEAD_ENTITY" then R.p1_dead_watched = true end
							print( "\1Clog\2second player watched: " .. name .. " -> " .. tostring( a3 ) )
							f( eo, P2, a3, a4, a5 )
						end
					end
				end
				local function p2wrap3( name )
					local f = _G[ name ]
					if type( f ) ~= "function" then return end
					_G[ name ] = function( eo, reg, id, a4, a5 )
						f( eo, reg, id, a4, a5 )
						if id == ( ENTITY_ID__PLAYER or 1000 ) then
							print( "\1Clog\2second player watched: " .. name .. " -> " .. tostring( a4 ) )
							f( eo, reg, P2, a4, a5 )
						end
					end
				end
				p2wrap2( "ON_DEAD_ENTITY" )
				p2wrap2( "ObserveMissionRegionOut" )
				p2wrap2( "ObserveMissionRegionTimeout" )
				p2wrap2( "ObserveNoticeRegionLostInfinity" )
				p2wrap2( "WatchEntityDistNear" )
				p2wrap2( "WatchEntityDistNearInfinity" )
				p2wrap2( "SendUserMessageToEntity" )
				p2wrap3( "ObserveRegionExist" )
				p2wrap3( "ObserveRegionIn" )
				p2wrap3( "ObserveRegionOut" )
			end
			init( eventOperation )
			R.eo = eventOperation
			-- For missions whose mXXX.lua left ON_DEAD_ENTITY(PLAYER) commented out (m080/m090/m200/
			-- m210/m220/m280/m380, delegating death to mXXX_player.lua): watch both players as a safety
			-- net on the host (p2wrap2 automatically mirrors ENTITY_ID__PLAYER -> 1001). Because
			-- mXXX_player.lua fires synchronously before ON_DEAD_ENTITY dispatches, has_result is
			-- already set if the chara script handled it; otherwise fire MissionFailure_PlayerIsDead
			-- immediately (do NOT rely on a timer on the dead body's eventOperation).
			if R.role == "host" and who == "2" and not R.p1_dead_watched and type( ON_DEAD_ENTITY ) == "function" then
				print( "\1Clog\2fallback death watch installed for P1/P2 (mXXX.lua has no ON_DEAD_ENTITY)" )
				ON_DEAD_ENTITY( eventOperation, ENTITY_ID__PLAYER or 1000, "__CoopFallbackDead" )
			end
			print( "\1Clog\2runtime ready: role " .. tostring( R.role ) .. ", player copy " .. tostring( who ) )
			if R.role == "join" and who ~= "2" then
				SetTimerWithHandler( eventOperation, %(timer)d, 0.1, "__CoopPoll" )
			end
			-- test aid (host, AC4_COOP_TEST_AUTOKILL=<seconds>): every enemy group dies after the delay,
			-- so the end of a mission can be exercised without playing it
			local ak = tonumber( print( "\1Cflag\2autokill" ) or "" )
			if R.role == "host" and ak ~= nil and who ~= "2" then
				SetTimerWithHandler( eventOperation, %(killtimer)d, ak, "__CoopAutoKill" )
			end
			-- test aid (host, AC4_COOP_TEST_KILLP2=<seconds>, not with autokill: same timer id): the
			-- second player's body is killed once, to exercise that player's death path
			local kp = tonumber( print( "\1Cflag\2killp2" ) or "" )
			if R.role == "host" and kp ~= nil and ak == nil and who ~= "2" then
				SetTimerWithHandler( eventOperation, %(killtimer)d, kp, "__CoopKillP2" )
			end
			-- test aid (host, AC4_COOP_TEST_KILLP1=<seconds>): kills P1 (host) once to test host-death sync
			local kp1 = tonumber( print( "\1Cflag\2killp1" ) or "" )
			if R.role == "host" and kp1 ~= nil and ak == nil and kp == nil and who ~= "2" then
				SetTimerWithHandler( eventOperation, %(killtimer)d, kp1, "__CoopKillP1" )
			end
			-- test aid (host, autotest smoke): completes mission cleanly after delay to verify full loop
			local ats = tonumber( print( "\1Cflag\2autotest_succeed" ) or "" )
			if R.role == "host" and ats ~= nil and ak == nil and kp == nil and kp1 == nil and who ~= "2" then
				SetTimerWithHandler( eventOperation, %(killtimer)d, ats, "__CoopTestSucceed" )
			end
		end
	end
	function __CoopTestSucceed( eventOperation )
		print( "\1Clog\2autotest: firing mission success" )
		if type( SetMissionResult_Success ) == "function" then
			SetMissionResult_Success( eventOperation, 0 )
		else
			eventOperation:SetMissionResult( 1 )
		end
		if type( OnMissionSuccess ) == "function" then
			OnMissionSuccess( eventOperation )
		elseif type( MissionSuccess ) == "function" then
			MissionSuccess( eventOperation, 0 )
		else
			eventOperation:SetMissionEndingFlowCAPEvent( 1 )
		end
	end
	function __CoopFallbackDead( eventOperation )
		if R.fallback_fired then return end
		if print( "\1Cflag\2has_result" ) ~= nil then
			print( "\1Clog\2fallback death: mission result already set by chara script" )
			return
		end
		R.fallback_fired = true
		__CoopFallbackFail( eventOperation )
	end
	function __CoopFallbackFail( eventOperation )
		print( "\1Clog\2fallback death: firing MissionFailure_PlayerIsDead" )
		if type( MissionFailure_PlayerIsDead ) == "function" then
			MissionFailure_PlayerIsDead( eventOperation, 0 )
		end
	end
	function __CoopKillP1( eventOperation )
		print( "\1Clog\2killp1: KillEntity 1000" )
		eventOperation:KillEntity( ENTITY_ID__PLAYER or 1000 )
	end
	function __CoopKillP2( eventOperation )
		print( "\1Clog\2killp2: KillEntity 1001" )
		eventOperation:KillEntity( 1001 )
	end
	function __CoopAutoKill( eventOperation )
		for k, v in pairs( _G ) do
			if type( k ) == "string" and type( v ) == "number" and string.find( k, "_GP_ALLENEMY$" ) then
				print( "\1Clog\2autokill: KillEventGroup " .. k )
				eventOperation:KillEventGroup( v )
			end
		end
		-- later waves of the same groups appear after the first kill (s28r): keep killing every 5 s,
		-- until a success handler has run (killing on through the ending held it on black, s28t/s28u)
		-- (the port stops answering "autokill" once a success handler ran, in either copy of the script)
		if print( "\1Cflag\2autokill" ) ~= nil then
			SetTimerWithHandler( eventOperation, %(killtimer)d, 5, "__CoopAutoKill" )
		end
	end
end
if __COOP_RT.role == "host" or __COOP_RT.role == "join" then
	for _, n in ipairs( { %(names)s } ) do __COOP_RT.wrap( n ) end
	__COOP_RT.wrap( "__CoopFallbackFail" )
	__COOP_RT.wrap( "__CoopTestSucceed" )
	__COOP_RT.install_initial()
	print( "\1Clog\2queries: " .. __COOP_RT.wrapq_globals() .. " global function(s)" )
end
-- ===== end of the co-op runtime =====
'''

SUB_RUNTIME = r'''

-- ===== AC4 CO-OP SUB-SCRIPT RUNTIME (tools/coop_patch.py) =====
if __COOP_SRT == nil then
	__COOP_SRT = {}
	local R = __COOP_SRT
	R.role = print( "\1Crole" )
	R.depth = 0
	R.orig = {}
	R.qorig = {}
	R.eo = nil
	R.sid = nil
	local function ser( v )
		local t = type( v )
		if t == "number" then return "n" .. tostring( v )
		elseif t == "string" then return "s" .. v
		elseif t == "boolean" then if v then return "b1" else return "b0" end
		elseif t == "nil" then return "z"
		end
		return "u"
	end
	local function des( s )
		local c = string.sub( s, 1, 1 )
		local r = string.sub( s, 2 )
		if c == "n" then return tonumber( r )
		elseif c == "s" then return r
		elseif c == "b" then return r == "1"
		elseif c == "z" then return nil
		end
		return R.eo
	end
	local function split( s, sep )
		local out = {}
		if s == nil or s == "" then return out end
		local i = 1
		while true do
			local j = string.find( s, sep, i, 1 )
			if j == nil then table.insert( out, string.sub( s, i ) ); break end
			table.insert( out, string.sub( s, i, j - 1 ) )
			i = j + 1
		end
		return out
	end
	function R.qwrap( f )
		return function( ... )
			if R.depth == 0 or R.inq then return f( unpack( arg ) ) end
			if R.rq ~= nil then
				R.rqi = R.rqi + 1
				local v = R.rq[ R.rqi ]
				if v ~= nil and v ~= "u" then return des( v ) end
			end
			R.inq = true
			local ok, r = pcall( f, unpack( arg ) )
			R.inq = false
			if not ok then error( r, 0 ) end
			if R.q ~= nil then table.insert( R.q, ser( r ) ) end
			return r
		end
	end
	function R.wrapq_rng()
		for _, k in ipairs( { "RandomRangeInt", "RandomRangeFloat", "DifficultyIsNormal" } ) do
			if type( _G[ k ] ) == "function" and R.qorig[ k ] == nil then
				R.qorig[ k ] = _G[ k ]
				_G[ k ] = R.qwrap( _G[ k ] )
			end
		end
		if type( math ) == "table" and type( math.random ) == "function" and R.qorig[ "math.random" ] == nil then
			R.qorig[ "math.random" ] = math.random
			math.random = R.qwrap( math.random )
		end
	end
	function R.wrap( name )
		local f = _G[ name ]
		if type( f ) ~= "function" or R.orig[ name ] ~= nil then return end
		R.orig[ name ] = f
		_G[ name ] = function( ... )
			if R.depth > 0 then return f( unpack( arg ) ) end
			if type( arg[ 1 ] ) == "userdata" then R.eo = arg[ 1 ] end
			if R.role == "join" then return end
			local s = name
			for i = 1, arg.n do s = s .. "\2" .. ser( arg[ i ] ) end
			R.depth = 1
			R.q = {}
			print( "\1Chbeg" )
			local r = { pcall( f, unpack( arg ) ) }
			print( "\1Chend" )
			R.depth = 0
			if R.sid ~= nil then
				print( "\1Csev\2" .. R.sid .. "\2" .. s .. "\3" .. table.concat( R.q, "\2" ) )
			end
			R.q = nil
			if not r[ 1 ] then error( r[ 2 ], 0 ) end
			table.remove( r, 1 )
			return unpack( r )
		end
	end
	function __CoopSubPoll( eventOperation )
		R.eo = eventOperation
		if R.poll_cmd ~= nil then
			for k = 1, 64 do
				local s = print( R.poll_cmd )
				if s == nil then break end
				local cut = string.find( s, "\3", 1, 1 )
				local head, tail = s, ""
				if cut ~= nil then head = string.sub( s, 1, cut - 1 ); tail = string.sub( s, cut + 1 ) end
				local fields = split( head, "\2" )
				local name = fields[ 1 ]
				local f = R.orig[ name ]
				if f == nil then f = _G[ name ] end
				if type( f ) == "function" then
					local a = { n = table.getn( fields ) - 1 }
					for m = 2, table.getn( fields ) do a[ m - 1 ] = des( fields[ m ] ) end
					R.rq = split( tail, "\2" )
					R.rqi = 0
					R.depth = 1
					print( "\1Chbeg" )
					local ok, err = pcall( f, unpack( a ) )
					print( "\1Chend" )
					R.depth = 0
					R.rq = nil
					if not ok then print( "\1Clog\2sub replay " .. tostring( R.sid ) .. ":" .. tostring( name ) .. " failed: " .. tostring( err ) ) end
				end
			end
		end
		SetTimerWithHandler( eventOperation, %(timer)d, 0.15, "__CoopSubPoll" )
	end
	function R.install_initial()
		local init = _G[ "Initial" ]
		if type( init ) ~= "function" or R.init_done then return end
		R.init_done = true
		_G[ "Initial" ] = function( eventOperation )
			R.eo = eventOperation
			R.sid = print( "\1Csid\2%(script)s" )
			if R.sid ~= nil then R.poll_cmd = "\1Cspoll\2" .. R.sid end
			init( eventOperation )
			if R.role == "join" then
				SetTimerWithHandler( eventOperation, %(timer)d, 0.15, "__CoopSubPoll" )
			end
		end
	end
end
if __COOP_SRT.role == "host" or __COOP_SRT.role == "join" then
	for _, n in ipairs( { %(names)s } ) do __COOP_SRT.wrap( n ) end
	__COOP_SRT.install_initial()
	__COOP_SRT.wrapq_rng()
end
-- ===== end of the co-op sub-script runtime =====
'''


def handlers(src):
    """Functions defined in this script whose names also appear as string literals in it."""
    text = src.decode("cp932", errors="replace")
    # strip comments so commented-out registrations do not count
    text = re.sub(r"--\[\[.*?\]\]", "", text, flags=re.S)
    text = re.sub(r"--[^\n]*", "", text)
    defined = set(re.findall(r"^\s*function\s+([A-Za-z_][A-Za-z0-9_]*)\s*\(", text, flags=re.M))
    literal = set(re.findall(r'"([A-Za-z_][A-Za-z0-9_]*)"', text))
    return sorted((defined & literal) - {"Initial"})


def patch(mid, out_root):
    src = os.path.join(DISC, mid + "_event.000")
    orig, entries = T.parse(src)
    for e in entries:
        if re.search(r"event/mission/m\d{3}\.lua$", e["name"].replace("\\", "/")):  # m051 carries m050.lua
            d = bytes(e["data"])
            if b"AC4 CO-OP RUNTIME" in d:
                raise SystemExit("%s is already patched (the disc copy must be pristine)" % mid)
            names = handlers(d)
            code = RUNTIME % {"timer": POLL_TIMER, "killtimer": KILL_TIMER, "names": ", ".join('"%s"' % n for n in names)}
            e["data"] = bytearray(d + code.replace("\n", "\r\n").encode("ascii"))
            break
    else:
        print(mid, "mission script not found; skipped")
        return None
    dst_dir = os.path.join(out_root, r"PS3_GAME\USRDIR\bind\world")
    os.makedirs(dst_dir, exist_ok=True)
    dst = os.path.join(dst_dir, mid + "_event.000")
    with open(dst, "wb") as f:
        f.write(T.build(orig, entries))
    # verify: the rebuilt bundle parses and carries the runtime
    _, chk = T.parse(dst)
    ok = any(b"AC4 CO-OP RUNTIME" in bytes(c["data"]) for c in chk)
    print("%s: %d handler(s) %s -> %s" % (mid, len(names), "OK" if ok else "VERIFY FAILED", dst))
    return names


def patch_mission(mid, out_root):
    """Patch P1-hardcoded AI / boss hooks inside mXXX_mission.000 (solo-safe, gated on print('\\1Crole'))."""
    src = os.path.join(DISC, mid + "_mission.000")
    if not os.path.exists(src):
        return 0
    orig, entries = T.parse(src)
    edits = 0
    for e in entries:
        name = e["name"].replace("\\", "/")
        if not name.endswith(".lua"):
            continue
        raw = bytes(e["data"])
        txt = raw.decode("cp932", errors="surrogateescape")
        new_txt = txt
        if "/airesource/script/" in name:
            # m020/m040/m140: drop P1-only force target in co-op so radar targeting engages both players.
            new_txt, n1 = re.subn(
                r'Targetting_SetForceTarget\s*\(\s*([^,]+?)\s*,\s*ENTITY_ID__PLAYER\s*\)\s*;?',
                r'if print("\\1Crole") == nil then Targetting_SetForceTarget(\1, ENTITY_ID__PLAYER); end',
                new_txt,
            )
            # m040_fortress_eye: treat P2 (1001) as a player target too.
            new_txt, n2 = re.subn(
                r'ENTITY_ID__PLAYER\s*==\s*GetEntityIdOfTarget\s*\(\s*([^)]+?)\s*\)',
                r'(ENTITY_ID__PLAYER == GetEntityIdOfTarget(\1) or (print("\\1Crole") ~= nil and 1001 == GetEntityIdOfTarget(\1)))',
                new_txt,
            )
            # m120/m130/m240: match altitude of whichever player (1000 or 1001) this enemy is targeting.
            new_txt, n3 = re.subn(
                r'GetEntityAltitude\s*\(\s*([^,]+?)\s*,\s*ENTITY_ID__PLAYER\s*\)',
                r'GetEntityAltitude(\1, (print("\\1Crole") ~= nil and type(GetEntityIdOfTarget) == "function" and GetEntityIdOfTarget(\1) == 1001 and 1001 or ENTITY_ID__PLAYER))',
                new_txt,
            )
            edits += n1 + n2 + n3
        elif "/event/mission/obj/m350_" in name:
            # m350_obj/wall/henden: also watch P2 (1001) so Joiner triggers boss terrain destruction too.
            new_txt, n4 = re.subn(
                r'(ctrl:WatchM350PlayerBossEntity\s*\(\s*)ENTITY_ID__PLAYER(\s*,[^;]+;)',
                r'\1ENTITY_ID__PLAYER\2\r\n\t\tif print("\\1Crole") ~= nil then \g<1>1001\2 end',
                new_txt,
                flags=re.S,
            )
            edits += n4
        # Inject lean host-authoritative event-replay runtime ONLY into non-player chara/*.lua and
        # obj/*.lua scripts that perform animations/CAP/fades/gauges/RNG from non-message triggers
        # (keeping the shared PS3 DLLua heap lean; pure user-message scripts already sync via mXXX.lua).
        is_sub = (
            ("/event/mission/chara/" in name and not name.endswith("_player.lua"))
            or ("/event/mission/obj/" in name)
        )
        if is_sub and "AC4 CO-OP SUB-SCRIPT RUNTIME" not in new_txt:
            stripped = re.sub(r"--\[\[.*?\]\]", "", new_txt, flags=re.S)
            stripped = re.sub(r"--[^\n]*", "", stripped)
            has_action = any(k in stripped for k in (
                "SetAnime", "PauseAnime", "RestartAnime", "SetCapEvent", "SetFadeScene",
                "ShowEventGauge", "SetCameraShake", "SetEnableTree", "KillConstraintEntity",
                "RandomRange", "math.random",
            ))
            has_async_trigger = any(k in stripped for k in (
                "RandomRange", "math.random", "ON_PARTS_DURABILITY_REMAINDER_",
                "ON_DECREASE_DURABILITY_MYSELF", "ON_OVER_PROGRESS_CURR_ANIME",
                "ON_END_OF_CURRENT_ANIME", "ObserveRegion", "WatchSharedGlobalValue",
            ))
            sub_names = handlers(raw)
            has_init = re.search(r"^\s*function\s+Initial\s*\(", stripped, flags=re.M) is not None
            if has_action and has_async_trigger and sub_names and has_init:
                script_id = os.path.basename(name)
                sub_code = SUB_RUNTIME % {
                    "timer": POLL_TIMER,
                    "script": script_id,
                    "names": ", ".join('"%s"' % n for n in sub_names),
                }
                new_txt = new_txt + sub_code.replace("\n", "\r\n")
                edits += 1
        if new_txt != txt:
            e["data"] = bytearray(new_txt.encode("cp932", errors="surrogateescape"))
    if edits == 0:
        return 0
    dst_dir = os.path.join(out_root, r"PS3_GAME\USRDIR\bind\world")
    os.makedirs(dst_dir, exist_ok=True)
    dst = os.path.join(dst_dir, mid + "_mission.000")
    with open(dst, "wb") as f:
        f.write(T.build(orig, entries))
    print("%s_mission: %d co-op AI/obj edit(s) -> %s" % (mid, edits, dst))
    return edits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=OUT)
    ap.add_argument("missions", nargs="*")
    a = ap.parse_args()
    mids = a.missions or sorted(f[:4] for f in os.listdir(DISC) if re.match(r"m\d{3}_event\.000$", f))
    for mid in mids:
        patch(mid, a.out)
        patch_mission(mid, a.out)


if __name__ == "__main__":
    main()
