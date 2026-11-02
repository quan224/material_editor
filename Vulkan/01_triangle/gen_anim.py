# -*- coding: utf-8 -*-
# 生成"代码运行动画.html"：读 vulkan.cpp 全文嵌入 + 事件表映射
import io, json, re

src = io.open('vulkan.cpp', encoding='utf-8').read()
lines = src.split('\n')

# 事件表：(匹配子串, 事件dict)。执行序里逐行扫描，命中则附加事件。
# 事件: w=写watch(名:真实值) use=[出示] kill=销毁 mech=机制场景 note=解说
#       field=[结构体字段名, 值, 来源]（createInfo 填充动画）
E = []
def ev(sub, **kw):
    E.append((sub, kw))

ev('window = glfwCreateWindow', w='window', mech='win',
   note='GLFW 开窗。window 是 GLFW 对象不是 Vulkan 的')
ev('vkCreateInstance(&create_info', w='instance', mech='inst',
   note='交表（名片+扩展+验证层）→ loader 装驱动发证 instance')
# create_instance 的填表行
ev('appInfo.pApplicationName', field=['pApplicationName','"Hello Triangle"','常量'])
ev('auto extensions = getRequiredExtensions', mech='extq',
   note='数据来源①：问 GLFW 开窗要什么扩展 → [VK_KHR_surface, VK_KHR_win32_surface, debug_utils]')
ev('create_info.enabledExtensionCount = static_cast<uint32_t>(extensions.size())', field=['enabledExtensionCount','3','← getRequiredExtensions()'])
ev('create_info.ppEnabledExtensionNames = extensions.data()', field=['ppEnabledExtensionNames','数组指针','← extensions.data()'])
ev('create_info.enabledLayerCount = validationLayers.size()', field=['enabledLayerCount','1','← validationLayers'])
ev('CreateDebugUtilsMessengerEXT(instance', w='debugMessenger', use=['instance'], mech='msg',
   note='出示 instance 按名查址（扩展函数）→ 验证层发证')
ev('createInfo.pfnUserCallback = debugCallback', field=['pfnUserCallback','debugCallback','← 我们的函数'])
ev('glfwCreateWindowSurface(instance, window', w='surface', use=['instance','window'], mech='surf',
   note='出示 instance+window → OS 侧登记呈现接头')
ev('vkEnumeratePhysicalDevices(instance, &deviceCount, nullptr)', mech='pick0', use=['instance'],
   note='两段式第一问：几张卡？→ 实测 1')
ev('vkEnumeratePhysicalDevices(instance, &deviceCount, devices.data())', mech='pick1', use=['instance'],
   note='第二问拿清单 → RTX 4070 Ti SUPER')
ev('if (isDeviceSuitable(device)) {', mech='interview',
   note='step-in：进入面试函数（内部还有 findQueueFamilies/扩展检查/能力查询三重幕后）')
ev('physicalDevice = device;', w='physicalDevice', mech='pickdone',
   note='面试通过，说明书到手（查询来的，无销毁义务）')
# isDeviceSuitable 内部
ev('vkGetPhysicalDeviceProperties(device', mech='suit_prop',
   note='面试①：查属性（名字/类型/版本）→ deviceType == DISCRETE_GPU ?')
ev('vkGetPhysicalDeviceFeatures(device', mech='suit_feat',
   note='面试②：查特性 → geometryShader == true ?')
ev('QueueFamilyIndices indices = findQueueFamilies(device);', mech='fam',
   note='面试③ step-in findQueueFamilies：真实族表（6 族，仅族 0 有 G 位）')
ev('bool extensionsSupported = checkDeviceExtensionSupport(device);', mech='extchk',
   note='面试④ step-in：两段式枚举设备扩展，对照 VK_KHR_swapchain')
ev('SwapChainSupportDetails swapChainSupport = querySwapChainSupport(device);', mech='capq',
   note='面试⑤ step-in：六次两段式问 surface 能力（格式/模式）')
ev('return deviceProperties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU', mech='suit_ret',
   note='五项全 ✓ → true')
# findQueueFamilies 内部
ev('vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr)', mech='fam',
   note='族清单两段式第一问 → 实测 6 族')
