## skynet-EventDispatch
在skynet中，事件分发的服务类型有两种，一种是会被创建多个实例的，另一种是只有一个实例的  
多实例服务:multi server， 单实例服务:single server  
事件分发实现规则限制：  
multi server分发事件一般不希望被其他服务收到，所以它仅分发到 自身和其他single server  
single server分发事件可以被所有的服务收到  
