local skynet = require "skynet"
local Event = require "Event"

local EventDefine = {
	LevelUP = 1,
}


local Player = {}
Player.level = 0
function Player:LevelUP() 
	self.level = self.level + 1
	Event.Dispatch(EventDefine.LevelUP, {level = self.level})
end


local Task = {}
function Task:Init()
	Event.AddListen(EventDefine.LevelUP, self, "_OnLevelUp")
end

function Task:_OnLevelUp(tArgs)
	skynet.error("Player Level:", tArgs.level)
end


local CMD = {}
function CMD._Event(...)
	Event.Receive(...)
end

skynet.start(function()
	skynet.dispatch("lua", function(session, source, cmd, ...)
		local f = CMD[cmd]
		if f then
			skynet.ret(skynet.pack(f(...)))
		end
	end)

	Task:Init()

	skynet.fork(function()
		for i = 1, 3 do
			skynet.sleep(100)
			Player:LevelUP()
		end
	end)
end)