ev('indices.graphicsFamily = i;', mech='fam', note='族 0 有 G 位 → graphicsFamily = 0')
ev('vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &presentSupport)', mech='fampresent',
   note='逐族问：这个族能往 surface 呈现吗？（族×surface 的接线关系，不是队列的属性）')
ev('indices.presentFamily = i;', mech='fam', note='族 0 ✓ → presentFamily = 0（与 graphics 同族！）')
# createLogicalDevice
ev('QueueFamilyIndices indices = findQueueFamilies(physicalDevice);', mech='fam',
   note='又查一次（上次结果没存——之前聊过的教程取舍）')
ev('std::set<uint32_t> uniqueQueueFamilies', mech='dedup',
   note='{0,0} 去重成 {0}——同族只建一份队列CreateInfo')
ev('queueCreateInfo.queueFamilyIndex = queueFamily;', field=['queueCreateInfo.queueFamilyIndex','0','← findQueueFamilies'])
ev('queueCreateInfo.queueCount = 1;', field=['queueCreateInfo.queueCount','1','常量（16 条池只取 1）'])
ev('queueCreateInfo.pQueuePriorities = &queuePriority;', field=['queueCreateInfo.pQueuePriorities','&1.0f','必填'])
ev('vkCreateDevice(physicalDevice', w='device', use=['physicalDevice'], mech='dev',
   note='出示说明书签合同 → 驱动开 device 连接，按合同建 1 条队列')
ev('vkGetDeviceQueue(device, indices.graphicsFamily.value(), 0, &graphicsQueue)', w='graphicsQueue', use=['device'], mech='qtake',
   note='族 0 池（16 条）取 index 0 → 通道点亮')
ev('vkGetDeviceQueue(device, indices.presentFamily.value(), 0, &presentQueue)', w='presentQueue', use=['device'], mech='qtake2',
   note='同族同 index 再取 → 同一条！Watch 两行值一字不差')
# createSwapChain
ev('SwapChainSupportDetails swapChainSupport = querySwapChainSupport(physicalDevice);', mech='capq',
   note='step-in：问 surface 能力（min=2 max=8 张 · 格式 7 · 模式 5）')
ev('VkSurfaceFormatKHR surfaceFormat = chooseSwapSurfaceFormat', mech='fmt',
   note='step-in 三选①：遍历 7 种格式找 B8G8R8A8_SRGB(50)')
ev('VkPresentModeKHR presentMode = chooseSwapPresentMode', mech='prm',
   note='三选②：找 MAILBOX → 实测命中（不撕裂+低延迟）')
ev('VkExtent2D extent = chooseSwapExtent', mech='ext',
   note='三选③：驱动直接给了 currentExtent=800×600')
ev('uint32_t imageCount = swapChainSupport.capablities.minImageCount + 1', mech='cnt',
   note='2+1=3：显示占 1 张时手里还有 2 张')
ev('createInfo.surface = surface;', field=['surface','0x0200…0002','← 成员（createSurface 存的）'])
ev('createInfo.minImageCount = imageCount;', field=['minImageCount','3','← capabilities.min+1'])
ev('createInfo.imageFormat = surfaceFormat.format;', field=['imageFormat','50 (SRGB)','← chooseSwapSurfaceFormat'])
ev('createInfo.imageColorSpace = surfaceFormat.colorSpace;', field=['imageColorSpace','0','← 同上'])
ev('createInfo.imageExtent = extent;', field=['imageExtent','800×600','← chooseSwapExtent'])
ev('createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT', field=['imageUsage','COLOR_ATTACH','常量：要当画布'])
ev('createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE', field=['imageSharingMode','EXCLUSIVE','← 同族（不同族才 CONCURRENT）'])
ev('createInfo.presentMode = presentMode;', field=['presentMode','3 (MAILBOX)','← chooseSwapPresentMode'])
ev('createInfo.clipped = VK_TRUE;', field=['clipped','TRUE','常量'])
ev('vkCreateSwapchainKHR(device', w='swapChain', use=['device','surface'], mech='sccreate',
   note='交表 → 驱动显存真建 3 张图接线到 surface，发号 0x0300…0003')
ev('vkGetSwapchainImagesKHR(device, swapChain, &imageCount, nullptr)', mech='imgq',
   note='两段式第一问：池里有几张？→ 3')
