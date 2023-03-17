local skynet = require "skynet"

local E = {}

function E.register()
	skynet.send(".event", "lua", "register")
end

function E.add(iEventType, backCallName)
	skynet.send(".event", "lua", "add", iEventType, backCallName)
end

function E.dispatch(iEventType, param)
	skynet.send(".event", "lua", "dispatch", iEventType, param)
end

function E.clear()
	skynet.send(".event", "lua", "clear")
end

return E