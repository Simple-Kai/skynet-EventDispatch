require "skynet.manager"
local skynet = require "skynet"

local EventAPI = require "EventAPI"
local EEvent = require "EventDefine"

local CMD = {}

local Event2Callback = {}

Event2Callback["testPrintTable"] = function(param)
	print(param.name)
end

function CMD.event(backCallName, param)
	print("multi server:", skynet.self(), backCallName, param)
	local f = Event2Callback[backCallName]
	if f then
		f(param)
	end
end


local function init()
	EventAPI.add(EEvent.TEST, "testPrintTable")
	skynet.timeout(100 + skynet.self() % 50, function()
		EventAPI.dispatch(EEvent.TEST, {name = "lisi"..skynet.self()})
	end)
end

skynet.start(function()
	skynet.dispatch("lua", function(session, source, cmd, ...)
		local f = CMD[cmd]
		if f then
			skynet.ret(skynet.pack(f(...)))
		else
			skynet.ret()
		end
	end)
	init()
end)