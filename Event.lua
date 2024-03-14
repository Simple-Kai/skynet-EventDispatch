--[[
	EventCore会在服务gc释放该服务相关资源
	20231027优化：在一服务内，对同一事件的多次添加，只会加入eventcore一次
	20231030优化：防止分发事件阻塞导致bug
]]

local skynet = require "skynet"
local EventCore = require "eventcore"

local skypack = skynet.pack

local meta = {__mode = "k"}
local listener = {}
local prefix<const> = "_Event"

local Event = {}
function Event.RegisterInst()
	EventCore.register()
end

-- eventid in range[1, 10000]
function Event.AddListen(eventid, obj, funcName)
	assert(type(obj) == "table")
	assert(type(eventid) == "number")
	assert(type(funcName) == "string")
	assert(type(obj[funcName]) == "function")
	local obj2func = listener[eventid]
	if not obj2func then
		obj2func = setmetatable({}, meta)
		obj2func[obj] = funcName
		listener[eventid] = obj2func
		EventCore.add(eventid, skypack(prefix, eventid))--, funcName, euid))
	else
		obj2func[obj] = funcName
	end
end

-- 尽量将事件挂在生命周期较久的obj上
function Event.DelListener(object, eventid)
	if eventid then
		local obj2func = listener[eventid]
		for obj, funname in pairs(obj2func) do
			if object == obj then
				obj2func[obj] = nil
			end
		end
	else
		for _, obj2func in pairs(listener) do
			for obj, funname in pairs(obj2func) do
				if object == obj then
					obj2func[obj] = nil
				end
			end
		end
	end
end

function Event.Receive(eventid, param)--, funcName, euid, param)
	local obj2func = listener[eventid]
	local func
	for obj, funcName in pairs(obj2func) do
		func = obj[funcName]
		skynet.fork(func, obj, param, eventid)
	end
end

-- i3-6157U 2.40GHz   10000 msg cost: 20ms   	100000msg cost:400ms
function Event.Dispatch(eventid, param)
	EventCore.dispatch(eventid, skypack(param))
end

return Event