ev('vkGetSwapchainImagesKHR(device, swapChain, &imageCount, swapChainImages.data())', w='swapChainImages', use=['swapChain'],
   mech='imgq2', note='第二问领编号 04/05/06')
# imageViews
ev('vkCreateImageView(device, &createInfo, nullptr, &swapChainImageViews[i])', w='swapChainImageViews',
   use=['device'], mech='view',
   note='每张图配取景器（view 描述怎么读：格式+范围）。NVIDIA 从 0 起号')
ev('swapChainImageFormat = surfaceFormat.format;', w='swapChainImageFormat', mech='-',
   note='记录协商结果给后面管线用')
ev('swapChainExtent = extent;', w='swapChainExtent', mech='-', note='同上')
ev('swapChainImageFormat = surfaceFormat.format;\n    swapChainExtent = extent;', w='swapChainExtent')
# main/kill
ev('while (!glfwWindowShouldClose(window))', mech='loop', use=['window'],
   note='初始化完成！三图轮转（14 课填内容）')
ev('vkDestroySwapchainKHR', kill='swapChain,swapChainImages,swapChainImageViews', mech='kill',
   note='逆序开始：图+视图随 swapChain 走')
ev('vkDestroyDevice', kill='device,graphicsQueue,presentQueue', mech='kill', note='队列随 device 走')
ev('vkDestroySurfaceKHR', kill='surface', mech='kill')
ev('vkDestroyInstance', kill='instance,debugMessenger', mech='killall',
   note='会话根最后关 —— Watch 归零，零泄漏 ✓（messenger 已在前面退）')

# 生成播放序列：跳过纯空行/纯大括号行；注释行保留（教学价值）
seq = []
for idx, raw in enumerate(lines):
    t = raw.strip()
    if not t: continue                    # 空行跳过
    if t in ('{','}','};','} else {','else {'): continue  # 纯括号跳过
    entry = {'c': raw.rstrip(), 'src': idx+1}
    for sub, kw in E:
        if sub in raw:
            entry.update(kw); break
    seq.append(entry)

