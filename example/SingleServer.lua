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
	print("single server:", backCallName, param)
	local f = Event2Callback[backCallName]
	if f then
		f(param)
	end
end


local function init()
	EventAPI.register()
	EventAPI.add(EEvent.TEST, "testPrintTable")
	skynet.timeout(200 + skynet.self() % 50, function()
		local stm = skynet.now()
		EventAPI.dispatch(EEvent.TEST, {name = "zhangsan"})
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