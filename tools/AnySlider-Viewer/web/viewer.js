'use strict';
const $ = s => document.querySelector(s);
const hex = (v,w=2) => v.toString(16).toUpperCase().padStart(w,'0');
const faces={TRIANGLE:[3,'△'],SQUARE:[6,'□'],CROSS:[4,'×'],CIRCLE:[7,'○']};
const slider=$('#slider'),output=$('#outputByteSummary');
for(let i=0;i<32;i++){const n=document.createElement('div');n.className='slider-cell';n.dataset.slider=i;n.title=`LED ${i} · BRG bytes ${1+i*3}–${3+i*3}`;n.innerHTML=`<span>${i}</span>`;slider.append(n);}
for(const [name,[bit,symbol]] of Object.entries(faces)){const n=document.createElement('div');n.className='key face-key';n.dataset.button=name;n.dataset.lamp=bit;n.title=`${name} LED · IO3 bit ${bit}`;n.innerHTML=`<span>${symbol}</span>`;$('#faceButtons').append(n);}
for(let i=0;i<97;i++){const n=document.createElement('div');n.className='byte-card';n.innerHTML=`<label>${i}</label><strong>00</strong>`;output.append(n);}
if(new URLSearchParams(location.search).get('overlay')==='1')document.body.classList.add('overlay');
let count=0,lastMessage=0,lastSequence='',lastChanged=0,lastState='';
function log(text){const n=document.createElement('span');n.className='log-line';n.textContent=`[${new Date().toLocaleTimeString()}] ${text}\n`;$('#log').append(n);while($('#log').childNodes.length>200)$('#log').firstChild.remove();$('#log').scrollTop=$('#log').scrollHeight;}
function renderPartitions(bitmap){document.querySelectorAll("[data-partition]").forEach(n=>{const bit=Number(n.dataset.rgbStart),rgb=[0,1,2].map(i=>bitmap===null?0:((bitmap&(1<<(bit+i)))?255:0));n.style.backgroundColor=`rgb(${rgb.join(" ")})`;const label=bitmap===null?`${n.dataset.partition} partition LED unavailable`:`${n.dataset.partition} partition LED · RGB(${rgb.join(", ")}) · IO3 bits ${bit}–${bit+2}`;n.title=label;n.setAttribute("aria-label",label);});}
function clearLed(){renderPartitions(null);slider.childNodes.forEach(n=>{n.style.setProperty('--led-color','rgb(0 0 0)');n.classList.remove('led-on');});document.querySelectorAll('[data-lamp]').forEach(n=>{n.classList.remove('lit');n.setAttribute('aria-label',`${n.dataset.button} LED unavailable`);});output.childNodes.forEach(n=>n.querySelector('strong').textContent='—');$('#outputHexDump').textContent='LED snapshot unavailable';$('#wsSequence').textContent='—';$('#io3Bitmap').textContent='—';lastSequence='';document.body.classList.remove('led-stale');}
function state(text,connected){$('#connectionText').textContent=text;$('#connectionState').classList.toggle('connected',connected);if(text!==lastState){log(text);lastState=text;}}
function render(frame){const now=performance.now();lastMessage=now;
  if(frame.led){const led=frame.led,raw=Uint8Array.from(atob(led.sliderBase64),c=>c.charCodeAt(0));if(raw.length!==97||raw[0]!==63)throw Error('Expected 97-byte LED payload / 0x3F prefix');
    if(led.sequence!==lastSequence){lastSequence=led.sequence;lastChanged=now;}
    renderPartitions(led.io3Bitmap);
    $('#wsSequence').textContent=`${led.sequence} / ${led.timestampMs}`;$('#io3Bitmap').textContent=`0x${hex(led.io3Bitmap,8)}`;
    slider.childNodes.forEach((n,i)=>{const b=raw[1+i*3],r=raw[2+i*3],g=raw[3+i*3];n.style.setProperty('--led-color',`rgb(${r} ${g} ${b})`);n.title=`LED ${i} · RGB(${r}, ${g}, ${b})`;n.setAttribute('aria-label',n.title);n.classList.toggle('led-on',Boolean(b||r||g));});
    document.querySelectorAll('[data-lamp]').forEach(n=>{const on=Boolean(led.io3Bitmap&(1<<Number(n.dataset.lamp)));n.classList.toggle('lit',on);n.setAttribute('aria-label',`${n.dataset.button} LED ${on?'on':'off'}`);});
    output.childNodes.forEach((n,i)=>n.querySelector('strong').textContent=hex(raw[i]));$('#outputHexDump').textContent=Array.from(raw,v=>hex(v)).join(' ');
  }else clearLed();
  $('#reportCount').textContent=++count;state(`${frame.source==='abi-fixture'?'[Test fixture] ':''}${frame.status}${frame.error?': '+frame.error:''}`,frame.status==='available');if($('#rawLogging').checked)log(JSON.stringify(frame));
}
let inputSocket=null,inputEnabled=false;
function connect(){const socket=inputSocket=new WebSocket(`${location.protocol==='https:'?'wss':'ws'}://${location.host}/ws`);socket.onopen=()=>state('Connected, waiting for LED snapshot',true);socket.onmessage=e=>{try{const frame=JSON.parse(e.data);render(frame);inputEnabled=Boolean(frame.inputEnabled);$('#enableInput').checked=inputEnabled;$('#inputStatus').textContent=frame.inputError|| (inputEnabled?'Input enabled · '+($('#singleFrameInput').checked?'Immediate press/release snapshots':'Hold / drag with the mouse'):'Input disabled. Stop other MMIO producers before enabling.');document.body.classList.toggle('input-enabled',inputEnabled);if(!inputEnabled)clearPointers();}catch(error){clearLed();state(`Data error: ${error.message}`,false);}};socket.onclose=()=>{inputEnabled=false;$('#enableInput').checked=false;document.body.classList.remove('input-enabled');clearPointers();clearLed();state('Disconnected, reconnecting',false);setTimeout(connect,1000);};socket.onerror=()=>state('Connection failed',false);}
setInterval(()=>{const now=performance.now();if(lastMessage&&now-lastMessage>3000){clearLed();state('WebSocket data timeout',false);}document.body.classList.toggle('led-stale',Boolean(lastSequence)&&now-lastChanged>2000);},250);
$('#clearLogButton').onclick=()=>$('#log').replaceChildren();$('#copyLogButton').onclick=()=>navigator.clipboard.writeText($('#log').textContent).catch(e=>log(e.message));
const pointers=new Map(), inputButtons={TRIANGLE:8,SQUARE:7,CROSS:10,CIRCLE:9};
function sendInput(command){if(inputSocket?.readyState===WebSocket.OPEN)inputSocket.send(JSON.stringify(command));}
function clearPointers(){pointers.clear();document.querySelectorAll('.input-held').forEach(n=>n.classList.remove('input-held'));}
function targetInput(n){const button=n?.closest('[data-button]'),cell=n?.closest('[data-slider]');return button?{button:inputButtons[button.dataset.button]}:cell?{cell:Number(cell.dataset.slider)}:null;}
function inputState(){let buttons=0,touchMask=0;for(const p of pointers.values()){if(p.button!==undefined)buttons|=1<<p.button;if(p.cell!==undefined)touchMask|=1<<p.cell;}return {buttons:buttons>>>0,touchMask:touchMask>>>0};}
function publishPointers(){sendInput({type:'state',...inputState()});document.querySelectorAll('[data-button],[data-slider]').forEach(n=>{const p=targetInput(n);n.classList.toggle('input-held',Array.from(pointers.values()).some(v=>p.button!==undefined?v.button===p.button:v.cell===p.cell));});}
$('#enableInput').onchange=e=>{clearPointers();sendInput({type:e.target.checked?'enable':'disable'});};
$('#singleFrameInput').onchange=()=>{clearPointers();if(inputEnabled)sendInput({type:'release'});};
document.querySelector('.controller-shell').addEventListener('pointerdown',e=>{if(!inputEnabled||e.button!==0)return;const p=targetInput(e.target);if(!p)return;e.preventDefault();
  if($('#singleFrameInput').checked){sendInput({type:'pulse',buttons:p.button===undefined?0:(1<<p.button)>>>0,touchMask:p.cell===undefined?0:(1<<p.cell)>>>0});log(`Pulse ${p.button===undefined?'slider '+p.cell:'button '+p.button}: down → release snapshots (no hold)`);return;}
  pointers.set(e.pointerId,p);e.target.setPointerCapture(e.pointerId);publishPointers();});
window.addEventListener('pointermove',e=>{const p=pointers.get(e.pointerId);if(!p||p.button!==undefined)return;const target=targetInput(document.elementFromPoint(e.clientX,e.clientY));const next=target?.cell===undefined?{}:target;if(next.cell!==p.cell){pointers.set(e.pointerId,next);publishPointers();}});
function releasePointer(e){if(pointers.delete(e.pointerId))publishPointers();}
window.addEventListener('pointerup',releasePointer);window.addEventListener('pointercancel',releasePointer);window.addEventListener('lostpointercapture',releasePointer);
function releaseAll(){clearPointers();if(inputEnabled)sendInput({type:'release'});}
window.addEventListener('blur',releaseAll);document.addEventListener('visibilitychange',()=>{if(document.hidden)releaseAll();});window.addEventListener('pagehide',()=>{sendInput({type:'disable'});clearPointers();});
setInterval(()=>{if(inputEnabled)sendInput({type:'keepalive'});},250);
clearLed();log('LED output + opt-in debug input. Unchanged sequence for 2 seconds is marked stale; this does not indicate a physical device disconnect.');connect();
