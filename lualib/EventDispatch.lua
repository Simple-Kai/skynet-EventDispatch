require "skynet.manager"
local skynet = require "skynet"

local EventCore = require "event"
local EEvent = require "EventDefine"

local CMD = {}

function CMD.register(iSource)
	EventCore.register(iSource)
end

function CMD.add(iSource, iEventType, ...)
	EventCore.add(iEventType, iSource, skynet.pack("event", ...))
end

function CMD.del(iSource, iEventType, ...)
	EventCore.del(iEventType, iSource, skynet.pack("event", ...))
end

function CMD.dispatch(iSource, iEventType, param)
	EventCore.dispatch(iEventType, iSource, skynet.pack(param))
end

function CMD.clear(iSource)
	print("event sum:", EventCore.sum())
	EventCore.clear(iSource)
	print("event sum:", EventCore.sum())
end

local function init()
	EventCore.init(100)
end

skynet.start(function()
	skynet.dispatch("lua", function(session, source, cmd, ...)
		assert(session == 0) -- don't call
		local f = assert(CMD[cmd])
		local ok, err = pcall(f, source, ...)
		if not ok then
			skynet.error(source, cmd, err)
		end
	end)
	init()
	skynet.register(".Event")
end)

