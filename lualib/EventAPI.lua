local skynet = require "skynet"

local E = {}

function E.register()
	skynet.send(".Event", "lua", "register")
end

function E.add(iEventType, ...)
	skynet.send(".Event", "lua", "add", iEventType, ...)
end

function E.del(iEventType, ...) -- 删除是有延迟的, 因为服务的消息队列
	skynet.send(".Event", "lua", "del", iEventType, ...)
end

function E.dispatch(iEventType, ...)
	skynet.send(".Event", "lua", "dispatch", iEventType, ...)
end

function E.clear()
	skynet.send(".Event", "lua", "clear")
end

setmetatable(E, {
	__gc = function()
		skynet.send(".Event", "lua", "clear")
	end,
})

return E