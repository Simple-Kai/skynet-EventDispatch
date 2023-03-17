require "skynet.manager"
local skynet = require "skynet"

local EventDispatcher = require "event"
local EEvent = require "EventDefine"

local CMD = {}

function CMD.register(iSource)
	EventDispatcher.register(iSource)
end

function CMD.add(iSource, iEventType, ...)
	EventDispatcher.add(iEventType, iSource, skynet.pack("event", ...))
end

function CMD.dispatch(iSource, iEventType, param)
	EventDispatcher.dispatch(iEventType, iSource, skynet.pack(param))
end

function CMD.clear(iSource)
	print("event sum:", EventDispatcher.sum(0))
	EventDispatcher.clear(iSource)
	print("event sum:", EventDispatcher.sum(0))
end

local function init()
	local n = 0
	for k, v in pairs(EEvent) do
		n = n + 1
	end
	EventDispatcher.init(n)
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
	skynet.register(".EventDispatcher")
end)