json_lines = json.dumps(seq, ensure_ascii=False)
html_head = '''<!DOCTYPE html>
<html lang="zh"><head><meta charset="UTF-8">
<title>vulkan.cpp 完整代码运行动画（含幕后）</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:"Segoe UI","Microsoft YaHei",sans-serif;background:#16181f;color:#d8dae0;padding:10px}
h1{font-size:15px;color:#7ab0afff;margin-bottom:2px}
.sub{font-size:11px;color:#8a8f9a;margin-bottom:6px}
#wrap{display:flex;gap:8px;height:660px}
#codebox{flex:1;background:#12141a;border:1px solid #2a2e38;border-radius:8px;display:flex;flex-direction:column;overflow:hidden}
#cboxhead{font-size:11px;padding:5px 10px;color:#8a8f9a;background:#191c24;border-bottom:1px solid #2a2e38}
#code{flex:1;overflow:auto;padding:4px 0;font-family:Consolas,monospace;font-size:11.5px;line-height:1.55}
.ln{padding:0 8px 0 40px;position:relative;white-space:pre;color:#a9b1c0;min-height:18px}
.ln::before{content:attr(data-s);position:absolute;left:8px;color:#3d4354;font-size:9px}
.ln.cm{color:#5d6370;font-style:italic}
.ln.cur{background:rgba(122,176,255,.18);border-left:3px solid #7ab0ff;color:#eaf0ff}
.ln .kw{color:#c792ea}.ln .vk{color:#82aaff}.ln .str{color:#9ec787}.ln .num{color:#f78c6c}
#right{flex:1.15;display:flex;flex-direction:column;gap:8px}
#mech{flex:1;background:#12141a;border:1px solid #2a2e38;border-radius:8px;position:relative;overflow:hidden;display:flex;flex-direction:column}
.mhead{font-size:11px;padding:5px 10px;color:#8a8f9a;background:#191c24;border-bottom:1px solid #2a2e38}
#canvas{flex:1;position:relative;overflow:auto;padding:8px;font-size:12px}
#mnote{padding:6px 10px;border-top:1px solid #2a2e38;color:#aab0bc;font-size:11.5px;line-height:1.5;background:#161a22;min-height:44px}
#watchbox{height:150px;background:#12141a;border:1px solid #2a2e38;border-radius:8px;overflow:hidden;display:flex;flex-direction:column}
#wscroll{overflow:auto}
#wtable{width:100%;border-collapse:collapse;font-family:Consolas,monospace;font-size:10.5px}
#wtable td{padding:2px 8px;border-bottom:1px solid #1d2029}
#wtable td.nm{color:#9ec787;white-space:nowrap}#wtable td.vl{color:#a9b1c0;word-break:break-all}
#wtable tr.hit td{background:rgba(255,211,92,.12)}#wtable tr.hit td.vl{color:#ffd35c;font-weight:700}
/* 结构体填充面板 */
#fieldbox{margin:6px 0;font-family:Consolas,monospace;font-size:11.5px;border:1px solid #2a2e38;border-radius:6px;overflow:hidden}
#fieldbox .ftitle{padding:3px 8px;background:#191c24;color:#82aaff;font-size:10.5px}
#fieldbox .frow{display:flex;padding:2px 8px;border-top:1px solid #1d2029;color:#5d6370;transition:all .3s}
#fieldbox .frow .fn{flex:1.1;color:#a9b1c0}
#fieldbox .frow .fv{flex:1;color:#f78c6c}
#fieldbox .frow .fs{flex:1.2;color:#5d6370;font-size:10px}
#fieldbox .frow.on{background:rgba(255,211,92,.14)}
#fieldbox .frow.on .fn{color:#ffd35c;font-weight:700}
#fieldbox .frow.on .fs{color:#9ec787}
/* 族表/队列池等 */
.fam{display:grid;grid-template-columns:auto auto auto;gap:1px 10px;font-family:Consolas,monospace;font-size:11px;margin:4px 0}
.fam div{color:#5d6370}.fam .on{color:#ffd35c;font-weight:700}
.qpool{display:flex;gap:3px;flex-wrap:wrap;margin:4px 0}
.qslot{width:14px;height:9px;border-radius:2px;background:#232733;border:1px solid #3a4152}
.qslot.taken{background:#76b900;border-color:#9ec787}
.qslot.same{background:#ffd35c;border-color:#ffd35c}
.gpuname{color:#76b900;font-weight:700;font-size:13px}
.opt{font-family:Consolas,monospace;font-size:11px;line-height:1.8}
.opt .oi{color:#5d6370}.opt .oi.pick{color:#ffd35c;font-weight:700}.opt .oi.dim{color:#3d4354}
.tag{font-size:9.5px;color:#5d6370}
#ctrl{margin-top:6px;display:flex;gap:8px;align-items:center}
button{background:#2a3140;color:#d8dae0;border:1px solid #3a4152;border-radius:6px;padding:5px 14px;font-size:13px;cursor:pointer}
button:hover{background:#39445a}
#pos{font-size:12px;color:#8a8f9a;margin-left:auto}
.speed{font-size:11px;color:#8a8f9a}
</style></head><body>
<h1>vulkan.cpp 完整代码运行动画 — 全文逐行 + Vulkan 幕后数据流</h1>
<div class="sub">左：vulkan.cpp 全文（真实行号）· 右上：幕后（结构体字段来源 / 族表 / 队列池 / 三选协商 · 全部本机实测数据）· 右下：Watch</div>
<div id="wrap">
<div id="codebox"><div id="cboxhead">vulkan.cpp — 446 行全文</div><div id="code"></div></div>
<div id="right">
<div id="mech"><div class="mhead">幕后 — 这行代码在 Vulkan 里做了什么 / 数据从哪来</div><div id="canvas"></div><div id="mnote">按 ▶ 开始。</div></div>
<div id="watchbox"><div class="mhead">Watch — 成员变量真实值（实测）</div><div id="wscroll"><table id="wtable"></table></div></div>
</div></div>
<div id="ctrl">
<button onclick="mv(-1)">◀ 上一行</button><button onclick="auto()" id="abtn">▶ 自动播放</button>
<button onclick="mv(1)">下一行 ▶</button><button onclick="resetAll()">↺ 重置</button>
<span class="speed">事件行停 1.6s / 普通行停 0.25s</span><span id="pos"></span>
</div>
<script>
const L='''
html_tail = '''
const W=[
["window","GLFWwindow*","nullptr","0x0000017B4A45E160"],
["instance","VkInstance","VK_NULL_HANDLE","0x0000017B4D142FA0"],
["debugMessenger","…MessengerEXT","VK_NULL_HANDLE","0x0000010000000001（验证层编号）"],
["surface","VkSurfaceKHR","VK_NULL_HANDLE","0x0000020000000002"],
["physicalDevice","VkPhysicalDevice","VK_NULL_HANDLE","0x0000017B4D158F30（查询）"],
["device","VkDevice","VK_NULL_HANDLE","0x0000017B006800D0"],
["graphicsQueue","VkQueue","VK_NULL_HANDLE","0x0000017B003A14D0"],
["presentQueue","VkQueue","VK_NULL_HANDLE","0x0000017B003A14D0（同gfxQ！）"],
["swapChain","VkSwapchainKHR","VK_NULL_HANDLE","0x0000030000000003"],
["swapChainImages","vector<VkImage>","{}","[0x400…0004, 05, 06]"],
["swapChainImageViews","vector<VkImageView>","{}","[0x0, 0x1, 0x2]"],
["swapChainImageFormat","VkFormat","0","50 = B8G8R8A8_SRGB"],
["swapChainExtent","VkExtent2D","0×0","800 × 600"]];
const FAMS=[[0,"G C T O",16],[1,"C T",2],[2,"C T O",8],[3,"T O",1],[4,"T O",2],[5,"T O",1]];
const FMTS=[["44","B8G8R8A8_UNORM",""],["50","B8G8R8A8_SRGB","✓ 我们选的"],["37","R8G8B8A8_UNORM",""],
["43","R16G16B16A16_SFLOAT","HDR浮点"],["97","(EXT)","扩展"],["64","A2R10G10B10","HDR10"],["64","A2B10G10R10",""]];
const PRMS=[["2","FIFO_RELAXED",""],["3","MAILBOX","✓ 选中的"],["1","FIFO","vsync"],["0","IMMEDIATE","撕裂"],["1000361000","FIFO_LATEST","EXT"]];
const R=codeEl=document.getElementById("code"),wt=document.getElementById("wtable"),cv=document.getElementById("canvas"),mn=document.getElementById("mnote");
let cur=-1,timer=null,fields=[],loopTimer=null;
L.forEach((o,i)=>{const d=document.createElement("div");d.className="ln"+(o.c.trim().startsWith("//")?" cm":"");
d.dataset.s=o.src;d.id="ln"+i;
let h=o.c.replace(/&/g,"&amp;").replace(/</g,"&lt;")
 .replace(/(vk[A-Za-z]+|glfw[A-Za-z]+|Vk[A-Za-z]+|GLFW_[A-Z_]+)/g,'<span class="vk">$1</span>')
 .replace(/(void|if|else|while|for|return|const|auto|static|struct|class)/g,'<span class="kw">$1</span>')
 .replace(/"([^"]*)"/g,'<span class="str">"$1"</span>');
d.innerHTML=h;codeEl.appendChild(d);});
W.forEach((w,i)=>{const tr=document.createElement("tr");tr.id="w"+i;
tr.innerHTML='<td class="nm">'+w[0]+'</td><td class="vl" id="wv'+i+'">'+w[2]+'</td>';wt.appendChild(tr);});
function setW(n,v){const i=W.findIndex(w=>w[0]===n);if(i<0)return;
document.getElementById("wv"+i).textContent=v;const tr=document.getElementById("w"+i);
tr.classList.add("hit");setTimeout(()=>tr.classList.remove("hit"),1400);}
function addField(f){ if(!f)return;
 if(!fields.find(x=>x[0]===f[0])) fields.push(f);
 drawFields(f[0]); }
function drawFields(hl){
 let h='<div id="fieldbox"><div class="ftitle">正在填充的 createInfo 结构体（字段 ← 数据来源）</div>';
 fields.forEach(f=>{h+='<div class="frow'+(f[0]===hl?' on':'')+'"><div class="fn">'+f[0]+'</div><div class="fv">'+f[1]+'</div><div class="fs">← '+f[2]+'</div></div>';});
 h+='</div>'; return h; }
function famTable(pool,qsame){
 let h='<div class="gpuname">RTX 4070 Ti SUPER</div><div style="font-size:10px;color:#5d6370">真实族表（6 族 · 仅族 0 有 G 位）</div><div class="fam">';
 FAMS.forEach((f,i)=>{h+='<div'+(i===0?' class="on"':'')+'>族'+f[0]+'</div><div'+(i===0?' class="on"':'')+'>'+f[1]+'</div><div'+(i===0?' class="on"':'')+'>'+f[2]+'条</div>';});
 h+='</div>';
 if(pool){h+='<div style="font-size:10.5px;color:#8a8f9a">族0 队列池（16条 · 取 index 0）：</div><div class="qpool">';
 for(let k=0;k<16;k++)h+='<div class="qslot'+(k===0?(qsame?' taken same':' taken'):'')+'"></div>';
 h+='</div>'+(qsame?'<div style="font-size:10px;color:#ffd35c">金色 = 两次 GetDeviceQueue 同一条</div>':'');}
 return h; }
function optList(title,items,pick){
 let h='<div style="color:#8a8f9a;font-size:10.5px;margin:2px 0">'+title+'</div><div class="opt">';
 items.forEach((it,i)=>{h+='<div class="oi'+(i===pick?' pick':(pick>=0?' dim':''))+'">'+(i===pick?'▶ ':'&nbsp;&nbsp;')+it[0]+' '+it[1]+(it[2]?' <span class="tag">'+it[2]+'</span>':'')+'</div>';});
 return h+'</div>'; }
function mech(m){
 if(loopTimer){clearInterval(loopTimer);loopTimer=null;}
 let h='';
 if(m==='win')h='<div style="color:#9ec787;font-size:13px;margin:20px 0">GLFW 窗口诞生（800×600）<br><span style="font-size:11px;color:#5d6370">window 是 GLFW 的对象 —— Vulkan 只认 surface 不认窗口</span></div>';
 else if(m==='inst')h='<div style="margin:8px 0">'+drawFields()+'<div style="font-size:11px;color:#8a8f9a;margin-top:4px">交表 → loader 按名单装驱动 → 发证 <b style="color:#ffd35c">0x0000017B4D142FA0</b></div></div>';
 else if(m==='extq')h=optList('数据来源：GLFW 说开窗要这三个',[["VK_KHR_surface","跨平台"],["VK_KHR_win32_surface","Windows"],["VK_EXT_debug_utils","调试回调"]],-1);
 else if(m==='msg')h='<div style="margin:8px 0;font-size:12px">出示 instance 按名查函数地址（扩展函数不自动加载）<br>→ 验证层发证 <b style="color:#ffd35c">0x0000010000000001</b><br><span style="font-size:10.5px;color:#5d6370">0x0100…0001 = 验证层内部编号（不是堆指针）</span></div>';
 else if(m==='surf')h='<div style="margin:8px 0;font-size:12px">出示 instance + window<br>→ OS 侧登记呈现接头 <b style="color:#ffd35c">0x0200…0002</b><br><span style="font-size:10.5px;color:#5d6370">之后 swapchain/呈现模式都问它</span></div>';
 else if(m==='pick0')h='<div style="margin:8px 0;font-size:12px">两段式第一问（传 nullptr）：<br>→ <b style="color:#ffd35c">deviceCount = 1</b>（这台机器 1 张卡）</div>';
 else if(m==='pick1')h='<div style="margin:8px 0">'+famTable(false)+'</div>';
 else if(m==='interview')h='<div style="margin:8px 0;font-size:12px;color:#aab0bc">step-in 面试 →<br>① 属性查询（独显？）<br>② 特性查询（几何着色器？）<br>③ findQueueFamilies（族齐？）<br>④ 扩展检查（swapchain？）<br>⑤ 能力查询（格式/模式非空？）</div>';
 else if(m==='suit_prop')h='<div style="margin:8px 0;font-size:12px">vkGetPhysicalDeviceProperties<br>→ deviceType = <b style="color:#9ec787">VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ✓</b></div>';
 else if(m==='suit_feat')h='<div style="margin:8px 0;font-size:12px">vkGetPhysicalDeviceFeatures<br>→ geometryShader = <b style="color:#9ec787">true ✓</b></div>';
 else if(m==='fam')h='<div style="margin:6px 0">'+famTable(false)+'</div>';
 else if(m==='fampresent')h='<div style="margin:8px 0">'+famTable(false)+'<div style="font-size:11px;color:#ffd35c;margin-top:4px">族0 × surface ✓ → presentFamily = 0</div></div>';
 else if(m==='extchk')h=optList('设备扩展对照（两段式枚举）',[["VK_KHR_swapchain","✓ 在清单里"],["_ 分 dozens 个其他扩展","（未启用）"]],0);
 else if(m==='capq')h='<div style="margin:6px 0">'+optList('能力（问 surface）',[["minImageCount","2"],["maxImageCount","8"],["currentExtent","800×600"]],-1)+optList('格式 7 种 → 选',FMTS.slice(0,3),1)+'</div>';
 else if(m==='suit_ret')h='<div style="margin:8px 0;font-size:13px;color:#9ec787">五项全 ✓ → true → 录用</div>';
 else if(m==='pickdone')h='<div style="margin:8px 0">'+famTable(false)+'<div style="font-size:11px;color:#ffd35c">说明书 0x…58F30 到手（查询的，无销毁）</div></div>';
 else if(m==='dedup')h='<div style="margin:8px 0;font-size:12px">{graphicsFamily=0, presentFamily=0}<br>→ set 去重 → <b style="color:#ffd35c">{0}</b>（同族只建一份队列申请）</div>';
 else if(m==='dev')h='<div style="margin:6px 0">'+famTable(false)+'<div style="margin-top:6px;border:2px dashed #ff9d5c;border-radius:8px;padding:6px;display:inline-block;color:#ff9d5c;font-weight:700">device 0x…6800D0 ✓</div></div>';
 else if(m==='qtake')h='<div style="margin:6px 0">'+famTable(true,false)+'</div>';
 else if(m==='qtake2')h='<div style="margin:6px 0">'+famTable(true,true)+'</div>';
 else if(m==='fmt')h='<div style="margin:6px 0">'+optList('三选① 格式：像素怎么存',FMTS,1)+'</div>';
 else if(m==='prm')h='<div style="margin:6px 0">'+optList('三选② 呈现模式：何时换帧',PRMS,1)+'</div>';
 else if(m==='ext')h='<div style="margin:6px 0">'+optList('三选③ 尺寸',[["currentExtent","800×600 ✓（驱动直接给）"],["手动 clamp","（当 extent=0xFFFFFFFF 时才用）"]],0)+'</div>';
 else if(m==='cnt')h=optList('张数协商',[["2","驱动最少"],["2+1=3","▶ 我们要的"],["8","驱动最多"]],1);
 else if(m==='sccreate')h='<div style="margin:6px 0">'+drawFields()+'<div style="font-size:11px;color:#8a8f9a;margin-top:4px">交表 → 显存建 3 张图 → 接线 surface → 发号 <b style="color:#ffd35c">0x0300…0003</b></div></div>';
 else if(m==='imgq')h='<div style="margin:8px 0;font-size:12px">两段式第一问：池里有几张？→ <b style="color:#ffd35c">3</b></div>';
 else if(m==='imgq2')h='<div style="margin:8px 0;font-family:Consolas;font-size:12px;line-height:2">img0 = 0x400000000004<br>img1 = 0x400000000005<br>img2 = 0x400000000006<br><span style="font-size:10.5px;color:#5d6370">驱动按创建顺序发号</span></div>';
 else if(m==='view')h='<div style="margin:8px 0;font-size:12px">'+[0,1,2].map(i=>'图'+i+' → view'+i+' = '+i).join('<br>')+'<br><span style="font-size:10.5px;color:#5d6370">NVIDIA 从 0 起号；view 描述怎么读图（格式+范围）</span></div>';
 else if(m==='loop'){h='<div class="sc3" style="position:relative;height:130px;width:300px;margin:6px auto"><div class="fr f0" id="lf0" style="position:absolute;width:80px;height:54px;border:2px solid #3a4152;border-radius:6px;text-align:center;font-size:10px;padding-top:4px">图A</div><div class="fr f1" id="lf1" style="position:absolute;width:80px;height:54px;border:2px solid #3a4152;border-radius:6px;text-align:center;font-size:10px;padding-top:4px">图B</div><div class="fr f2" id="lf2" style="position:absolute;width:80px;height:54px;border:2px solid #3a4152;border-radius:6px;text-align:center;font-size:10px;padding-top:4px">图C</div></div><div style="text-align:center;font-size:10.5px;color:#5d6370">acquire→画→present（14课填真内容）</div>';
  const st=[["render","✏️","border-color:#ffd35c"],["queued","📥","border-color:#7ab0ff"],["display","🖥️","border-color:#76b900"]];let t=0;
  const tick=()=>{["lf0","lf1","lf2"].forEach((p,i)=>{const el=document.getElementById(p);if(el){const s=st[(i+t)%3];el.textContent="图"+["A","B","C"][i]+" "+s[1];el.style.cssText+=s[2];}});t++;};
  tick();loopTimer=setInterval(tick,900);}
 else if(m==='kill')h='<div style="text-align:center;margin:30px 0;color:#8a8f9a">逆序销毁中…… Watch 逐行归零</div>';
 else if(m==='killall')h='<div style="text-align:center;margin:30px 0"><span style="color:#9ec787;font-size:14px">零泄漏 ✓ 验证层全程盯梢</span></div>';
 else h=drawFields();
 cv.innerHTML=h;
}
function render(i){
 if(i===cur)return;
 if(i<cur){resetAll(false);for(let k=0;k<=i;k++)apply(k);}
 else apply(i);
 cur=i;
 document.querySelectorAll(".ln").forEach(e=>e.classList.remove("cur"));
 const el=document.getElementById("ln"+i);el.classList.add("cur");el.scrollIntoView({block:"center"});
 const o=L[i];
 if(o.mech&&o.mech!=="-"){ if(o.field)addField(o.field); mech(o.mech); }
 else if(o.field){ addField(o.field); mech(); }
 else { mech(o.mech||""); }
 mn.innerHTML='<b style="color:#7ab0ff">vulkan.cpp:'+o.src+'</b> '+(o.note||'<span style="color:#3d4354">（填表/常规行）</span>');
 (o.kill||"").split(",").forEach(n=>{if(n.trim())setW(n.trim(),"（已销毁）")});
 document.getElementById("pos").textContent="播放行 "+(i+1)+" / "+L.length+"（源码 "+o.src+" 行）";
}
function apply(i){const o=L[i];
 (o.use||[]).forEach(n=>{const j=W.findIndex(w=>w[0]===n);if(j>=0){const tr=document.getElementById("w"+j);tr.classList.add("hit");setTimeout(()=>tr.classList.remove("hit"),1200);}});
 if(o.w)setW(o.w, W.find(w=>w[0]===o.w)[3]);
}
function resetAll(r=true){W.forEach((w,i)=>{document.getElementById("wv"+i).textContent=w[2];});
 fields=[];if(loopTimer){clearInterval(loopTimer);loopTimer=null;}
 if(r){cur=-1;cv.innerHTML="";mn.textContent="已重置。";}}
function mv(d){render(Math.max(0,Math.min(L.length-1,(cur<0?0:cur)+d)));}
function auto(){
 if(timer){clearInterval(timer);timer=null;document.getElementById("abtn").textContent="▶ 自动播放";return;}
 document.getElementById("abtn").textContent="⏸ 暂停";
 const tick=()=>{if(cur>=L.length-1){clearInterval(timer);timer=null;document.getElementById("abtn").textContent="▶ 自动播放";return;}
  mv(1); const o=L[cur];
  timer=setTimeout(tick, (o.note||o.field||o.w||o.mech)?1600:250);};
 timer=setTimeout(tick,300);}
render(0);
</script></body></html>
'''
io.open('../代码运行动画.html', 'w', encoding='utf-8', newline='\n').write(html_head + json_lines + ';\n' + html_tail)
print('生成完成：', len(seq), '个播放行（源码', len(lines), '行）')
